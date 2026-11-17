#include "wifi.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "cJSON.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_sntp.h"
#include "esp_wifi.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_SSID_MAX_LEN 32
#define WIFI_PASSWORD_MAX_LEN 64
#define WIFI_PORTAL_BODY_MAX 512
#define WIFI_SCAN_MAX_RESULTS 24
#define WIFI_CONNECT_TIMEOUT_MS 20000

typedef enum {
    PORTAL_STATUS_READY = 0,
    PORTAL_STATUS_CONNECTING,
    PORTAL_STATUS_CONNECTED,
    PORTAL_STATUS_FAILED,
    PORTAL_STATUS_SAVE_FAILED,
} portal_status_t;

static const char *TAG = "WIFI";
static EventGroupHandle_t s_wifi_event_group;
static esp_event_handler_instance_t s_wifi_handler_instance = NULL;
static esp_event_handler_instance_t s_ip_handler_instance = NULL;
static httpd_handle_t s_http_server = NULL;
static bool s_time_synced = false;
static bool s_have_saved_credentials = false;
static bool s_connect_on_start = false;
static bool s_force_ap = false;
static bool s_provisioning_pending = false;
static portal_status_t s_portal_status = PORTAL_STATUS_READY;
static char s_pending_ssid[WIFI_SSID_MAX_LEN + 1];
static char s_pending_password[WIFI_PASSWORD_MAX_LEN + 1];
static char s_ap_ssid[33];
static char s_ap_password[33];
static portMUX_TYPE s_state_lock = portMUX_INITIALIZER_UNLOCKED;

static const char s_setup_page[] =
    "<!doctype html><html lang='vi'><head><meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>Thiết lập SmartPlan</title><style>"
    "body{font:16px system-ui;background:#f3f6f4;margin:0;padding:24px;color:#16352b}"
    ".card{max-width:420px;margin:8vh auto;background:white;padding:24px;border-radius:16px;"
    "box-shadow:0 8px 30px #1232}h1{font-size:22px}label{display:block;margin:16px 0 6px}"
    "input,select,button{box-sizing:border-box;width:100%;padding:12px;border:1px solid #bac9c2;"
    "border-radius:8px;font:inherit}button{margin-top:18px;background:#16734f;color:white;"
    "border:0;font-weight:700}#status{min-height:24px;margin-top:14px}</style></head><body>"
    "<main class='card'><h1>Kết nối SmartPlan với Wi‑Fi</h1>"
    "<p>Chọn mạng Wi‑Fi 2.4 GHz gần thiết bị rồi nhập mật khẩu.</p>"
    "<form id='form'><label for='ssid'>Mạng Wi‑Fi</label>"
    "<select id='ssid' name='ssid' required><option value=''>Đang tìm mạng Wi‑Fi…</option></select>"
    "<button id='rescan' type='button'>Quét lại mạng Wi‑Fi</button>"
    "<label for='password'>Mật khẩu</label><input id='password' name='password'"
    "type='password' maxlength='64' autocomplete='new-password'>"
    "<button id='submit'>Lưu và kết nối</button></form><p id='status'></p></main>"
    "<script>const statusEl=document.getElementById('status');const ssidEl=document.getElementById('ssid');let polling=false;"
    "async function scan(){const button=document.getElementById('rescan');button.disabled=true;"
    "ssidEl.innerHTML='<option value=\"\">Đang quét mạng Wi‑Fi…</option>';"
    "try{const r=await fetch('/scan');if(!r.ok)throw new Error(await r.text());const nets=await r.json();"
    "ssidEl.innerHTML='';if(!nets.length){ssidEl.innerHTML='<option value=\"\">Không tìm thấy mạng</option>';"
    "statusEl.textContent='Không tìm thấy Wi‑Fi 2.4 GHz. Hãy thử quét lại.';return;}"
    "for(const n of nets){const o=document.createElement('option');o.value=n.ssid;"
    "o.textContent=n.ssid+' ('+n.rssi+' dBm)'+(n.secure?' 🔒':' — mở');ssidEl.appendChild(o);}"
    "statusEl.textContent='Chọn mạng Wi‑Fi cần kết nối.';}catch(err){ssidEl.innerHTML='<option value=\"\">Không quét được mạng</option>';"
    "statusEl.textContent='Lỗi quét Wi‑Fi: '+err.message;}finally{button.disabled=false;}}"
    "document.getElementById('rescan').addEventListener('click',scan);"
    "document.getElementById('form').addEventListener('submit',async e=>{e.preventDefault();"
    "if(!ssidEl.value){statusEl.textContent='Hãy chọn một mạng Wi‑Fi.';return;}"
    "statusEl.textContent='Đang gửi thông tin…';const data={ssid:ssidEl.value,"
    "password:document.getElementById('password').value};try{const r=await fetch('/wifi',"
    "{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(data)});"
    "if(!r.ok)throw new Error(await r.text());statusEl.textContent='Đang thử kết nối…';"
    "if(!polling){polling=true;poll();}}catch(err){statusEl.textContent='Lỗi: '+err.message;}});"
    "async function poll(){try{const r=await fetch('/status');const d=await r.json();"
    "if(d.status==='connected'){statusEl.textContent='Đã kết nối Wi‑Fi. Thiết bị đang khởi động lại mạng…';return;}"
    "if(d.status==='save_failed'){statusEl.textContent='Đã kết nối nhưng không lưu được cấu hình vào flash. Hãy kiểm tra NVS; Wi‑Fi có thể không còn sau khi khởi động lại.';polling=false;return;}"
    "if(d.status==='failed'){statusEl.textContent='Không kết nối được. Kiểm tra tên mạng/mật khẩu rồi thử lại.';polling=false;return;}"
    "setTimeout(poll,1000);}catch(_){setTimeout(poll,1500);}}scan();</script></body></html>";

static const char *portal_status_name(portal_status_t status)
{
    switch (status) {
    case PORTAL_STATUS_CONNECTING: return "connecting";
    case PORTAL_STATUS_CONNECTED: return "connected";
    case PORTAL_STATUS_FAILED: return "failed";
    case PORTAL_STATUS_SAVE_FAILED: return "save_failed";
    case PORTAL_STATUS_READY:
    default: return "ready";
    }
}

static portal_status_t get_portal_status(void)
{
    portal_status_t status;
    taskENTER_CRITICAL(&s_state_lock);
    status = s_portal_status;
    taskEXIT_CRITICAL(&s_state_lock);
    return status;
}

static void set_portal_status(portal_status_t status)
{
    taskENTER_CRITICAL(&s_state_lock);
    s_portal_status = status;
    taskEXIT_CRITICAL(&s_state_lock);
}

static esp_err_t load_wifi_credentials(char *ssid, size_t ssid_size,
                                       char *password, size_t password_size)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open("wifi_cfg", NVS_READONLY, &handle);
    if (err != ESP_OK) return err;

    size_t ssid_len = ssid_size;
    size_t password_len = password_size;
    err = nvs_get_str(handle, "ssid", ssid, &ssid_len);
    if (err == ESP_OK) {
        err = nvs_get_str(handle, "password", password, &password_len);
    }
    nvs_close(handle);
    return err;
}

static esp_err_t save_wifi_credentials(const char *ssid, const char *password)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open("wifi_cfg", NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;

    err = nvs_set_str(handle, "ssid", ssid);
    if (err == ESP_OK) err = nvs_set_str(handle, "password", password);
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}

static void make_ap_credentials(void)
{
    uint8_t mac[6] = {0};
    if (esp_wifi_get_mac(WIFI_IF_STA, mac) != ESP_OK) {
        snprintf(s_ap_ssid, sizeof(s_ap_ssid), "SmartPlan-Setup");
        snprintf(s_ap_password, sizeof(s_ap_password), "setup-123456");
        return;
    }
    snprintf(s_ap_ssid, sizeof(s_ap_ssid), "SmartPlan-%02X%02X%02X",
             mac[3], mac[4], mac[5]);
    snprintf(s_ap_password, sizeof(s_ap_password), "setup-%02X%02X%02X%02X%02X%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

static esp_err_t configure_setup_ap(void)
{
    wifi_config_t ap_config = {0};
    size_t ssid_len = strnlen(s_ap_ssid, sizeof(ap_config.ap.ssid));
    memcpy(ap_config.ap.ssid, s_ap_ssid, ssid_len);
    snprintf((char *)ap_config.ap.password, sizeof(ap_config.ap.password), "%s", s_ap_password);
    ap_config.ap.ssid_len = ssid_len;
    ap_config.ap.channel = 1;
    ap_config.ap.max_connection = 4;
    ap_config.ap.authmode = WIFI_AUTH_WPA2_PSK;
    ap_config.ap.pmf_cfg.required = false;
    ap_config.ap.pmf_cfg.capable = true;

    esp_err_t err = esp_wifi_set_mode(WIFI_MODE_AP);
    if (err == ESP_OK) err = esp_wifi_set_config(WIFI_IF_AP, &ap_config);
    return err;
}

static esp_err_t root_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, s_setup_page, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t wifi_scan_get_handler(httpd_req_t *req)
{
    portal_status_t status = get_portal_status();
    if (status == PORTAL_STATUS_CONNECTING || status == PORTAL_STATUS_CONNECTED) {
        httpd_resp_set_status(req, "409 Conflict");
        return httpd_resp_sendstr(req, "Wi-Fi connection is already active");
    }

    esp_err_t err = esp_wifi_set_mode(WIFI_MODE_APSTA);
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Could not enable Wi-Fi scan mode");
        return ESP_FAIL;
    }

    wifi_scan_config_t scan_config = {0};
    err = esp_wifi_scan_start(&scan_config, true);
    uint16_t ap_count = 0;
    wifi_ap_record_t *records = NULL;
    if (err == ESP_OK) err = esp_wifi_scan_get_ap_num(&ap_count);
    if (err == ESP_OK && ap_count > 0) {
        if (ap_count > WIFI_SCAN_MAX_RESULTS) ap_count = WIFI_SCAN_MAX_RESULTS;
        records = calloc(ap_count, sizeof(*records));
        if (records == NULL) {
            esp_wifi_clear_ap_list();
            err = ESP_ERR_NO_MEM;
        } else {
            err = esp_wifi_scan_get_ap_records(&ap_count, records);
        }
    } else {
        esp_wifi_clear_ap_list();
    }

    esp_err_t restore_err = esp_wifi_set_mode(WIFI_MODE_AP);
    if (err == ESP_OK && restore_err != ESP_OK) err = restore_err;
    if (err != ESP_OK) {
        free(records);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Could not scan Wi-Fi networks");
        return ESP_FAIL;
    }

    cJSON *networks = cJSON_CreateArray();
    if (networks == NULL) {
        free(records);
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    for (uint16_t i = 0; i < ap_count; i++) {
        char ssid[WIFI_SSID_MAX_LEN + 1];
        memcpy(ssid, records[i].ssid, WIFI_SSID_MAX_LEN);
        ssid[WIFI_SSID_MAX_LEN] = '\0';
        if (ssid[0] == '\0') continue;

        bool duplicate = false;
        cJSON *existing;
        cJSON_ArrayForEach(existing, networks) {
            cJSON *existing_ssid = cJSON_GetObjectItemCaseSensitive(existing, "ssid");
            if (cJSON_IsString(existing_ssid) && strcmp(existing_ssid->valuestring, ssid) == 0) {
                duplicate = true;
                break;
            }
        }
        if (duplicate) continue;

        cJSON *network = cJSON_CreateObject();
        if (network == NULL ||
            cJSON_AddStringToObject(network, "ssid", ssid) == NULL ||
            cJSON_AddNumberToObject(network, "rssi", records[i].rssi) == NULL ||
            cJSON_AddBoolToObject(network, "secure", records[i].authmode != WIFI_AUTH_OPEN) == NULL) {
            cJSON_Delete(network);
            cJSON_Delete(networks);
            free(records);
            httpd_resp_send_500(req);
            return ESP_FAIL;
        }
        cJSON_AddItemToArray(networks, network);
    }

    free(records);
    char *response = cJSON_PrintUnformatted(networks);
    cJSON_Delete(networks);
    if (response == NULL) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    err = httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
    free(response);
    return err;
}

static esp_err_t status_get_handler(httpd_req_t *req)
{
    char response[48];
    snprintf(response, sizeof(response), "{\"status\":\"%s\"}",
             portal_status_name(get_portal_status()));
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    return httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t wifi_post_handler(httpd_req_t *req)
{
    if (req->content_len <= 0 || req->content_len > WIFI_PORTAL_BODY_MAX) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid request body");
        return ESP_FAIL;
    }

    char body[WIFI_PORTAL_BODY_MAX + 1];
    int received = 0;
    while (received < req->content_len) {
        int n = httpd_req_recv(req, body + received, req->content_len - received);
        if (n <= 0) {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Could not read request body");
            return ESP_FAIL;
        }
        received += n;
    }
    body[received] = '\0';

    cJSON *root = cJSON_Parse(body);
    cJSON *ssid_json = root ? cJSON_GetObjectItemCaseSensitive(root, "ssid") : NULL;
    cJSON *password_json = root ? cJSON_GetObjectItemCaseSensitive(root, "password") : NULL;
    if (!cJSON_IsString(ssid_json) || !cJSON_IsString(password_json) ||
        ssid_json->valuestring == NULL || password_json->valuestring == NULL) {
        if (root) cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "SSID and password are required");
        return ESP_FAIL;
    }

    size_t ssid_len = strlen(ssid_json->valuestring);
    size_t password_len = strlen(password_json->valuestring);
    if (ssid_len == 0 || ssid_len > WIFI_SSID_MAX_LEN ||
        password_len > WIFI_PASSWORD_MAX_LEN) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "SSID or password is too long");
        return ESP_FAIL;
    }

    taskENTER_CRITICAL(&s_state_lock);
    if (s_provisioning_pending ||
        s_portal_status == PORTAL_STATUS_CONNECTING ||
        s_portal_status == PORTAL_STATUS_CONNECTED) {
        taskEXIT_CRITICAL(&s_state_lock);
        cJSON_Delete(root);
        httpd_resp_set_status(req, "409 Conflict");
        httpd_resp_set_type(req, "text/plain");
        httpd_resp_sendstr(req, "A connection attempt is already active");
        return ESP_FAIL;
    }
    memcpy(s_pending_ssid, ssid_json->valuestring, ssid_len + 1);
    memcpy(s_pending_password, password_json->valuestring, password_len + 1);
    s_provisioning_pending = true;
    s_portal_status = PORTAL_STATUS_CONNECTING;
    s_force_ap = false;
    taskEXIT_CRITICAL(&s_state_lock);
    cJSON_Delete(root);

    wifi_config_t sta_config = {0};
    memcpy(sta_config.sta.ssid, s_pending_ssid, strlen(s_pending_ssid));
    memcpy(sta_config.sta.password, s_pending_password, strlen(s_pending_password));
    sta_config.sta.pmf_cfg.capable = true;
    sta_config.sta.pmf_cfg.required = false;

    // Enable the STA interface before applying its configuration.
    esp_err_t err = esp_wifi_set_mode(WIFI_MODE_APSTA);
    if (err == ESP_OK) err = esp_wifi_set_config(WIFI_IF_STA, &sta_config);
    if (err == ESP_OK) err = esp_wifi_connect();
    if (err != ESP_OK) {
        taskENTER_CRITICAL(&s_state_lock);
        s_provisioning_pending = false;
        s_force_ap = true;
        s_portal_status = PORTAL_STATUS_FAILED;
        taskEXIT_CRITICAL(&s_state_lock);
        esp_wifi_set_mode(WIFI_MODE_AP);
        ESP_LOGE(TAG, "Khong the bat dau ket noi Wi-Fi: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Could not start Wi-Fi connection");
        return ESP_FAIL;
    }

    httpd_resp_set_status(req, "202 Accepted");
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"status\":\"connecting\"}");
}

static esp_err_t start_web_server(void)
{
    if (s_http_server != NULL) return ESP_OK;

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 8;
    esp_err_t err = httpd_start(&s_http_server, &config);
    if (err != ESP_OK) return err;

    const httpd_uri_t root_uri = {
        .uri = "/", .method = HTTP_GET, .handler = root_get_handler,
    };
    const httpd_uri_t scan_uri = {
        .uri = "/scan", .method = HTTP_GET, .handler = wifi_scan_get_handler,
    };
    const httpd_uri_t wifi_uri = {
        .uri = "/wifi", .method = HTTP_POST, .handler = wifi_post_handler,
    };
    const httpd_uri_t status_uri = {
        .uri = "/status", .method = HTTP_GET, .handler = status_get_handler,
    };
    if ((err = httpd_register_uri_handler(s_http_server, &root_uri)) != ESP_OK ||
        (err = httpd_register_uri_handler(s_http_server, &scan_uri)) != ESP_OK ||
        (err = httpd_register_uri_handler(s_http_server, &wifi_uri)) != ESP_OK ||
        (err = httpd_register_uri_handler(s_http_server, &status_uri)) != ESP_OK) {
        httpd_stop(s_http_server);
        s_http_server = NULL;
        return err;
    }
    return ESP_OK;
}

static esp_err_t start_config_portal(void)
{
    if (wifi_is_connected()) return ESP_OK;

    taskENTER_CRITICAL(&s_state_lock);
    s_force_ap = true;
    taskEXIT_CRITICAL(&s_state_lock);

    esp_err_t err = configure_setup_ap();
    if (err == ESP_OK) err = start_web_server();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Khong khoi dong duoc portal: %s", esp_err_to_name(err));
        return err;
    }
    ESP_LOGW(TAG, "Wi-Fi setup: SSID '%s', password '%s', URL http://192.168.4.1",
             s_ap_ssid, s_ap_password);
    return ESP_OK;
}

static void sntp_time_sync_notification_cb(struct timeval *tv)
{
    (void)tv;
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
    if (esp_sntp_enabled()) esp_sntp_stop();
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_setservername(1, "time.google.com");
    esp_sntp_setservername(2, "time.cloudflare.com");
    esp_sntp_set_time_sync_notification_cb(sntp_time_sync_notification_cb);
    esp_sntp_set_sync_mode(SNTP_SYNC_MODE_IMMED);
    esp_sntp_init();

    for (int retry = 0; retry < 15; retry++) {
        if (!wifi_is_connected()) {
            ESP_LOGW(TAG, "Wi-Fi ngat ket noi trong luc doi SNTP");
            break;
        }
        if (s_time_synced || esp_sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED) {
            s_time_synced = true;
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    if (!s_time_synced) {
        ESP_LOGW(TAG, "NTP chua dong bo sau 15s; SNTP se tiep tuc thu o nen.");
    }
    vTaskDelete(NULL);
}

static void finish_provisioning_task(void *pvParameters)
{
    (void)pvParameters;
    char ssid[WIFI_SSID_MAX_LEN + 1];
    char password[WIFI_PASSWORD_MAX_LEN + 1];
    taskENTER_CRITICAL(&s_state_lock);
    memcpy(ssid, s_pending_ssid, sizeof(ssid));
    memcpy(password, s_pending_password, sizeof(password));
    taskEXIT_CRITICAL(&s_state_lock);

    esp_err_t err = save_wifi_credentials(ssid, password);
    if (err != ESP_OK) {
        set_portal_status(PORTAL_STATUS_SAVE_FAILED);
        ESP_LOGE(TAG, "Da ket noi nhung khong luu duoc Wi-Fi vao NVS: %s",
                 esp_err_to_name(err));
        vTaskDelete(NULL);
        return;
    }

    taskENTER_CRITICAL(&s_state_lock);
    s_have_saved_credentials = true;
    taskEXIT_CRITICAL(&s_state_lock);
    set_portal_status(PORTAL_STATUS_CONNECTED);
    ESP_LOGI(TAG, "Da ket noi va luu cau hinh Wi-Fi vao NVS");

    // Cho trang web nhan duoc trang thai thanh cong roi tat AP.
    vTaskDelay(pdMS_TO_TICKS(5000));
    if (s_http_server != NULL) {
        httpd_stop(s_http_server);
        s_http_server = NULL;
    }
    taskENTER_CRITICAL(&s_state_lock);
    s_force_ap = false;
    taskEXIT_CRITICAL(&s_state_lock);
    esp_wifi_set_mode(WIFI_MODE_STA);
    vTaskDelete(NULL);
}

static void wifi_connect_timeout_task(void *pvParameters)
{
    (void)pvParameters;
    vTaskDelay(pdMS_TO_TICKS(WIFI_CONNECT_TIMEOUT_MS));
    if (!wifi_is_connected() && s_have_saved_credentials) {
        ESP_LOGW(TAG, "Khong ket noi duoc Wi-Fi da luu; bat portal cau hinh");
        start_config_portal();
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

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    (void)arg;
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        if (s_connect_on_start) {
            s_connect_on_start = false;
            esp_wifi_connect();
        }
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        const wifi_event_sta_disconnected_t *disconnected =
            (const wifi_event_sta_disconnected_t *)event_data;
        if (disconnected != NULL) {
            ESP_LOGW(TAG, "Wi-Fi ngat ket noi: reason=%u, RSSI=%d dBm",
                     disconnected->reason, disconnected->rssi);
        }
        bool provisioning_pending;
        bool force_ap;
        taskENTER_CRITICAL(&s_state_lock);
        provisioning_pending = s_provisioning_pending;
        force_ap = s_force_ap;
        if (provisioning_pending) {
            s_provisioning_pending = false;
            s_force_ap = true;
            s_portal_status = PORTAL_STATUS_FAILED;
        }
        taskEXIT_CRITICAL(&s_state_lock);

        if (provisioning_pending) {
            ESP_LOGW(TAG, "Wi-Fi vua nhap khong ket noi duoc; tiep tuc mo portal");
            esp_wifi_set_mode(WIFI_MODE_AP);
        } else if (!force_ap) {
            ESP_LOGW(TAG, "Mat Wi-Fi, dang thu ket noi lai...");
            esp_wifi_connect();
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *event = (const ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Da ket noi Wi-Fi, IP: " IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);

        bool provisioning_pending;
        taskENTER_CRITICAL(&s_state_lock);
        provisioning_pending = s_provisioning_pending;
        if (provisioning_pending) s_provisioning_pending = false;
        taskEXIT_CRITICAL(&s_state_lock);
        if (provisioning_pending) {
            if (xTaskCreate(finish_provisioning_task, "wifi_save", 4096, NULL, 5, NULL) != pdPASS) {
                set_portal_status(PORTAL_STATUS_SAVE_FAILED);
                ESP_LOGE(TAG, "Khong tao duoc task luu cau hinh Wi-Fi");
            }
        }
        xTaskCreate(sntp_sync_task, "sntp_task", 3072, NULL, 3, NULL);
    }
}

static esp_err_t init_network_interfaces(void)
{
    esp_err_t err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;

    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;

    if (esp_netif_create_default_wifi_sta() == NULL ||
        esp_netif_create_default_wifi_ap() == NULL) return ESP_FAIL;
    return ESP_OK;
}

esp_err_t wifi_init(void)
{
    if (s_wifi_event_group != NULL) return ESP_OK;

    s_wifi_event_group = xEventGroupCreate();
    if (s_wifi_event_group == NULL) return ESP_ERR_NO_MEM;

    esp_err_t err = init_network_interfaces();
    if (err != ESP_OK) return err;
    err = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                              wifi_event_handler, NULL,
                                              &s_wifi_handler_instance);
    if (err != ESP_OK) return err;
    err = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                              wifi_event_handler, NULL,
                                              &s_ip_handler_instance);
    if (err != ESP_OK) return err;

    wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&init_config);
    if (err != ESP_OK) return err;
    err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (err != ESP_OK) return err;
    make_ap_credentials();

    char ssid[WIFI_SSID_MAX_LEN + 1] = {0};
    char password[WIFI_PASSWORD_MAX_LEN + 1] = {0};
    err = load_wifi_credentials(ssid, sizeof(ssid), password, sizeof(password));
    if (err == ESP_OK && ssid[0] != '\0') {
        wifi_config_t sta_config = {0};
        memcpy(sta_config.sta.ssid, ssid, strlen(ssid));
        memcpy(sta_config.sta.password, password, strlen(password));
        sta_config.sta.pmf_cfg.capable = true;
        sta_config.sta.pmf_cfg.required = false;
        err = esp_wifi_set_mode(WIFI_MODE_STA);
        if (err == ESP_OK) err = esp_wifi_set_config(WIFI_IF_STA, &sta_config);
        if (err != ESP_OK) return err;

        s_have_saved_credentials = true;
        s_connect_on_start = true;
        err = esp_wifi_start();
        if (err != ESP_OK) return err;
        ESP_LOGI(TAG, "Dang ket noi Wi-Fi da luu '%s'", ssid);
        if (xTaskCreate(wifi_connect_timeout_task, "wifi_fallback", 3072, NULL, 3, NULL) != pdPASS) {
            ESP_LOGW(TAG, "Khong tao duoc task fallback portal");
        }
        return ESP_OK;
    }

    err = configure_setup_ap();
    if (err == ESP_OK) err = esp_wifi_start();
    if (err == ESP_OK) err = start_web_server();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Khong khoi dong duoc Wi-Fi setup: %s", esp_err_to_name(err));
        return err;
    }
    ESP_LOGW(TAG, "Chua co Wi-Fi. Ket noi AP '%s', mat khau '%s', mo http://192.168.4.1",
             s_ap_ssid, s_ap_password);
    return ESP_OK;
}
