#include "loki.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_http_client.h"
#include "esp_log.h"

#include <string.h>

static const char *TAG = "loki";
const TickType_t xTicksToWait = pdMS_TO_TICKS(100);
static log_data_t in_frame;
static char post_buff[JSON_BUFF_SIZE];
static char entry_buff[ENTRY_BUFF_SIZE];

QueueHandle_t data0_queue;

esp_err_t _http_event_handle(esp_http_client_event_t *evt) {
  switch(evt->event_id) {
    case HTTP_EVENT_ERROR:
      ESP_LOGE(TAG, "HTTP_EVENT_ERROR");
      break;
    case HTTP_EVENT_ON_CONNECTED:
      ESP_LOGI(TAG, "HTTP_EVENT_ON_CONNECTED");
      break;
    case HTTP_EVENT_HEADER_SENT:
      ESP_LOGD(TAG, "HTTP_EVENT_HEADER_SENT");
      break;
    case HTTP_EVENT_ON_HEADER:
      ESP_LOGD(TAG, "HTTP_EVENT_ON_HEADER");
      printf("%.*s", evt->data_len, (char*)evt->data);
      break;
    case HTTP_EVENT_ON_DATA:
      ESP_LOGD(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
      break;
    case HTTP_EVENT_ON_FINISH:
      ESP_LOGI(TAG, "HTTP_EVENT_ON_FINISH");
      break;
    case HTTP_EVENT_DISCONNECTED:
      ESP_LOGI(TAG, "HTTP_EVENT_DISCONNECTED");
      break;
  }
  return ESP_OK;
}

void send_data_task(void *arg) {
  int len = 0;
  BaseType_t xStatus;
  while(1){
    xStatus = xQueueReceive(data0_queue, &in_frame, xTicksToWait);
    if(xStatus == pdPASS) {
      // -- Make POST body
      // {
      //   "streams": [
      //     {
      //       "stream": {
      //         "label": "value"
      //       },
      //       "values": [
      //           [ "<unix epoch in nanoseconds>", "<log line>" ],
      //           [ "<unix epoch in nanoseconds>", "<log line>" ]
      //       ]
      //     }
      //   ]
      // }
      ESP_LOGI(TAG, "Preparing loki msg");
      sprintf(post_buff, "{\"streams\": [{\"stream\": {\"emitter\": \"" EMITTER_LABEL "\", \"job\": \"" JOB_LABEL "\"");
      for (int i = 0; i < LABELS_NUM; i++) {
        if (in_frame.labels[i][0] != '\0') {
          sprintf(entry_buff, ", \"%s\": \"%s\"", in_frame.labels[i], in_frame.labels[i + LABELS_NUM]);
          strcat(post_buff, entry_buff);
        }
      }
      strcat(post_buff, "}, \"values\":[[");
      sprintf(entry_buff, "\"%ld%ld000\", \"", in_frame.tv.tv_sec, in_frame.tv.tv_usec);
      strcat(post_buff, entry_buff);
      strcat(post_buff, in_frame.log_line);
      strcat(post_buff, "\"]]}]}");
      ESP_LOGV(TAG, "POST body: %s", post_buff);
      esp_http_client_config_t config = {
        .url = SERVER_URL,
        .event_handler = _http_event_handle,
        .method = HTTP_METHOD_POST,
      };
      esp_http_client_handle_t client = esp_http_client_init(&config);
      esp_http_client_set_header(client, "Content-Type", "application/json");
      len = strlen(post_buff);
      esp_http_client_open(client, len);
      esp_http_client_write(client, post_buff, len);
      len = esp_http_client_fetch_headers(client);
      ESP_LOGI(TAG, "Status = %d, content_length = %d",
           esp_http_client_get_status_code(client),
           esp_http_client_get_content_length(client));
      esp_http_client_cleanup(client);
    }
  }
}

void init_loki() {
  data0_queue = xQueueCreate(15, sizeof(log_data_t));
  xTaskCreate(send_data_task, "send_data_task", 8192, NULL, 10, NULL);
}
