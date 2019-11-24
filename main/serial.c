#include "serial.h"

#include "utils.h"
#include "loki.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"

#include "driver/uart.h"

#include <string.h>

static const char *TAG = "serail";
const int uart_buffer_size = (RD_BUF_SIZE * 2);

QueueHandle_t uart_queue;

static void uart_event_task(void *pvParameters) {
  uart_event_t event;
  uint8_t* dtmp = (uint8_t*) malloc(RD_BUF_SIZE + 1);
  char* ctmp = (char*) malloc(RD_BUF_SIZE + 1);
  log_data_t out_line;
  for(;;) {
    //Waiting for UART event.
    if(xQueueReceive(uart_queue, (void *)&event, (portTickType)portMAX_DELAY)) {
      bzero(dtmp, RD_BUF_SIZE);
      switch(event.type) {
          //Event of UART receving data
          /*We'd better handler data event fast, there would be much more data events than
            other types of events. If we take too much time on data event, the queue might
            be full.*/
          case UART_DATA:
            ESP_LOGI(TAG, "[UART DATA]: %d", event.size);
            uart_read_bytes(EX_UART_NUM, dtmp, event.size, portMAX_DELAY);
            memset(&out_line, 0, sizeof(log_data_t));
            gettimeofday(&out_line.tv, NULL);
            memset(ctmp, 0, RD_BUF_SIZE + 1);
            remove_vt100(event.size, (char *)dtmp, LOG_LINE_SIZE, ctmp);

            char delim[] = "\r\n";
            char *ptr = strtok(ctmp, delim);
            while(ptr != NULL) {
              if (strlen(ptr)) {
                strcpy(out_line.log_line, ptr);
                if (strchr("VDIWE", ptr[0]) && ptr[1] == ' ' && ptr[2] == '(') {
                  strcpy(out_line.labels[0], "level");
                  if (ptr[0] == 'E') strcpy(out_line.labels[0 + LABELS_NUM], "error");
                  else if (ptr[0] == 'W') strcpy(out_line.labels[0 + LABELS_NUM], "warning");
                  else if (ptr[0] == 'I') strcpy(out_line.labels[0 + LABELS_NUM], "info");
                  else if (ptr[0] == 'D') strcpy(out_line.labels[0 + LABELS_NUM], "debug");
                  else if (ptr[0] == 'V') strcpy(out_line.labels[0 + LABELS_NUM], "verbose");
                }
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

void init_serial() {
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

  xTaskCreate(uart_event_task, "uart_event_task", 4096, NULL, 12, NULL);
}
