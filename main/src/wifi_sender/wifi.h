#ifndef SMARTPLAN_WIFI_H
#define SMARTPLAN_WIFI_H

#include <stdbool.h>
#include "esp_err.h"

esp_err_t wifi_init(void);
bool wifi_is_connected(void);
bool wifi_is_time_synced(void);

#endif // SMARTPLAN_WIFI_H
