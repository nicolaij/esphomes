#pragma once

#include "esphome/core/component.h"
#include "esphome/components/sensor/sensor.h"

static const char *TAG = "UDP_SENSOR";

#define LOCAL_BUFFER_SIZE 1024

#include <WiFiUdp.h>

class UDPSensor : public PollingComponent, public Sensor
{
public:
  WiFiUDP Udp;
  unsigned int localPort = 23456;
  char packetBuffer[255];

  StaticJsonDocument<255> sensordata;

  sensor::Sensor *temperature_sensor = new Sensor();
  sensor::Sensor *humidity_sensor = new Sensor();
  sensor::Sensor *voltage_sensor = new Sensor();

  // constructor
  UDPSensor() : PollingComponent(1200000)
  {
    timeout_t_ = millis();
    timeout_h_ = millis();
    timeout_v_ = millis();
  }

  // float get_setup_priority() const override { return esphome::setup_priority::PROCESSOR; }
  float get_setup_priority() const override { return esphome::setup_priority::WIFI; }

  void setup() override
  {
    Udp.begin(localPort);
  }

  void update() override
  {
    if (millis() - timeout_t_ > this->get_update_interval())
      temperature_sensor->publish_state(NAN);
    if (millis() - timeout_h_ > this->get_update_interval())
      humidity_sensor->publish_state(NAN);
    if (millis() - timeout_v_ > this->get_update_interval())
      voltage_sensor->publish_state(NAN);

    // This will be called every "update_interval" milliseconds.
  }

  void loop() override
  {

    int packetSize = Udp.parsePacket();

    if (packetSize)
    {
      // read the packet into packetBufffer
      int len = Udp.read(packetBuffer, 255);
      if (len > 0)
      {
        packetBuffer[len] = 0;
        DeserializationError error = deserializeJson(sensordata, packetBuffer);
        if (!error)
        {
          if (sensordata.containsKey("t"))
          {
            float t = sensordata["t"];
            timeout_t_ = millis();
            temperature_sensor->publish_state(t);
          }
          if (sensordata.containsKey("h"))
          {
            float h = sensordata["h"];
            timeout_h_ = millis();
            humidity_sensor->publish_state(h);
          }
          if (sensordata.containsKey("v"))
          {
            float v = sensordata["v"];
            timeout_v_ = millis();
            voltage_sensor->publish_state(v);
          }
        }
      }
    }
  }

protected:
  unsigned long timeout_t_;
  unsigned long timeout_h_;
  unsigned long timeout_v_;
};

// #endif
