#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "esp_wifi.h"
#include "esp_event_loop.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "lwip/apps/sntp.h"

#include "loki.h"
#include "serial.h"
#include "webconfig.h"

#define ESP_WIFI_SSID "SSID"
#define ESP_WIFI_PASS "passphrase"
#define ESP_MAXIMUM_RETRY 13
#define ESP_SCAN_LIST_SIZE 10 //MAX 20
#define ESP_AP_SSID "ESP-TAIL"
#define ESP_AP_PASS "esp-tail"
#define ESP_MAX_STA 1

static EventGroupHandle_t wifi_event_group;

static const char *TAG = "esptail";
const int CONNECTED_BIT = BIT0;

static int s_retry_num = 0;

static esp_err_t event_handler(void *ctx, system_event_t *event) {
  httpd_handle_t *server = (httpd_handle_t *) ctx;

  switch(event->event_id) {
    case SYSTEM_EVENT_STA_START:
      esp_wifi_connect();
      break;
    case SYSTEM_EVENT_STA_GOT_IP:
      ESP_LOGI(TAG, "got ip:%s", ip4addr_ntoa(&event->event_info.got_ip.ip_info.ip));
      s_retry_num = 0;
      if (*server == NULL) {
          *server = start_webserver();
      }
      xEventGroupSetBits(wifi_event_group, CONNECTED_BIT);
      break;
    case SYSTEM_EVENT_STA_DISCONNECTED: {
      if (*server) {
          stop_webserver(*server);
          *server = NULL;
      }
      if (s_retry_num < ESP_MAXIMUM_RETRY) {
        esp_wifi_connect();
        xEventGroupClearBits(wifi_event_group, CONNECTED_BIT);
        s_retry_num++;
        ESP_LOGI(TAG,"retry to connect to the AP");
      }
      ESP_LOGI(TAG,"connect to the AP fail\n");
      break;
    }
    default:
        break;
  }
  return ESP_OK;
}

void wifi_init_ap_sta(void *arg) {
  wifi_event_group = xEventGroupCreate();

  tcpip_adapter_init();
  ESP_ERROR_CHECK(esp_event_loop_init(event_handler, arg) );

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));

  wifi_config_t wifi_sta_config = {
    .sta = {
      .ssid = ESP_WIFI_SSID,
      .password = ESP_WIFI_PASS
    },
  };

  wifi_config_t wifi_ap_config = {
    .ap = {
      .ssid = ESP_AP_SSID,
      .ssid_len = strlen(ESP_AP_SSID),
      .password = ESP_AP_PASS,
      .max_connection = ESP_MAX_STA,
      .authmode = WIFI_AUTH_WPA_WPA2_PSK
    },
  };

  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
  ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_AP, &wifi_ap_config));
  ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_sta_config));
  ESP_ERROR_CHECK(esp_wifi_start());

  ESP_LOGI(TAG, "wifi_init_sta finished.");
  ESP_LOGI(TAG, "connect to ap SSID:%s", ESP_WIFI_SSID);
}

void app_main() {
  const int retry_count = 10;
  time_t now = 0;
  struct tm timeinfo = { 0 };
  int retry = 0;
  static httpd_handle_t server = NULL;
  
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);
  // Connect to wifi
  wifi_init_ap_sta(&server);
  xEventGroupWaitBits(wifi_event_group, CONNECTED_BIT, false, true, portMAX_DELAY);
  // Obtain time
  ESP_LOGI(TAG, "Initializing SNTP");
  sntp_setoperatingmode(SNTP_OPMODE_POLL);
  sntp_setservername(0, "pool.ntp.org");
  sntp_init();
  
  while(timeinfo.tm_year < (2016 - 1900) && ++retry < retry_count) {
    ESP_LOGI(TAG, "Waiting for system time to be set... (%d/%d)", retry, retry_count);
    vTaskDelay(2000 / portTICK_PERIOD_MS);
    time(&now);
    localtime_r(&now, &timeinfo);
  }

  init_loki();
  init_serial();
}
