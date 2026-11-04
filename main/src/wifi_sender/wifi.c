#include "wifi.h"

#include <stdio.h>
#include <time.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_sntp.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

/* Thay bang thong tin Wi-Fi cua ban. Khong commit mat khau that len Git. */
#define WIFI_SSID       "LAB_DTVT_SV"
#define WIFI_PASSWORD   "Labdtvt@109"

#define WIFI_CONNECTED_BIT BIT0

static const char *TAG = "WIFI";
static EventGroupHandle_t s_wifi_event_group;
static esp_event_handler_instance_t s_wifi_handler_instance = NULL;
static esp_event_handler_instance_t s_ip_handler_instance = NULL;
static bool s_time_synced = false;

static void sntp_time_sync_notification_cb(struct timeval *tv)
{
    time_t now = 0;
    struct tm timeinfo = {0};
    time(&now);
    localtime_r(&now, &timeinfo);
    s_time_synced = true;
    ESP_LOGI(TAG, "Thoi gian da dong bo NTP: %04d-%02d-%02d %02d:%02d:%02d",
             timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
             timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
}

static void sntp_sync_task(void *pvParameters)
{
    (void)pvParameters;
    ESP_LOGI(TAG, "Bat dau dong bo thoi gian NTP o background...");
    if (esp_sntp_enabled()) {
        esp_sntp_stop();
    }
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_setservername(1, "time.google.com");
    esp_sntp_setservername(2, "time.cloudflare.com");
    esp_sntp_set_time_sync_notification_cb(sntp_time_sync_notification_cb);
    esp_sntp_set_sync_mode(SNTP_SYNC_MODE_IMMED);
    esp_sntp_init();

    int retry = 0;
    while (retry < 15) {
        if (!wifi_is_connected()) {
            ESP_LOGW(TAG, "Wi-Fi ngat ket noi trong luc doi SNTP");
            break;
        }
        if (s_time_synced || esp_sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED) {
            s_time_synced = true;
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
        retry++;
    }

    if (!s_time_synced) {
        ESP_LOGW(TAG, "NTP chua dong bo sau 15s (mang co the chan UDP 123 hoac co captive portal). SNTP se tiep tuc thu o nen.");
    }

    vTaskDelete(NULL);
}

bool wifi_is_connected(void)
{
    return s_wifi_event_group != NULL &&
           (xEventGroupGetBits(s_wifi_event_group) & WIFI_CONNECTED_BIT) != 0;
}

bool wifi_is_time_synced(void)
{
    return s_time_synced;
}

static void wifi_event_handler(void *arg,
                               esp_event_base_t event_base,
                               int32_t event_id,
                               void *event_data)
{
    (void)arg;

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT &&
               event_id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        ESP_LOGW(TAG, "Mat Wi-Fi, dang thu ket noi lai...");
        esp_wifi_connect();
    } else if (event_base == IP_EVENT &&
               event_id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *event =
            (const ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Da ket noi Wi-Fi, IP: " IPSTR,
                 IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        // Khoi chay SNTP trong task rieng, tranh block event loop
        xTaskCreate(sntp_sync_task, "sntp_task", 3072, NULL, 3, NULL);
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

esp_err_t wifi_init(void)
{
    if (s_wifi_event_group != NULL) {
        return ESP_OK;
    }

    // Khoi tao NVS truoc Wi-Fi driver; driver can NVS khi esp_wifi_init().
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
        err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        err = nvs_flash_erase();
        if (err == ESP_OK) {
            err = nvs_flash_init();
        }
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Khoi tao NVS loi: %s", esp_err_to_name(err));
        return err;
    }

    s_wifi_event_group = xEventGroupCreate();
    if (s_wifi_event_group == NULL) {
        return ESP_ERR_NO_MEM;
    }

    err = init_network_interfaces();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Khoi tao network interface loi: %s",
                 esp_err_to_name(err));
        return err;
    }

    err = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                              wifi_event_handler, NULL,
                                              &s_wifi_handler_instance);
    if (err != ESP_OK) {
        return err;
    }

    err = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                              wifi_event_handler, NULL,
                                              &s_ip_handler_instance);
    if (err != ESP_OK) {
        esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                              s_wifi_handler_instance);
        return err;
    }

    wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&init_config);
    if (err != ESP_OK) {
        return err;
    }

    // Wi-Fi cau hinh trong RAM, khong luu thong tin ket noi vao NVS.
    err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (err != ESP_OK) {
        return err;
    }

    wifi_config_t wifi_config = {0};
    snprintf((char *)wifi_config.sta.ssid,
             sizeof(wifi_config.sta.ssid), "%s", WIFI_SSID);
    snprintf((char *)wifi_config.sta.password,
             sizeof(wifi_config.sta.password), "%s", WIFI_PASSWORD);
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
        ESP_LOGE(TAG, "Cau hinh/khoi dong Wi-Fi loi: %s",
                 esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "Dang ket noi Wi-Fi '%s'...", WIFI_SSID);
    return ESP_OK;
}
