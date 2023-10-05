#pragma once

#include "esphome/core/component.h"
#include "esphome/components/sensor/sensor.h"

static const char *TAG = "UDP_SENSOR";

#define LOCAL_BUFFER_SIZE 1024

#include <WiFiUdp.h>

class UDPSensor : public PollingComponent, public Sensor
{
public:
  sensor::Sensor *temperature_sensor{nullptr};
  sensor::Sensor *humidity_sensor{nullptr};
  sensor::Sensor *voltage_sensor{nullptr};
  WiFiUDP Udp;
  unsigned int localPort = 23456;
  char packetBuffer[255];

  // constructor
  UDPSensor() : PollingComponent(60000)
  {
  }

  // float get_setup_priority() const override { return esphome::setup_priority::PROCESSOR; }
  float get_setup_priority() const override { return esphome::setup_priority::WIFI; }

  void setup() override
  {
    Udp.begin(localPort);
  }

  void update() override
  {
    // This will be called every "update_interval" milliseconds.
  }

  void loop() override
  {

    int packetSize = Udp.parsePacket();

    if (packetSize)
    {
      Serial.print("Received packet of size ");
      Serial.println(packetSize);
      Serial.print("From ");
      IPAddress remoteIp = Udp.remoteIP();
      Serial.print(remoteIp);
      Serial.print(", port ");
      Serial.println(Udp.remotePort());
      // read the packet into packetBufffer
      int len = Udp.read(packetBuffer, 255);
      if (len > 0)
      {
        packetBuffer[len] = 0;
      }

      Serial.println("Contents:");
      Serial.println(packetBuffer);
    }
  }
};

// #endif
