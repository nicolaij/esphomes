#pragma once
// #ifdef USE_ESP_IDF

#include "esphome/core/component.h"
#include "esphome/components/sensor/sensor.h"

#include "freertos/FreeRTOS.h"
#include "freertos/ringbuf.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_tls.h"
#include "esp_http_client.h"

#include "string.h"
#include "cJSON.h"
static const char *TAG = "HTTP_CLIENT";

#define LOCAL_BUFFER_SIZE 1024

RingbufHandle_t buf_handle = NULL;
TaskHandle_t myTaskHandle = NULL;

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
    if (evt->user_data && (evt->data_len + output_len) <= LOCAL_BUFFER_SIZE)
    {
      memcpy((char *)evt->user_data + output_len, evt->data, evt->data_len);
      output_len += evt->data_len;
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
      ESP_LOGI(TAG, "Last esp error code: 0x%x", err);
      ESP_LOGI(TAG, "Last mbedtls failure: 0x%x", mbedtls_err);
    }
    output_len = 0;
    break;
  }
  return ESP_OK;
}

const char *dht = "{\"StatusSNS\": {         \"Time\": \"2018.02.01 22:48:39\",        \"DHT11\": {          \"Temperature\": 12.0,          \"Humidity\": 42.0        },        \"TempUnit\": \"C\"      }}";

class HTTPSensor : public PollingComponent, public Sensor
{
public:
  Sensor *sensors_array[10] = {0};
  int sensors_count = 0;

  // constructor
  HTTPSensor(const char *url, const char *sensors_map) : PollingComponent(15000)
  {
    _url = url;
    jsensors = cJSON_Parse(sensors_map);
    cJSON *subitem = NULL;

    cJSON_ArrayForEach(subitem, jsensors)
    {
      ESP_LOGV("HTTPSensor", "Add sensor %s", subitem->valuestring);
      sensors_array[sensors_count++] = new Sensor();
    }
  }

  float get_setup_priority() const override { return esphome::setup_priority::PROCESSOR; }
  // float get_setup_priority() const override { return esphome::setup_priority::AFTER_WIFI; }

  void setup() override
  {
    if (!buf_handle)
    {
      buf_handle = xRingbufferCreate(512, RINGBUF_TYPE_NOSPLIT);
      xTaskCreate(http_client_task, "http_client_task", 4096, NULL, 10, &myTaskHandle);
    }

    /*
        // This will be called by App.setup()
        ESP_LOGD("HTTPSensor", "setup");
        cJSON *root = cJSON_Parse(dht);
        cJSON *subitem = NULL;
        for (int i = 0; i < sensors_count; i++)
        {
          cJSON *jsensor = cJSON_GetArrayItem(jsensors, i);
          char *sens = strdup(jsensor->valuestring);
          ESP_LOGV("HTTPSensor", "Find: \"%s\"", jsensor->valuestring);

          char *token;
          char *rest = sens;

          subitem = root;

          while ((token = strtok_r(rest, ".", &rest)))
          {
            subitem = cJSON_GetObjectItem(subitem, token);
            if (!subitem)
            {
              ESP_LOGE("HTTPSensor", "Sensor \"%s\" - data not found", token);
              break;
            }
          }

          if (subitem)
          {
            sensors_array[i]->publish_state(subitem->valuedouble);
          }

          free(sens);
        }

        cJSON_Delete(root);

        */
  }

  void update() override
  {
    // This will be called every "update_interval" milliseconds.
    //ESP_LOGD("HTTPSensor", "update");
    UBaseType_t res = xRingbufferSend(buf_handle, _url, strlen(_url) + 1, 0);
    if (res != pdTRUE)
    {
      ESP_LOGE("HTTPSensor", "Failed to send url");
    }
  }

  static void http_client_task(void *pv)
  {
    char local_response_buffer[LOCAL_BUFFER_SIZE] = {0};

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
      // Receive an item from no-split ring buffer
      size_t url_item_size;
      char *url_item = (char *)xRingbufferReceive(buf_handle, &url_item_size, portMAX_DELAY);
      ESP_LOGD("HTTPSensor", "GET %s", url_item);
      if (url_item != NULL)
      {
        config_client.url = url_item;

        http_client = esp_http_client_init(&config_client);

        // GET
        esp_err_t err = esp_http_client_perform(http_client);
        if (err == ESP_OK)
        {
          ESP_LOGV(TAG, "Data: %s", local_response_buffer);
          ESP_LOGI(TAG, "HTTP GET Status = %d, content_length = %d",
                   esp_http_client_get_status_code(http_client),
                   esp_http_client_get_content_length(http_client));
        }
        else
        {
          ESP_LOGE(TAG, "HTTP GET request failed: %s", esp_err_to_name(err));
        }

        esp_http_client_cleanup(http_client);

        vRingbufferReturnItem(buf_handle, (void *)url_item);
      }
    }
  }

protected:
  const char *_url;
  cJSON *jsensors;
};
// #endif
