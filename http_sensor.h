#pragma once
// #ifdef USE_ESP_IDF

#include "esphome/core/component.h"
#include "esphome/components/sensor/sensor.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_tls.h"
#include "esp_http_client.h"

static const char *TAG = "HTTP_CLIENT";

#define LOCAL_BUFFER_SIZE 1024

class HTTPSensor : public PollingComponent, public Sensor
{
public:
  esp_http_client_config_t config = {
      //.url = "http://192.168.10.1/",
      .url = "http://192.168.10.69/cm?cmnd=Status%2010",

      //.host = "192.168.10.69",
      //.path = "/cm",
      //.query = "cmnd=Status%2010",
      .event_handler = _http_event_handler,
      .transport_type = HTTP_TRANSPORT_OVER_TCP,
      .buffer_size = 1024,
      .user_data = local_response_buffer, // Pass address of local buffer to get response
  };
  esp_http_client_handle_t client;
  Sensor *temperature_sensor = new Sensor();
  // constructor
  HTTPSensor() : PollingComponent(15000) {}

  float get_setup_priority() const override { return esphome::setup_priority::WIFI; }

  void setup() override
  {
    // This will be called by App.setup()
    //ESP_LOGD("HTTPSensor", "setup");
  }

  void update() override
  {
    // This will be called every "update_interval" milliseconds.
    ESP_LOGD("HTTPSensor", "update");
    client = esp_http_client_init(&config);

    // GET
    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK)
    {
      ESP_LOGV(TAG, "Data: %s", local_response_buffer);
      ESP_LOGI(TAG, "HTTP GET Status = %d, content_length = %d",
               esp_http_client_get_status_code(client),
               esp_http_client_get_content_length(client));
    }
    else
    {
      ESP_LOGE(TAG, "HTTP GET request failed: %s", esp_err_to_name(err));
    }
    // ESP_LOG_BUFFER_CHAR(TAG, local_response_buffer, strlen(local_response_buffer));
    esp_http_client_cleanup(client);
  }

  void loop() override
  {
    
  }

private:
  char local_response_buffer[LOCAL_BUFFER_SIZE] = {0};

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
      output_len = 0;
      break;
    }
    return ESP_OK;
  }

};
// #endif
