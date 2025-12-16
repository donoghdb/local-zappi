#ifndef WIFI_MQTT_H
#define WIFI_MQTT_H

#include <Arduino.h>
#include <AsyncMqttClient.h>
#include <WiFi.h>

// Connection Functions
void connectToWifi();
void connectToMqtt();
void setupMqtt(); // Helper to configure the client

// Callbacks
void wifiConnected(WiFiEvent_t event, WiFiEventInfo_t info);
void wifiDisconnected(WiFiEvent_t event, WiFiEventInfo_t info);
void onMqttConnect(bool sessionPresent);
void onMqttDisconnect(AsyncMqttClientDisconnectReason reason);
void onMqttMessage(char* topic, char* payload, AsyncMqttClientMessageProperties properties, size_t len, size_t index, size_t total);

// Discovery Helpers
void MqttDiscoveryInitial();
void publishDiscovery(
  const char *name,
  const char *sensorName,
  const char *unitOfMeasurement,
  const char *unique_id,
  const char *icon,
  const char *cat,
  const char *deviceClass = nullptr,
  const char *options[] = nullptr,
  size_t optionCount = 0
);
void sendButtonDiscovery(const char *name, int buttonIndex);
void sendRestartDiscovery(const char *name);
void sendStatusDiscoveryButton(const char *name);
void sendRelayDiscovery(const char *name);
void sendMenuResetDiscovery();
void sendStateResetDiscovery();

#endif  // WIFI_MQTT_H