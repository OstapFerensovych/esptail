#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs_flash.h"

#include "store.h"

static const char *TAG = "store";
static const char wireless_nvs_namespace[] = "espwifi";
static const char store_nvs_namespace[] = "esptail_store";

char sta_ssid[32] = "";
char sta_password[64] = "";
SemaphoreHandle_t store_mutex = NULL;
loki_cfg_t _curr_config = { .url="", .username="", .password="" };

bool lock_store(TickType_t xTicksToWait) {
  if (!store_mutex) store_mutex = xSemaphoreCreateMutex();
  if (xSemaphoreTake(store_mutex, xTicksToWait) == pdTRUE) return true;
  else return false;
}

void unlock_store() {
  if (!store_mutex) store_mutex = xSemaphoreCreateMutex();
  if (store_mutex) xSemaphoreGive(store_mutex);
}

esp_err_t wifi_save_settings() {
  bool diff_found = false;
  nvs_handle handle;
  esp_err_t esp_err;

  esp_err = nvs_open(wireless_nvs_namespace, NVS_READWRITE, &handle);
  if (esp_err != ESP_OK) return esp_err;

  // Check if saved settings is different
  size_t sz = 128;
  uint8_t *buff = (uint8_t*)malloc(sizeof(uint8_t) * sz);
  memset(buff, 0x00, sizeof(sz));
  //- sta_ssid
  sz = sizeof(sta_ssid);
  esp_err = nvs_get_blob(handle, "sta_ssid", buff, &sz);
  if (esp_err == ESP_OK) {
    if (bcmp(sta_ssid, buff, sz)) diff_found = true;
  } else diff_found = true;
  //- sta_password
  if (!diff_found) {
    sz = sizeof(sta_password);
    esp_err = nvs_get_blob(handle, "sta_password", buff, &sz);
    if (esp_err == ESP_OK) {
      if (bcmp(sta_password, buff, sz)) diff_found = true;
    } else diff_found = true;
  }
  free(buff);

  // Save settings if needed
  if (diff_found) {
    esp_err = nvs_set_blob(handle, "sta_ssid", &sta_ssid, sizeof(sta_ssid));
    if (esp_err == ESP_OK) {
      esp_err = nvs_set_blob(handle, "sta_password", &sta_password, sizeof(sta_password));
    }
    if (esp_err == ESP_OK) {
      esp_err = nvs_commit(handle);
    }
    ESP_LOGI(TAG, "saving settings to NVS");
  } else {
    ESP_LOGV(TAG, "no difference in settings, skipping saving to NVS");
  }
  nvs_close(handle);

  return esp_err;
}

esp_err_t wifi_load_settings() {
  nvs_handle handle;
  esp_err_t esp_err;

  esp_err = nvs_open(wireless_nvs_namespace, NVS_READONLY, &handle);
  if (esp_err != ESP_OK) return esp_err;

  size_t sz = 128;
  uint8_t *buff = (uint8_t*)malloc(sizeof(uint8_t) * sz);
  memset(buff, 0x00, sizeof(sz));
  // sta_ssid
  sz = sizeof(sta_ssid);
  esp_err = nvs_get_blob(handle, "sta_ssid", buff, &sz);
  if (esp_err == ESP_OK) {
    memcpy(&sta_ssid, buff, sz);
    ESP_LOGV(TAG, "sta_ssid loaded '%s'", sta_ssid);
  }

  // sta_password
  if (esp_err == ESP_OK) {
    sz = sizeof(sta_password);
    esp_err = nvs_get_blob(handle, "sta_password", buff, &sz);
    if (esp_err == ESP_OK) {
      memcpy(&sta_password, buff, sz);
      ESP_LOGV(TAG, "sta_password loaded '%s'", sta_password);
    }
  }

  free(buff);
  nvs_close(handle);

  if (esp_err != ESP_OK) {
    ESP_LOGD(TAG, "settings load failed");
  } else {
    ESP_LOGD(TAG, "settings loaded successfully");
  }

  return esp_err;
}

esp_err_t _loki_config_save() {
  nvs_handle handle;
  esp_err_t esp_err;

  esp_err = nvs_open(store_nvs_namespace, NVS_READWRITE, &handle);
  if (esp_err != ESP_OK) return esp_err;

  esp_err = nvs_set_blob(handle, "loki_cfg", &_curr_config, sizeof(_curr_config));
  if (esp_err == ESP_OK) {
    esp_err = nvs_commit(handle);
  }

  nvs_close(handle);

  return esp_err;
}

esp_err_t _loki_config_load() {
  nvs_handle handle;
  esp_err_t esp_err;

  esp_err = nvs_open(store_nvs_namespace, NVS_READONLY, &handle);
  if (esp_err != ESP_OK) return esp_err;

  size_t sz = sizeof(_curr_config);
  if (esp_err == ESP_OK) {
    esp_err = nvs_get_blob(handle, "loki_cfg", &_curr_config, &sz);
  }

  nvs_close(handle);

  if (esp_err != ESP_OK) {
    ESP_LOGD(TAG, "Loki config load failed");
  } else {
    ESP_LOGD(TAG, "Loki config loaded successfully");
  }

  return esp_err;
}

loki_cfg_t get_loki_config() {
  loki_cfg_t _config = { .url="", .username="", .password="" };
  size_t sz = sizeof(loki_cfg_t);
  if (lock_store(portMAX_DELAY)) {
    memcpy(&_config, &_curr_config, sz);
    unlock_store();
  } else {
    ESP_LOGW(TAG, "Data access timeout or mutex not created.");
  }
  return _config;
}

esp_err_t set_loki_config(loki_cfg_t config) {
  esp_err_t esp_err;
  size_t sz = sizeof(loki_cfg_t);

  if (lock_store(portMAX_DELAY)) {
    memcpy(&_curr_config, &config, sz);
    esp_err = _loki_config_save();
    unlock_store();
  } else {
    ESP_LOGW(TAG, "Data access timeout or mutex not created.");
    return ESP_FAIL;
  }

  if (esp_err != ESP_OK) ESP_LOGW(TAG, "Failed to save loki configuration.");

  return esp_err;
}

esp_err_t reset_store() {
  return nvs_flash_erase();
}

esp_err_t store_init() {
  esp_err_t esp_err;
  esp_err = _loki_config_load();
  if (esp_err != ESP_OK) return esp_err;
  return ESP_OK;
}
