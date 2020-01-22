#ifndef STORE_H_INCLUDED
#define STORE_H_INCLUDED

#include "freertos/FreeRTOS.h"

#include "esp_http_client.h"

typedef struct loki_cfg {
  esp_http_client_transport_t transport;
  char host[512];
  int port;
  char username[64];
  char password[64];
  char name[128];
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
