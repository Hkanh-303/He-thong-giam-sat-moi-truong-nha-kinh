#include "wifi_sender.h"
#include "wifi_sender_config.h"

#include <stdio.h>
#include <string.h>

#include "esp_event.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#define WIFI_CONNECTED_BIT BIT0

static const char *TAG = "WIFI_SENDER";
static EventGroupHandle_t s_wifi_event_group;

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    (void)arg;

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        ESP_LOGW(TAG, "Mat ket noi Wi-Fi, dang thu ket noi lai...");
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *event = (const ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Da ket noi Wi-Fi, IP ESP32: " IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

static esp_err_t init_network_interfaces(void)
{
    esp_err_t err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    if (esp_netif_create_default_wifi_sta() == NULL) {
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t wifi_sender_init(void)
{
    if (s_wifi_event_group != NULL) {
        return ESP_OK;
    }

    if (WIFI_SSID[0] == '\0') {
        ESP_LOGW(TAG, "WIFI_SSID dang de trong; chi tao JSON, chua gui qua Wi-Fi");
        return ESP_OK;
    }

    s_wifi_event_group = xEventGroupCreate();
    if (s_wifi_event_group == NULL) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = init_network_interfaces();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Khoi tao network interface that bai: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Dang ky Wi-Fi event that bai: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Dang ky IP event that bai: %s", esp_err_to_name(err));
        return err;
    }

    wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&init_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Khoi tao Wi-Fi that bai: %s", esp_err_to_name(err));
        return err;
    }

    // Luu cau hinh Wi-Fi trong RAM, khong can khoi tao NVS de doc/ghi thong tin ket noi.
    err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Cau hinh Wi-Fi storage that bai: %s", esp_err_to_name(err));
        return err;
    }

    wifi_config_t wifi_config = {0};
    snprintf((char *)wifi_config.sta.ssid, sizeof(wifi_config.sta.ssid), "%s", WIFI_SSID);
    snprintf((char *)wifi_config.sta.password, sizeof(wifi_config.sta.password), "%s", WIFI_PASSWORD);
    wifi_config.sta.pmf_cfg.capable = true;
    wifi_config.sta.pmf_cfg.required = false;

    err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err == ESP_OK) {
        err = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    }
    if (err == ESP_OK) {
        err = esp_wifi_start();
    }

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Cau hinh/khoi dong Wi-Fi that bai: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "Dang ket noi toi Wi-Fi '%s'...", WIFI_SSID);
    return ESP_OK;
}

esp_err_t wifi_sender_post_json(const char *json_payload)
{
    if (json_payload == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_wifi_event_group == NULL ||
        (xEventGroupGetBits(s_wifi_event_group) & WIFI_CONNECTED_BIT) == 0) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_http_client_config_t http_config = {
        .url = RPI_SENSOR_URL,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 3000,
    };
    esp_http_client_handle_t client = esp_http_client_init(&http_config);
    if (client == NULL) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = esp_http_client_set_header(client, "Content-Type", "application/json");
    if (err == ESP_OK) {
        err = esp_http_client_set_post_field(client, json_payload, (int)strlen(json_payload));
    }
    if (err == ESP_OK) {
        err = esp_http_client_perform(client);
    }

    if (err == ESP_OK) {
        int status_code = esp_http_client_get_status_code(client);
        if (status_code >= 200 && status_code < 300) {
            ESP_LOGI(TAG, "Da gui du lieu len Raspberry Pi (HTTP %d)", status_code);
        } else {
            ESP_LOGW(TAG, "Raspberry Pi tra ve HTTP %d", status_code);
            err = ESP_FAIL;
        }
    } else {
        ESP_LOGW(TAG, "Gui du lieu that bai: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
    return err;
}
