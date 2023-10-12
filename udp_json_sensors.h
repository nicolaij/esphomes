#pragma once

#include "esphome/core/component.h"
#include "esphome/components/sensor/sensor.h"

static const char *TAG = "UDP_SENSOR";

#define LOCAL_BUFFER_SIZE 1024

#include <WiFiUdp.h>
#include <AsyncTCP.h>
#include <vector>

// static std::vector<AsyncClient *> clients; // a list to hold all clients
AsyncClient *lastclient;
StaticJsonDocument<255> sensordata;
bool updated_t = false;
bool updated_h = false;
bool updated_v = false;
char packetBuffer[255];

class UDPSensor : public PollingComponent, public Sensor
{
public:
  WiFiUDP Udp;
  char packetBuffer[255];

  sensor::Sensor *temperature_sensor = new Sensor();
  sensor::Sensor *humidity_sensor = new Sensor();
  sensor::Sensor *voltage_sensor = new Sensor();

  // constructor
  UDPSensor(unsigned int port) : PollingComponent(1200000)
  {
    localPort_ = port;
    timeout_t_ = millis();
    timeout_h_ = millis();
    timeout_v_ = millis();
  }

  // float get_setup_priority() const override { return esphome::setup_priority::PROCESSOR; }
  float get_setup_priority() const override { return esphome::setup_priority::WIFI; }

  void setup() override
  {
    Udp.begin(localPort_);
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

  unsigned int localPort_ = 23456;
};

class TCPSensor : public PollingComponent, public Sensor
{
public:
  sensor::Sensor *temperature_sensor = new Sensor();
  sensor::Sensor *humidity_sensor = new Sensor();
  sensor::Sensor *voltage_sensor = new Sensor();

  // constructor
  TCPSensor(unsigned int port) : PollingComponent(1200000)
  {
    localPort_ = port;
    timeout_t_ = millis();
    timeout_h_ = millis();
    timeout_v_ = millis();
  }

  // float get_setup_priority() const override { return esphome::setup_priority::PROCESSOR; }
  float get_setup_priority() const override { return esphome::setup_priority::WIFI; }

  void setup() override
  {
    AsyncServer *server = new AsyncServer(localPort_);
    server->onClient(&handleNewClient, server);
    server->begin();
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
    if (updated_t)
    {
      DeserializationError error = deserializeJson(sensordata, packetBuffer);
      if (!error)
      {
        if (sensordata.containsKey("t"))
        {
          float t = sensordata["t"];
          timeout_t_ = millis();
          temperature_sensor->publish_state(t);
          updated_t = false;
        }
        if (sensordata.containsKey("h"))
        {
          float h = sensordata["h"];
          timeout_h_ = millis();
          humidity_sensor->publish_state(h);
          updated_h = false;
        }
        if (sensordata.containsKey("v"))
        {
          float v = sensordata["v"];
          timeout_v_ = millis();
          voltage_sensor->publish_state(v);
          updated_v = false;
        }
      }
      updated_t = false;
    }
  }

  /* clients events */
  static void handleError(void *arg, AsyncClient *client, int8_t error)
  {
    // Serial.printf("\n connection error %s from client %s \n", client->errorToString(error), client->remoteIP().toString().c_str());
  }

  static void handleData(void *arg, AsyncClient *client, void *data, size_t len)
  {
    packetBuffer[0] = 0;

    if (len > 0)
    {
      memcpy(packetBuffer, data, len);
      packetBuffer[len] = 0;
      ESP_LOGD("TCPSensor", "data received from client %s: %s", client->remoteIP().toString().c_str(), packetBuffer);
      updated_t = true;
    }

    client->close(true);
  }

  static void handleDisconnect(void *arg, AsyncClient *client)
  {
    // Serial.printf("\n client %s disconnected \n", client->remoteIP().toString().c_str());
  }

  static void handleTimeOut(void *arg, AsyncClient *client, uint32_t time)
  {
    // Serial.printf("\n client ACK timeout ip: %s \n", client->remoteIP().toString().c_str());
  }

  static void handleNewClient(void *arg, AsyncClient *client)
  {
    // Serial.printf("\n new client has been connected to server, ip: %s", client->remoteIP().toString().c_str());

    // add to list
    // clients.push_back(client);
    lastclient = client;

    // register events
    client->onData(&handleData, NULL);
    client->onError(&handleError, NULL);
    client->onDisconnect(&handleDisconnect, NULL);
    client->onTimeout(&handleTimeOut, NULL);
  }

protected:
  unsigned long timeout_t_;
  unsigned long timeout_h_;
  unsigned long timeout_v_;

  unsigned int localPort_ = 23456;
};

// #endif
