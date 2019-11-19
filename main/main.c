#include <stdio.h>
#include <string.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event_loop.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "lwip/apps/sntp.h"
#include "driver/uart.h"

#include "loki.h"
#include "utils.h"

#define RD_BUF_SIZE 1024
#define EX_UART_NUM UART_NUM_2
#define UART_RX_PIN 22

#define ESP_WIFI_SSID "SSID"
#define ESP_WIFI_PASS "passphrase"
#define ESP_MAXIMUM_RETRY 13

static EventGroupHandle_t wifi_event_group;
QueueHandle_t uart_queue;

static const char *TAG = "uart-loki-esp32";
const int uart_buffer_size = (RD_BUF_SIZE * 2);
const int CONNECTED_BIT = BIT0;

static int s_retry_num = 0;

static esp_err_t event_handler(void *ctx, system_event_t *event) {
  switch(event->event_id) {
    case SYSTEM_EVENT_STA_START:
      esp_wifi_connect();
      break;
    case SYSTEM_EVENT_STA_GOT_IP:
      ESP_LOGI(TAG, "got ip:%s", ip4addr_ntoa(&event->event_info.got_ip.ip_info.ip));
      s_retry_num = 0;
      xEventGroupSetBits(wifi_event_group, CONNECTED_BIT);
      break;
    case SYSTEM_EVENT_STA_DISCONNECTED: {
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

void wifi_init_sta() {
  wifi_event_group = xEventGroupCreate();

  tcpip_adapter_init();
  ESP_ERROR_CHECK(esp_event_loop_init(event_handler, NULL) );

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));
  wifi_config_t wifi_config = {
    .sta = {
      .ssid = ESP_WIFI_SSID,
      .password = ESP_WIFI_PASS
    },
  };

  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
  ESP_ERROR_CHECK(esp_wifi_start());

  ESP_LOGI(TAG, "wifi_init_sta finished.");
  ESP_LOGI(TAG, "connect to ap SSID:%s", ESP_WIFI_SSID);
}

static void uart_event_task(void *pvParameters) {
  uart_event_t event;
  uint8_t* dtmp = (uint8_t*) malloc(RD_BUF_SIZE + 1);
  char* ctmp = (char*) malloc(RD_BUF_SIZE + 1);
  log_data_t out_line;
  for(;;) {
    //Waiting for UART event.
    if(xQueueReceive(uart_queue, (void *)&event, (portTickType)portMAX_DELAY)) {
      bzero(dtmp, RD_BUF_SIZE);
      ESP_LOGI(TAG, "uart[%d] event:", EX_UART_NUM);
      switch(event.type) {
          //Event of UART receving data
          /*We'd better handler data event fast, there would be much more data events than
            other types of events. If we take too much time on data event, the queue might
            be full.*/
          case UART_DATA:
            ESP_LOGI(TAG, "[UART DATA]: %d", event.size);
            uart_read_bytes(EX_UART_NUM, dtmp, event.size, portMAX_DELAY);
            memset(&out_line, 0, sizeof(log_data_t));
            memset(ctmp, 0, RD_BUF_SIZE + 1);
            remove_vt100(event.size, (char *)dtmp, LOG_LINE_SIZE, ctmp);
            gettimeofday(&out_line.tv, NULL);

            char delim[] = "\r\n";
            char *ptr = strtok(ctmp, delim);
            while(ptr != NULL) {
              if (strlen(ptr)) {
                strcpy(out_line.log_line, ptr);
                xQueueSendToBack(data0_queue, &out_line, 0);
              }
              ptr = strtok(NULL, delim);
            }
            break;
          //Event of HW FIFO overflow detected
          case UART_FIFO_OVF:
            ESP_LOGI(TAG, "hw fifo overflow");
            // If fifo overflow happened, you should consider adding flow control for your application.
            // The ISR has already reset the rx FIFO,
            // As an example, we directly flush the rx buffer here in order to read more data.
            uart_flush_input(EX_UART_NUM);
            xQueueReset(uart_queue);
            break;
          //Event of UART ring buffer full
          case UART_BUFFER_FULL:
            ESP_LOGI(TAG, "ring buffer full");
            // If buffer full happened, you should consider encreasing your buffer size
            // As an example, we directly flush the rx buffer here in order to read more data.
            uart_flush_input(EX_UART_NUM);
            xQueueReset(uart_queue);
            break;
          //Event of UART RX break detected
          case UART_BREAK:
            ESP_LOGI(TAG, "uart rx break");
            break;
          //Event of UART parity check error
          case UART_PARITY_ERR:
            ESP_LOGI(TAG, "uart parity error");
            break;
          //Event of UART frame error
          case UART_FRAME_ERR:
            ESP_LOGI(TAG, "uart frame error");
            break;
          //UART_PATTERN_DET
          default:
            ESP_LOGI(TAG, "uart event type: %d", event.type);
            break;
      }
    }
  }
  free(dtmp);
  dtmp = NULL;
  vTaskDelete(NULL);
}

void app_main() {
  const int retry_count = 10;
  time_t now = 0;
  struct tm timeinfo = { 0 };
  int retry = 0;
  
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);
  // Connect to wifi
  wifi_init_sta();
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
  // configure UART
  uart_config_t uart_config = {
    .baud_rate = 115200,
    .data_bits = UART_DATA_8_BITS,
    .parity = UART_PARITY_DISABLE,
    .stop_bits = UART_STOP_BITS_1,
    .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
  };
  ESP_ERROR_CHECK(uart_param_config(EX_UART_NUM, &uart_config));
  ESP_ERROR_CHECK(uart_set_pin(EX_UART_NUM, UART_PIN_NO_CHANGE, UART_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
  ESP_ERROR_CHECK(uart_driver_install(EX_UART_NUM, uart_buffer_size, 0, 10, &uart_queue, 0));
  init_loki();
  xTaskCreate(uart_event_task, "uart_event_task", 4096, NULL, 12, NULL);
  // run task for posting messages to Loki
}
