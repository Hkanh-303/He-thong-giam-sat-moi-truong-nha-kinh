#ifndef WIFI_SENDER_H
#define WIFI_SENDER_H

#include "esp_err.h"

esp_err_t wifi_sender_init(void);
esp_err_t wifi_sender_post_json(const char *json_payload);

#endif
