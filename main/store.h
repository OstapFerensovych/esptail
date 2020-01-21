#ifndef STORE_H_INCLUDED
#define STORE_H_INCLUDED

#include "freertos/FreeRTOS.h"

typedef struct loki_cfg {
  char url[512];
  char username[64];
  char password[64];
} loki_cfg_t;

extern char sta_ssid[32];
extern char sta_password[64];

esp_err_t wifi_save_settings();
esp_err_t wifi_load_settings();
loki_cfg_t get_loki_config();
esp_err_t set_loki_config(loki_cfg_t config);
esp_err_t reset_store();
esp_err_t store_init();

#endif /* STORE_H_INCLUDED */
