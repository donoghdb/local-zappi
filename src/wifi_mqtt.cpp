#include "wifi_mqtt.h"
#include "globals.h"
#include "credentials.h" 
#include "hardware.h"    
#include "menu_system.h" 
#include "web_server.h"  
#include <esp_ota_ops.h>

// Options Arrays
const char *chargerOptions[] = {"Stopped", "Eco", "Eco++", "Fast"};
const char *connectionOptions[] = {"EV Connected", "EV Disconnected", "Error"};
const char *chargingOptions[] = {
  "Not Connected", "Connected", "Charging", "Charge Complete",
  "Ventilation Required", "No Power, Ready to Connect", "--- EVSE ERROR ---"
};

// ===============================
//  TOPIC SETUP (Merged from setupTopics.cpp)
// ===============================
void setupTopics() {
    // Generic Device Topics
    snprintf(discoveryTopic, sizeof(discoveryTopic), "%s/sensor/%s/config", haPrefix, deviceName);
    snprintf(deviceTopic, sizeof(deviceTopic), "%s/sensor/%s/state", baseTopic, deviceName);

    // Duty Cycle
    snprintf(dutyCycleStateTopicConfig, sizeof(dutyCycleStateTopicConfig), "%s/sensor/duty_cycle/config", haPrefix);
    snprintf(dutyCycleStateTopic, sizeof(dutyCycleStateTopic), "%s/sensor/duty_cycle/state", baseTopic);

    // Duty Cycle Amps
    snprintf(dutyCycleAmpsStateTopicConfig, sizeof(dutyCycleAmpsStateTopicConfig), "%s/sensor/duty_cycle_amps/config", haPrefix);
    snprintf(dutyCycleAmpsStateTopic, sizeof(dutyCycleAmpsStateTopic), "%s/sensor/duty_cycle_amps/state", baseTopic);

    // Charging State
    snprintf(chargingStateTopicConfig, sizeof(chargingStateTopicConfig), "%s/sensor/charging_state/config", haPrefix);
    snprintf(chargingStateTopic, sizeof(chargingStateTopic), "%s/sensor/charging_state/state", baseTopic);

    // Charger Mode
    snprintf(chargerStateTopicConfig, sizeof(chargerStateTopicConfig), "%s/sensor/charger/config", haPrefix);
    snprintf(chargerStateTopic, sizeof(chargerStateTopic), "%s/sensor/charger/state", baseTopic);

    // Car Connection
    snprintf(carStateTopicConfig, sizeof(carStateTopicConfig), "%s/sensor/car_connection/config", haPrefix);
    snprintf(carStateTopic, sizeof(carStateTopic), "%s/sensor/car_connection/state", baseTopic);

    // Reboot Button
    snprintf(charger_rebootConfig, sizeof(charger_rebootConfig), "%s/button/zappi/restart/config", haPrefix);
    snprintf(charger_reboot, sizeof(charger_reboot), "%s/button/zappi/restart/command", baseTopic);

    // Zappi Charging State (custom)
    snprintf(zappi_charging_stateConfig, sizeof(zappi_charging_stateConfig), "%s/sensor/zappi_charging_state/config", haPrefix);
    snprintf(zappi_charging_state, sizeof(zappi_charging_state), "%s/sensor/zappi_charging_state/state", baseTopic);

    // Menu Reset Button
    snprintf(menuResetTopicConfig, sizeof(menuResetTopicConfig), "%s/button/zappi/menu_reset/config", haPrefix);
    snprintf(menuResetTopic, sizeof(menuResetTopic), "%s/button/zappi/menu_reset/command", baseTopic);

    // Charger Reset Button
    snprintf(chargerResetTopicConfig, sizeof(chargerResetTopicConfig), "%s/button/zappi/charger_reset/config", haPrefix);
    snprintf(chargerResetTopic, sizeof(chargerResetTopic), "%s/button/zappi/charger_reset/command", baseTopic);
}

// ===============================
//  CONNECTION LOGIC
// ===============================

void connectToWifi() {
  DEBUG_PRINTLN("Connecting to WiFi...");
  WiFi.begin(ssid, password);
}

void connectToMqtt() {
  // Ensure topics are built before connecting!
  setupTopics(); 
  
  DEBUG_PRINTLN("Connecting to MQTT...");
  mqttClient.connect();
}

void wifiConnected(WiFiEvent_t event, WiFiEventInfo_t info) {
  DEBUG_PRINTLN("WiFi connected");
  connectToMqtt();
}

void wifiDisconnected(WiFiEvent_t event, WiFiEventInfo_t info) {
  DEBUG_PRINTLN("WiFi disconnected");
  xTimerStart(wifiReconnectTimer, 0);
}

void setupMqtt() {
  // You can also call setupTopics() here to be safe
  setupTopics();

  mqttClient.setServer(mqttServer, mqttPort);
  mqttClient.setClientId(mqttClientId);
  mqttClient.setCredentials(mqttUser, mqttPassword);

  mqttClient.onConnect(onMqttConnect);
  mqttClient.onDisconnect(onMqttDisconnect);
  mqttClient.onMessage(onMqttMessage);
}


// ===============================
//  MQTT CALLBACKS
// ===============================

void onMqttConnect(bool sessionPresent) {
  DEBUG_PRINTLN("Connected to MQTT broker (async)");

  // ============================================================
  //  OTA SUCCESS CONFIRMATION
  // ============================================================
  const esp_partition_t *running = esp_ota_get_running_partition();
  esp_ota_img_states_t ota_state;
  if (esp_ota_get_state_partition(running, &ota_state) == ESP_OK) {
      if (ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
          DEBUG_PRINTLN("OTA Verification: MQTT Connected. Marking Firmware Valid!");
          esp_ota_mark_app_valid_cancel_rollback();
      }
  }
  
  // Subscribe to topics
  // Note: Using dynamic topics from globals would be safer, but subscription strings
  // must be const char*. We can construct them or use the buffers if they are ready.
  // For now, using the hardcoded "zappi/..." pattern you had is okay if baseTopic doesn't change.
  
  // 1. Relay (Boost) Command
  char subRelayTopic[128];
  snprintf(subRelayTopic, sizeof(subRelayTopic), "%s/switch/relay/command", baseTopic);
  mqttClient.subscribe(subRelayTopic, 1);

  // 2. Global Commands
  mqttClient.subscribe(menuResetTopic, 1);
  mqttClient.subscribe(chargerResetTopic, 1);
  mqttClient.subscribe(charger_reboot, 1);
  mqttClient.subscribe(zappi_charging_state, 1);

  for (int i = 0; i < 4; i++) {
    char subTopic[128];
    snprintf(subTopic, sizeof(subTopic), "%s/switch/button%d/command", baseTopic, i + 1);
    mqttClient.subscribe(subTopic, 1);
  }

  // Initial Publishes
  char menuActiveTopic[128];
  snprintf(menuActiveTopic, sizeof(menuActiveTopic), "%s/sensor/menuActive/state", baseTopic);
  mqttClient.publish(menuActiveTopic, 1, true, "OFF");
  
  mqttClient.publish(dutyCycleStateTopic, 1, true, "0");
  mqttClient.publish(dutyCycleAmpsStateTopic, 1, true, "0");

  // Run Discovery
  MqttDiscoveryInitial();
}

void onMqttDisconnect(AsyncMqttClientDisconnectReason reason) {
  DEBUG_PRINTF("MQTT disconnected, reason=%d\n", (int)reason);
  if (WiFi.isConnected()) {
    xTimerStart(mqttReconnectTimer, 0);
  }
}

void onMqttMessage(char* topic, char* payload, AsyncMqttClientMessageProperties properties, size_t len, size_t index, size_t total) {
  
  // Convert payload to String for easier handling
  String payloadStr;
  if (len > 0) payloadStr = String((const char*)payload).substring(0, len);
  String receivedTopic = String(topic);

  // Debug
  DEBUG_PRINT("Msg received on: "); DEBUG_PRINTLN(topic);

  // 1. Charging State Override
  if (receivedTopic.equals(zappi_charging_state)) {
    if (payloadStr.equals("ON")) charging_state = false;
    else if (payloadStr.equals("OFF")) charging_state = true;
  }

  // 2. Menu Reset
  if (receivedTopic.equals(menuResetTopic) && payloadStr.equals("RESET")) {
    resetMenuToOff();
  }

  // 3. Charger Mode Reset
  if (receivedTopic.equals(chargerResetTopic) && payloadStr.equals("RESET")) {
    // Reset to Stopped (Counter 1)
    counter = 1;
    updateChargerState(counter); 
  }

  // 4. Restart Command
  if (receivedTopic.equals(charger_reboot) && payloadStr.equals("restart")) {
    ESP.restart();
  }

  // 5. Relay (Boost) Command
  // Dynamic check: does the topic match our relay command topic?
  // We can construct the expected topic to compare safely.
  char relayCmd[128];
  snprintf(relayCmd, sizeof(relayCmd), "%s/switch/relay/command", baseTopic);
  
  if (receivedTopic.equals(relayCmd)) {
    if (payloadStr.equals("ON")) {
      digitalWrite(switchPin, HIGH);
      boostSwitchState = true;
    } else if (payloadStr.equals("OFF")) {
      digitalWrite(switchPin, LOW);
      boostSwitchState = false;
    }
    
    // Publish state update
    char relayState[128];
    snprintf(relayState, sizeof(relayState), "%s/switch/relay/state", baseTopic);
    mqttClient.publish(relayState, 1, true, payloadStr.c_str());

    String msg = "{\"boost_state\":\"" + String(boostSwitchState ? "ON" : "OFF") + "\"}";
    ws.textAll(msg);
  }

  // 6. Buttons
  for (int i = 0; i < 4; i++) {
    char btnCmd[128];
    snprintf(btnCmd, sizeof(btnCmd), "%s/switch/button%d/command", baseTopic, i + 1);
    
    if (receivedTopic.equals(btnCmd) && payloadStr.equals("ON")) {
      char btnState[128];
      snprintf(btnState, sizeof(btnState), "%s/switch/button%d/state", baseTopic, i + 1);
      handleButtons(btnState, i); // Logic inside hardware.cpp
    }
  }
}

// ===============================
//  DISCOVERY FUNCTIONS
// ===============================

void publishDiscovery(const char *name, const char *sensorName, const char *unitOfMeasurement, const char *unique_id, const char *icon, const char *cat, const char *deviceClass, const char *options[], size_t optionCount) {
  JsonDocument discoveryDoc;

  discoveryDoc["name"] = name;
  if (unitOfMeasurement && strlen(unitOfMeasurement) > 0) discoveryDoc["unit_of_meas"] = unitOfMeasurement;
  if (cat && strlen(cat) > 0) discoveryDoc["ent_cat"] = cat;
  if (icon && strlen(icon) > 0) discoveryDoc["ic"] = icon;
  
  // Construct state topic
  char stateTop[128];
  snprintf(stateTop, sizeof(stateTop), "%s/sensor/%s/state", baseTopic, sensorName);
  discoveryDoc["stat_t"] = stateTop;
  
  discoveryDoc["uniq_id"] = unique_id;
  if (deviceClass && strlen(deviceClass) > 0) discoveryDoc["device_class"] = deviceClass;

  if (options != nullptr && optionCount > 0) {
    JsonArray opts = discoveryDoc["options"].to<JsonArray>();
    for (size_t i = 0; i < optionCount; i++) opts.add(options[i]);
  }

  JsonObject dev = discoveryDoc["dev"].to<JsonObject>();
  dev["name"] = deviceName;
  dev["ids"] = deviceIdentifiers;
  dev["mdl"] = deviceModel;
  dev["mf"] = deviceManufacturer;
  dev["sw"] = deviceSwVersion;

  String discoveryPayload;
  serializeJson(discoveryDoc, discoveryPayload);

  char configTopic[128];
  snprintf(configTopic, sizeof(configTopic), "%s/sensor/%s/config", haPrefix, sensorName);
  
  if (mqttClient.connected()) {
    mqttClient.publish(configTopic, 1, true, discoveryPayload.c_str());
  }
  DEBUG_PRINTLN("Discovery Sent for " + String(name));
}

void sendButtonDiscovery(const char *name, int buttonIndex) {
  JsonDocument discoveryDoc;
  discoveryDoc["name"] = name;
  
  char statTopic[128]; snprintf(statTopic, sizeof(statTopic), "%s/switch/button%d/state", baseTopic, buttonIndex);
  discoveryDoc["stat_t"] = statTopic;
  
  char cmdTopic[128]; snprintf(cmdTopic, sizeof(cmdTopic), "%s/switch/button%d/command", baseTopic, buttonIndex);
  discoveryDoc["cmd_t"] = cmdTopic;
  
  discoveryDoc["pl_on"] = "ON";
  discoveryDoc["pl_off"] = "OFF";

  if (buttonIndex == 1) discoveryDoc["ic"] = "mdi:menu";
  else if (buttonIndex == 2) discoveryDoc["ic"] = "mdi:menu-up-outline";
  else if (buttonIndex == 3) discoveryDoc["ic"] = "mdi:menu-down-outline";
  else if (buttonIndex == 4) discoveryDoc["ic"] = "mdi:plus";

  char uniq[64]; snprintf(uniq, sizeof(uniq), "mainbutton%d", buttonIndex);
  discoveryDoc["uniq_id"] = uniq;
  
  // Device info
  JsonObject dev = discoveryDoc["dev"].to<JsonObject>();
  dev["name"] = deviceName; dev["ids"] = deviceIdentifiers; dev["mdl"] = deviceModel; dev["mf"] = deviceManufacturer; dev["sw"] = deviceSwVersion;

  String discoveryPayload; serializeJson(discoveryDoc, discoveryPayload);
  
  char confTopic[128]; snprintf(confTopic, sizeof(confTopic), "%s/switch/button%d/config", haPrefix, buttonIndex);
  
  if (mqttClient.connected()) {
    mqttClient.publish(confTopic, 1, true, discoveryPayload.c_str());
    mqttClient.publish(statTopic, 1, true, "OFF");
  }
  DEBUG_PRINTLN("Button Discovery Sent");
}

void sendRestartDiscovery(const char *name) {
  JsonDocument discoveryDoc;
  discoveryDoc["name"] = name;
  discoveryDoc["cmd_t"] = charger_reboot; // Defined in globals/setupTopics
  discoveryDoc["pl_prs"] = "restart";
  discoveryDoc["entity_category"] = "config";
  discoveryDoc["device_class"] = "restart";
  discoveryDoc["uniq_id"] = "restart1";
  
  JsonObject dev = discoveryDoc["dev"].to<JsonObject>();
  dev["name"] = deviceName; dev["ids"] = deviceIdentifiers; dev["mdl"] = deviceModel; dev["mf"] = deviceManufacturer; dev["sw"] = deviceSwVersion;

  String discoveryPayload; serializeJson(discoveryDoc, discoveryPayload);
  if (mqttClient.connected()) mqttClient.publish(charger_rebootConfig, 1, true, discoveryPayload.c_str());
}

void sendRelayDiscovery(const char *name) {
  JsonDocument discoveryDoc;
  discoveryDoc["name"] = name;
  
  char statTopic[128]; snprintf(statTopic, sizeof(statTopic), "%s/switch/relay/state", baseTopic);
  discoveryDoc["stat_t"] = statTopic;
  
  char cmdTopic[128]; snprintf(cmdTopic, sizeof(cmdTopic), "%s/switch/relay/command", baseTopic);
  discoveryDoc["cmd_t"] = cmdTopic;
  
  discoveryDoc["pl_on"] = "ON";
  discoveryDoc["pl_off"] = "OFF";
  discoveryDoc["ic"] = "mdi:ev-plug-type2";
  discoveryDoc["uniq_id"] = "relay1";
  
  JsonObject dev = discoveryDoc["dev"].to<JsonObject>();
  dev["name"] = deviceName; dev["ids"] = deviceIdentifiers; dev["mdl"] = deviceModel; dev["mf"] = deviceManufacturer; dev["sw"] = deviceSwVersion;

  String discoveryPayload; serializeJson(discoveryDoc, discoveryPayload);
  
  char confTopic[128]; snprintf(confTopic, sizeof(confTopic), "%s/switch/relay/config", haPrefix);
  
  if (mqttClient.connected()) {
    mqttClient.publish(confTopic, 1, true, discoveryPayload.c_str());
    mqttClient.publish(statTopic, 1, true, "OFF");
  }
  DEBUG_PRINTLN("Relay Discovery Sent");
}

void sendMenuResetDiscovery() {
  JsonDocument doc;
  doc["name"] = "Menu Reset";
  doc["cmd_t"] = menuResetTopic;
  doc["entity_category"] = "config";
  doc["icon"] = "mdi:menu-open";
  doc["unique_id"] = "zappi_menu_reset";
  
  JsonObject dev = doc["device"].to<JsonObject>();
  dev["name"] = deviceName; dev["identifiers"] = deviceIdentifiers; dev["model"] = deviceModel; dev["manufacturer"] = deviceManufacturer; dev["sw_version"] = deviceSwVersion;

  String payload; serializeJson(doc, payload);
  if (mqttClient.connected()) mqttClient.publish(menuResetTopicConfig, 1, true, payload.c_str());
  DEBUG_PRINTLN("Menu Reset Discovery Sent");
}

void sendStateResetDiscovery() {
  JsonDocument doc;
  doc["name"] = "State Reset";
  doc["cmd_t"] = chargerResetTopic;
  doc["entity_category"] = "config";
  doc["icon"] = "mdi:menu-open";
  doc["unique_id"] = "zappi_state_reset";

  JsonObject dev = doc["device"].to<JsonObject>();
  dev["name"] = deviceName; dev["identifiers"] = deviceIdentifiers; dev["model"] = deviceModel; dev["manufacturer"] = deviceManufacturer; dev["sw_version"] = deviceSwVersion;

  String payload; serializeJson(doc, payload);
  if (mqttClient.connected()) mqttClient.publish(chargerResetTopicConfig, 1, true, payload.c_str());
  DEBUG_PRINTLN("State Reset Discovery Sent");
}

// Function to send button discovery message
void sendStatusDiscoveryButton(const char *name)
{
  String discoveryTopic = String(haPrefix) + "/binary_sensor/zappi_status/config";
  String zappi_status = String(baseTopic) + "/binary_sensor/zappi_status/state";

  JsonDocument discoveryDoc;
  discoveryDoc["name"] = name;
  discoveryDoc["stat_t"] = zappi_status;
  discoveryDoc["ent_cat"] = "diagnostic";
  discoveryDoc["dev_cla"] = "connectivity";
  discoveryDoc["uniq_id"] = "status132";
  // discoveryDoc["pl_on"] = "";
  // discoveryDoc["pl_off"] = "";
  discoveryDoc["dev"]["name"] = deviceName;
  discoveryDoc["dev"]["ids"] = deviceIdentifiers;
  discoveryDoc["dev"]["mdl"] = deviceModel;
  discoveryDoc["dev"]["mf"] = deviceManufacturer;
  discoveryDoc["dev"]["sw"] = deviceSwVersion;

  String discoveryPayload;
  serializeJson(discoveryDoc, discoveryPayload);

  // Publish the discovery message
  if (mqttClient.connected()) {
    mqttClient.publish(discoveryTopic.c_str(),1,true,discoveryPayload.c_str());
    mqttClient.publish(zappi_status.c_str(),1,true,"ON");
  }
  //client.publish(discoveryTopic.c_str(), discoveryPayload.c_str(),true);
  DEBUG_PRINTLN("Status Button Discovery Sent");

  //String zappi_status = String(baseTopic) + "/binary_sensor/zappi_status/state";
  //client.publish(zappi_status.c_str(), "ON", true);
}

void MqttDiscoveryInitial() {
  if (!mqttClient.connected()) return;

  // 1. Main Device
  JsonDocument deviceDoc;
  deviceDoc["name"] = deviceName;
  deviceDoc["identifiers"] = deviceIdentifiers;
  deviceDoc["model"] = deviceModel;
  deviceDoc["manufacturer"] = deviceManufacturer;
  deviceDoc["sw_version"] = deviceSwVersion;
  String devicePayload; serializeJson(deviceDoc, devicePayload);
  mqttClient.publish(discoveryTopic, 1, true, devicePayload.c_str());

  // 2. Sensors
  publishDiscovery("Charger Status", "charger", "", "d37a87ba", "mdi:ev-plug-type2", "","enum",chargerOptions,4);
  publishDiscovery("Charging State", "charging_state", "", "07310886", "mdi:ev-station", "","enum",chargingOptions,7);
  publishDiscovery("Car Connection", "car_connection", "", "03de5180", "mdi:car-off", "","enum",connectionOptions,3);
  publishDiscovery("Duty Cycle", "duty_cycle", "%", "7a7ded1a", "mdi:square-wave", "");
  publishDiscovery("Charging Amps", "duty_cycle_amps", "Amps", "7a7degt1a", "mdi:square-wave", "");
  publishDiscovery("Zappi IP", "zappi_ip", "", "wadsf334", "mdi:ip-outline", "diagnostic");
  publishDiscovery("Zappi RSSI", "zappi_rssi", "dBm", "wadsf33ew4", "mdi:wifi-check", "diagnostic");
  publishDiscovery("Zappi Wifi %", "zappi_rssi_per", "%", "wadsf33eerw4", "mdi:wifi-check", "diagnostic");
  
  // 3. Buttons & Switches
  sendButtonDiscovery("Enter", 1);
  sendButtonDiscovery("Up", 2);
  sendButtonDiscovery("Down", 3);
  sendButtonDiscovery("Select", 4);
  sendRelayDiscovery("Boost");
  sendRestartDiscovery("Zappi Restart");
  sendStatusDiscoveryButton("Zappi Connection");
  sendMenuResetDiscovery();
  sendStateResetDiscovery();
  
  // 4. WiFi Info
  char ipTopic[128]; snprintf(ipTopic, sizeof(ipTopic), "%s/sensor/zappi_ip/state", baseTopic);
  mqttClient.publish(ipTopic, 1, true, WiFi.localIP().toString().c_str()); // Publish IP Address

  char ipTopicRssi[128]; snprintf(ipTopicRssi, sizeof(ipTopicRssi), "%s/sensor/zappi_rssi/state", baseTopic);
  mqttClient.publish(ipTopicRssi, 1, true, String(WiFi.RSSI()).c_str()); // Publish RSSI

  char ipTopicRssiPer[128]; snprintf(ipTopicRssiPer, sizeof(ipTopicRssiPer), "%s/sensor/zappi_rssi_per/state", baseTopic);  
  mqttClient.publish(ipTopicRssiPer, 1, true, String(min(max(2 * (WiFi.RSSI() + 100.0), 0.0), 100.0)).c_str()); // Publish RSSI %
}