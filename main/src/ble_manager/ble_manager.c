#include "ble_manager.h"
#include "esp_log.h"
#include "esp_bt.h"           // Thêm thư viện Bluetooth Controller
#include "esp_bt_main.h"      // Thêm thư viện Bluetooth Host (Bluedroid)
#include "esp_ble_mesh_defs.h"
#include "esp_ble_mesh_common_api.h"
#include "esp_ble_mesh_networking_api.h"
#include "esp_ble_mesh_provisioning_api.h"
#include "esp_ble_mesh_config_model_api.h"
#include "nvs_flash.h"
#include <string.h>

static const char *TAG = "BLE_MESH_MANAGER";

static uint8_t dev_uuid[16] = { 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37 };

static esp_ble_mesh_prov_t provision = {
    .uuid = dev_uuid,
};

static esp_ble_mesh_cfg_srv_t config_server = {
    .relay = ESP_BLE_MESH_RELAY_ENABLED,
    .beacon = ESP_BLE_MESH_BEACON_ENABLED,
    .default_ttl = 7,
};

#define VENDOR_COMPANY_ID   0x02E5
#define VENDOR_MODEL_ID_SRV 0x0000
#define ESP_BLE_MESH_VEND_MODEL_OP_SEND ESP_BLE_MESH_MODEL_OP_3(0x01, VENDOR_COMPANY_ID)

static esp_ble_mesh_model_t vnd_models[] = {
    ESP_BLE_MESH_VENDOR_MODEL(VENDOR_COMPANY_ID, VENDOR_MODEL_ID_SRV, NULL, NULL, NULL),
};

static esp_ble_mesh_model_t root_models[] = {
    ESP_BLE_MESH_MODEL_CFG_SRV(&config_server),
};

static esp_ble_mesh_elem_t elements[] = {
    ESP_BLE_MESH_ELEMENT(0, root_models, vnd_models),
};

static esp_ble_mesh_comp_t composition = {
    .cid = VENDOR_COMPANY_ID,
    .element_count = ARRAY_SIZE(elements),
    .elements = elements,
};

static void prov_complete(uint16_t net_idx, uint16_t addr, uint8_t flags, uint32_t iv_index) {
    ESP_LOGI(TAG, "Provisioning hoan tat! NetKey Index: 0x%04x, Unicast Address: 0x%04x", net_idx, addr);
}

static void ble_mesh_provisioning_cb(esp_ble_mesh_prov_cb_event_t event, esp_ble_mesh_prov_cb_param_t *param) {
    switch (event) {
        case ESP_BLE_MESH_PROV_REGISTER_COMP_EVT:
            ESP_LOGI(TAG, "Khoi tao Provisioning, err_code: %d", param->prov_register_comp.err_code);
            break;
        case ESP_BLE_MESH_NODE_PROV_COMPLETE_EVT:
            prov_complete(param->node_prov_complete.net_idx, param->node_prov_complete.addr,
                          param->node_prov_complete.flags, param->node_prov_complete.iv_index);
            break;
        default:
            break;
    }
}

void ble_app_init(void) {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    // 1. Giải phóng RAM từ Bluetooth Classic (chỉ dùng BLE)
    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT));

    // 2. Khởi tạo và bật Bluetooth Controller
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    err = esp_bt_controller_init(&bt_cfg);
    if (err) { ESP_LOGE(TAG, "Loi init BT Controller: %d", err); return; }
    err = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    if (err) { ESP_LOGE(TAG, "Loi enable BT Controller: %d", err); return; }

    // 3. Khởi tạo và bật Bluedroid Host
    err = esp_bluedroid_init();
    if (err) { ESP_LOGE(TAG, "Loi init Bluedroid: %d", err); return; }
    err = esp_bluedroid_enable();
    if (err) { ESP_LOGE(TAG, "Loi enable Bluedroid: %d", err); return; }

    // 4. Bắt đầu khởi tạo BLE Mesh
    esp_ble_mesh_register_prov_callback(ble_mesh_provisioning_cb);
    err = esp_ble_mesh_init(&provision, &composition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Loi khoi tao Bluetooth Mesh (err %d)", err);
        return;
    }

    esp_ble_mesh_node_prov_enable(ESP_BLE_MESH_PROV_ADV | ESP_BLE_MESH_PROV_GATT);
    ESP_LOGI(TAG, "BLE Mesh Node dang phat tin hieu, san sang ket noi vao Raspberry Pi!");
}

void ble_update_sensor_data(const char *data_str) {
    esp_ble_mesh_server_model_send_msg(&vnd_models[0], NULL, ESP_BLE_MESH_VEND_MODEL_OP_SEND, strlen(data_str), (uint8_t *)data_str);
}