#pragma once
// #ifdef USE_ESP_IDF

#include <stdarg.h>

#include "esphome/core/component.h"
#include "esphome/components/sensor/sensor.h"

#include "freertos/FreeRTOS.h"
#include "freertos/ringbuf.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_tls.h"
#include "esp_err.h"
#include "esp_http_client.h"

#include "string.h"
#include "cJSON.h"
static const char *TAG = "HTTP_CLIENT";

#define LOCAL_BUFFER_SIZE 1024

RingbufHandle_t request_buf_handle = NULL;
RingbufHandle_t result_buf_handle = NULL;
TaskHandle_t myTaskHandle = NULL;
QueueHandle_t request_queue = NULL;

static void http_client_task(void *pv);

esp_err_t _http_event_handler(esp_http_client_event_t *evt)
{
  static int output_len; // Stores number of bytes read
  switch (evt->event_id)
  {
  case HTTP_EVENT_ERROR:
    ESP_LOGD(TAG, "HTTP_EVENT_ERROR");
    break;
  case HTTP_EVENT_ON_CONNECTED:
    ESP_LOGD(TAG, "HTTP_EVENT_ON_CONNECTED");
    break;
  case HTTP_EVENT_HEADER_SENT:
    ESP_LOGD(TAG, "HTTP_EVENT_HEADER_SENT");
    break;
  case HTTP_EVENT_ON_HEADER:
    ESP_LOGD(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
    break;
  case HTTP_EVENT_ON_DATA:
    ESP_LOGD(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
    /*
     *  Check for chunked encoding is added as the URL for chunked encoding used in this example returns binary data.
     *  However, event handler can also be used in case chunked encoding is used.
     */
    if (evt->user_data)
    {
      int free_space = LOCAL_BUFFER_SIZE - output_len;
      if (free_space >= evt->data_len)
      {
        memcpy((char *)evt->user_data + output_len, evt->data, evt->data_len);
        output_len += evt->data_len;
      }
      else // заполняем буфер полностью
      {
        if (free_space > 1)
        {
          memcpy((char *)evt->user_data + output_len, evt->data, free_space);
          output_len += free_space;
        }
      }
    }
    break;
  case HTTP_EVENT_ON_FINISH:
    ESP_LOGD(TAG, "HTTP_EVENT_ON_FINISH");
    output_len = 0;
    break;
  case HTTP_EVENT_DISCONNECTED:
    ESP_LOGI(TAG, "HTTP_EVENT_DISCONNECTED");
    int mbedtls_err = 0;
    esp_err_t err = esp_tls_get_and_clear_last_error((esp_tls_error_handle_t)evt->data, &mbedtls_err, NULL);
    if (err != 0)
    {
      ESP_LOGD(TAG, "Last esp error code: 0x%x", err);
      ESP_LOGD(TAG, "Last mbedtls failure: 0x%x", mbedtls_err);
    }
    output_len = 0;
    break;
  }
  return ESP_OK;
}

const char *dht = " {\"StatusSNS\": {         \"Time\": \"2018.02.01 22:48:39\",        \"DHT11\": {          \"Temperature\": 12.0,          \"Humidity\": 42.0        },        \"TempUnit\": \"C\"      }}fghffggf{{{{dfsdfd";

class HTTPSensor : public PollingComponent, public Sensor
{
public:
  Sensor *sensors_array[10];
  int sensors_count;
  cJSON *sensors_path;
  const char *url;

  // constructor
  HTTPSensor(const char *_url, const char *sensors_map) : PollingComponent(60000)
  {
    url = _url;

    sensors_count = 0;
    sensors_path = cJSON_CreateArray();

    char *sensors_map_copy = strdup(sensors_map);

    char *sensor, *sensor_p;
    char *rest = sensors_map_copy;
    while ((sensor = strtok_r(rest, ";", &rest)))
    {
      cJSON *jsens = cJSON_CreateArray();
      char *rest1 = sensor;
      while ((sensor_p = strtok_r(rest1, ".", &rest1)))
      {
        cJSON *name = cJSON_CreateString(sensor_p);
        cJSON_AddItemToArray(jsens, name);
      };
      cJSON_AddItemToArray(sensors_path, jsens);

      ESP_LOGV(TAG, "Add sensor path %s", cJSON_Print(jsens));
      sensors_array[sensors_count++] = new Sensor();
    };

    free(sensors_map_copy);

    if (sensors_count == 0)
    {
      ESP_LOGE(TAG, "Error in sensors definition");
    }
  }

  float get_setup_priority() const override { return esphome::setup_priority::PROCESSOR; }
  // float get_setup_priority() const override { return esphome::setup_priority::WIFI; }

  void setup() override
  {
    if (!request_buf_handle)
    {
      request_buf_handle = xRingbufferCreate(512, RINGBUF_TYPE_NOSPLIT);
      request_queue = xQueueCreate(10, sizeof(HTTPSensor *));
      xTaskCreate(http_client_task, "http_client_task", 4096, this, 10, &myTaskHandle);
    }
  }

  void update() override
  {
    // This will be called every "update_interval" milliseconds.
    // ESP_LOGD("HTTPSensor", "update");
    // UBaseType_t res = xRingbufferSend(request_buf_handle, _url, strlen(_url) + 1, 0);
    HTTPSensor *clobj = this;
    UBaseType_t res = xQueueSend(request_queue, (void *)&clobj, (TickType_t)0);
    if (res != pdTRUE)
    {
      ESP_LOGE(TAG, "Failed to send request");
    }

    // parse_data(dht);
  }

  void loop() override
  {
  }

  esp_err_t parse_data(const char *buffer)
  {
    cJSON *root = cJSON_Parse(buffer);
    cJSON *subitem = NULL;
    for (int i = 0; i < this->sensors_count; i++)
    {
      cJSON *jsensor = cJSON_GetArrayItem(this->sensors_path, i);
      cJSON *jsensor_item = NULL;
      subitem = root;
      cJSON_ArrayForEach(jsensor_item, jsensor)
      {
        subitem = cJSON_GetObjectItem(subitem, jsensor_item->valuestring);
        if (!subitem)
        {
          ESP_LOGE(TAG, "Sensor \"%s\" - data not found", jsensor_item->valuestring);
          break;
        }
      }

      if (subitem)
      {
        this->sensors_array[i]->publish_state(subitem->valuedouble);
      }
    }

    cJSON_Delete(root);
    return ESP_OK;
  }

protected:
};

static void http_client_task(void *pv)
{
  char local_response_buffer[LOCAL_BUFFER_SIZE] = {0};

  HTTPSensor *clobj;

  esp_http_client_config_t config_client = {
      .url = "http://192.168.10.69/cm?cmnd=Status%2010",
      .timeout_ms = 3000,
      .event_handler = _http_event_handler,
      .transport_type = HTTP_TRANSPORT_OVER_TCP,
      .buffer_size = 1024,
      .user_data = local_response_buffer, // Pass address of local buffer to get response
  };

  esp_http_client_handle_t http_client = {0};

  while (true)
  {
    UBaseType_t res = xQueueReceive(request_queue, &clobj, portMAX_DELAY);
    ESP_LOGV(TAG, "Try GET %s", clobj->url);
    if (clobj->url != NULL)
    {
      config_client.url = clobj->url;

      http_client = esp_http_client_init(&config_client);

      //clobj->parse_data(dht);

      // GET
      esp_err_t err = esp_http_client_perform(http_client);
      if (err == ESP_OK)
      {
        ESP_LOGV(TAG, "Data: %s", local_response_buffer);
        ESP_LOGD(TAG, "HTTP GET Status = %d, content_length = %d",
                 esp_http_client_get_status_code(http_client),
                 esp_http_client_get_content_length(http_client));

        clobj->parse_data(local_response_buffer);
      }
      else
      {
        ESP_LOGE(TAG, "HTTP GET request failed: %s", esp_err_to_name(err));
      }

      esp_http_client_cleanup(http_client);
    }
  }
}

// #endif
