#include "wifi_mqtt.h"
#include "globals.h"
#include "credentials.h" 
#include "hardware.h"    
#include "menu_system.h" 
#include "web_server.h"  
#include <esp_ota_ops.h>
#include <time.h>
#include <esp_sntp.h>

// Options Arrays
const char *chargerOptions[] = {"Stopped", "Fast", "Eco", "Eco++"};
const char *connectionOptions[] = {"EV Connected", "EV Disconnected", "Error"};
const char *chargingOptions[] = {
  "Not Connected", "Connected", "Charging", "Charge Complete",
  "Ventilation Required", "No Power, Ready to Connect", "--- EVSE ERROR ---"
};

unsigned long lastRssiCheck = 0;
const int MIN_RSSI_THRESHOLD = -85; // Signal is "bad" if worse than -85dBm
int lastPublishedMin = -1; // To track when to update system time
int wifiConnectAttempts = 0;
const int MAX_WIFI_ATTEMPTS = 6; // 6 attempts * 5 seconds = 30 seconds before AP mode
bool inFallbackAPMode = false;

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

// This function runs automatically the moment time is successfully synced
void timeSyncNotificationCallback(struct timeval *tv) {
    DEBUG_PRINTLN("✅ SNTP: Time Synchronized Successfully!");
    
    // Print the exact time right now to prove it
    struct tm timeinfo;
    getLocalTime(&timeinfo);
    DEBUG_PRINTF("✅ Current Time: %02d:%02d\n", timeinfo.tm_hour, timeinfo.tm_min);
}

void connectToWifi() {
  // 1. Check if we have failed too many times
  if (wifiConnectAttempts >= MAX_WIFI_ATTEMPTS) {
    if (!inFallbackAPMode) {
      DEBUG_PRINTLN("⚠️ WiFi Failed completely. Starting Fallback AP...");
      
      // Stop the reconnection timer so it stops spamming
      xTimerStop(wifiReconnectTimer, 0); 
      
      // Switch to Access Point Mode
      WiFi.disconnect();
      WiFi.mode(WIFI_AP);
      WiFi.softAP("Zappi-Setup", "password123"); // The network name and password
      
      DEBUG_PRINTLN("✅ AP Started! Connect your phone to 'Zappi-Setup'");
      DEBUG_PRINTLN("🌐 Web UI available at: http://192.168.4.1");
      
      inFallbackAPMode = true;
    }
    return; // Do not attempt to scan/connect anymore
  }

  // 2. We are still trying to connect...
  wifiConnectAttempts++;
  DEBUG_PRINTF("Scanning for AP (Attempt %d/%d)...\n", wifiConnectAttempts, MAX_WIFI_ATTEMPTS);

  DEBUG_PRINTLN("Scanning for strongest AP...");
  
  // 1. Scan for all networks (Async scan)
  int n = WiFi.scanNetworks(false, false, true, 120);
  
  // Variables to hold the best AP info
  int bestSignal = -127;
  uint8_t bestBSSID[6];
  int32_t bestChannel = 0;
  bool found = false;

  // 2. Iterate through results to find your SSID with best RSSI
  for (int i = 0; i < n; ++i) {
    if (WiFi.SSID(i) == ssid) { // 'ssid' comes from credentials.h
      DEBUG_PRINT("Found: "); 
      DEBUG_PRINT(WiFi.SSID(i));
      DEBUG_PRINT(" (");
      DEBUG_PRINT(WiFi.RSSI(i));
      DEBUG_PRINTLN("dBm)");

      if (WiFi.RSSI(i) > bestSignal) {
        bestSignal = WiFi.RSSI(i);
        bestChannel = WiFi.channel(i);
        memcpy(bestBSSID, WiFi.BSSID(i), 6);
        found = true;
      }
    }
  }
  
  // 3. Connect to the Specific Best AP
  if (found) {
    DEBUG_PRINTF("Connecting to Strongest BSSID: %02X:%02X:%02X:%02X:%02X:%02X at %d dBm\n", 
                 bestBSSID[0], bestBSSID[1], bestBSSID[2], bestBSSID[3], bestBSSID[4], bestBSSID[5], bestSignal);
                 
    // This overload of begin() forces a specific BSSID and Channel
    WiFi.begin(ssid, password, bestChannel, bestBSSID);
  } else {
    DEBUG_PRINTLN("Target SSID not found, using standard connect...");
    WiFi.begin(ssid, password);
  }
}

void connectToMqtt() {
  // Ensure topics are built before connecting!
  setupTopics(); 
  
  DEBUG_PRINTLN("Connecting to MQTT...");

  // 1. THE DEATH CERTIFICATE (Last Will)
  // We use a 'static' buffer so the memory stays valid while connecting
  static char lwtTopic[128]; 
  snprintf(lwtTopic, sizeof(lwtTopic), "%s/status", baseTopic);

  // Topic, QoS, Retain, Payload
  mqttClient.setWill(lwtTopic, 1, true, "offline");

  // 2. SERVER & CREDENTIALS (Crucial!)
  mqttClient.setServer(mqttServer, mqttPort);
  
  // --- RESTORED THIS LINE ---
  mqttClient.setCredentials(mqttUser, mqttPassword); 
  // --------------------------

  // 3. CONNECT
  mqttClient.connect();
}

void wifiConnected(WiFiEvent_t event, WiFiEventInfo_t info) {
  // RESET THE COUNTERS!
  wifiConnectAttempts = 0; 
  inFallbackAPMode = false;
  WiFi.mode(WIFI_STA); // Ensure AP is off if we connected successfully

  DEBUG_PRINTLN("WiFi Connected! IP Address:");
  DEBUG_PRINTLN(WiFi.localIP());

  // =======================================================
  
  // 1. Set the Callback (So we see the "✅" log immediately when it works)
  sntp_set_time_sync_notification_cb(timeSyncNotificationCallback);

  // 2. Set Timezone (GMT0 with 1hr Daylight Savings)
  // This helps the system calculate offsets correctly
  configTzTime("GMT0BST,M3.5.0/1,M10.5.0", "216.239.35.0", "129.6.15.28", "pool.ntp.org");
  
  // Note: 
  // "216.239.35.0" is time.google.com (IP address)
  // "129.6.15.28"  is time.nist.gov (IP address)
  // We use IPs to bypass any DNS issues.

  DEBUG_PRINTLN("NTP Time Sync Initiated (Using Direct IPs)...");
  // =======================================================

  connectToMqtt();
}

void wifiDisconnected(WiFiEvent_t event, WiFiEventInfo_t info) {
  DEBUG_PRINTLN("WiFi disconnected");
  xTimerStart(wifiReconnectTimer, 0);
}

void checkSignalHealth() {
  if (millis() - lastRssiCheck > 60000) { // Check every 60 seconds
    lastRssiCheck = millis();
    
    if (WiFi.status() == WL_CONNECTED) {
      long rssi = WiFi.RSSI();
      if (rssi < MIN_RSSI_THRESHOLD) {
        DEBUG_PRINTF("Signal Weak (%d dBm). Reconnecting to find better AP...\n", rssi);
        
        // Disconnect and let the auto-reconnect logic (which now Scans) take over
        WiFi.disconnect(); 
        // Note: Your existing wifiDisconnectHandler should trigger the reconnect timer
      }
    }
  }
}

void setupMqtt() {
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
  //  1. OTA SUCCESS CONFIRMATION
  // ============================================================
  const esp_partition_t *running = esp_ota_get_running_partition();
  esp_ota_img_states_t ota_state;
  if (esp_ota_get_state_partition(running, &ota_state) == ESP_OK) {
      if (ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
          DEBUG_PRINTLN("OTA Verification: MQTT Connected. Marking Firmware Valid!");
          esp_ota_mark_app_valid_cancel_rollback();
      }
  }
  
  // ============================================================
  //  2. SUBSCRIBE TO TOPICS & BRITH CERTIFICATE
  // ============================================================

  // --- THE BIRTH CERTIFICATE ---
  // We are alive! Publish "online"
  String statusTopic = String(baseTopic) + "/status";
  mqttClient.publish(statusTopic.c_str(), 1, true, "online");
  
  // A. Relay (Boost) Command
  char subTopicBuf[128]; // Reusable buffer for subscriptions
  snprintf(subTopicBuf, sizeof(subTopicBuf), "%s/switch/relay/command", baseTopic);
  mqttClient.subscribe(subTopicBuf, 1);

  // B. Global Commands
  mqttClient.subscribe(menuResetTopic, 1);
  mqttClient.subscribe(chargerResetTopic, 1);
  mqttClient.subscribe(charger_reboot, 1);
  mqttClient.subscribe(zappi_charging_state, 1);

  // C. Button Commands (Loop)
  for (int i = 0; i < 4; i++) {
    snprintf(subTopicBuf, sizeof(subTopicBuf), "%s/button/button%d/command", baseTopic, i + 1);
    mqttClient.subscribe(subTopicBuf, 1);
  }

  // --- NEW: SCHEDULE SUBSCRIPTIONS ---
  
  // D. Schedule Enable Switch
  snprintf(subTopicBuf, sizeof(subTopicBuf), "%s/switch/zap_sch_en/command", baseTopic);
  mqttClient.subscribe(subTopicBuf, 1);
  DEBUG_PRINT("Subscribed to: "); DEBUG_PRINTLN(subTopicBuf);

  // E. Schedule Start Time (Text)
  snprintf(subTopicBuf, sizeof(subTopicBuf), "%s/text/zap_sch_start/set", baseTopic);
  mqttClient.subscribe(subTopicBuf, 1);
  DEBUG_PRINT("Subscribed to: "); DEBUG_PRINTLN(subTopicBuf);

  // F. Schedule End Time (Text)
  snprintf(subTopicBuf, sizeof(subTopicBuf), "%s/text/zap_sch_end/set", baseTopic);
  mqttClient.subscribe(subTopicBuf, 1);
  DEBUG_PRINT("Subscribed to: "); DEBUG_PRINTLN(subTopicBuf);

  // G. Time Sync (Epoch Timestamp)
  snprintf(subTopicBuf, sizeof(subTopicBuf), "%s/time/set_epoch", baseTopic);
  mqttClient.subscribe(subTopicBuf, 1);
  DEBUG_PRINT("Subscribed to: "); DEBUG_PRINTLN(subTopicBuf);

  // ============================================================


  // ============================================================
  //  3. INITIAL PUBLISHES
  // ============================================================
  char menuActiveTopic[128];
  snprintf(menuActiveTopic, sizeof(menuActiveTopic), "%s/sensor/menuActive/state", baseTopic);
  mqttClient.publish(menuActiveTopic, 1, true, "OFF");
  
  mqttClient.publish(dutyCycleStateTopic, 1, true, "0");
  mqttClient.publish(dutyCycleAmpsStateTopic, 1, true, "0");

  // Run Discovery (and Initial Schedule State publish inside it)
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
  DEBUG_PRINT("MQTT RX: "); DEBUG_PRINT(receivedTopic);
  DEBUG_PRINT(" | Payload: "); DEBUG_PRINTLN(payloadStr);

  // 1. Charging State Override
  if (receivedTopic.equals(zappi_charging_state)) {
    if (payloadStr.equals("ON")) charging_state = false;
    else if (payloadStr.equals("OFF")) charging_state = true;
  }

  // 2. Menu Reset
  if (receivedTopic.equals(menuResetTopic) && payloadStr.equals("PRESS")) {
    resetMenuToOff();
  }

  // 3. Charger Mode Reset
  if (receivedTopic.equals(chargerResetTopic) && payloadStr.equals("PRESS")) {
    // Reset to Stopped (currentMode 1)
    currentMode = 1;
    updateChargerState(currentMode); 
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
    snprintf(btnCmd, sizeof(btnCmd), "%s/button/button%d/command", baseTopic, i + 1);
    
    if (receivedTopic.equals(btnCmd) && payloadStr.equals("PRESS")) {
      DEBUG_PRINTF("Button %d Pressed\n", i + 1);
      //char btnState[128];
      //snprintf(btnState, sizeof(btnState), "%s/button/button%d/state", baseTopic, i + 1);
      handleButtons(nullptr, i); // Logic inside hardware.cpp
    }
  }
  // 7. Schedule Commands

  // =================================================================
  // 7.1. SCHEDULE ENABLE SWITCH
  // =================================================================

  // Buffers for dynamic topic generation
  char targetCmd[128];
  char targetState[128];
  // Generate: "zappi/switch/zap_sch_en/command"
  snprintf(targetCmd, sizeof(targetCmd), "%s/switch/zap_sch_en/command", baseTopic);

  if (receivedTopic.equals(targetCmd)) {
    // Update Variable & Memory
    schedEnabled = (payloadStr == "ON");
    prefs.putBool("schedEnabled", schedEnabled);

    // Send Confirmation State Back
    snprintf(targetState, sizeof(targetState), "%s/switch/zap_sch_en/state", baseTopic);
    mqttClient.publish(targetState, 1, true, schedEnabled ? "ON" : "OFF");

    DEBUG_PRINTF("Schedule Enabled: %s\n", schedEnabled ? "ON" : "OFF");
  }


  // =================================================================
  // 7.2. START TIME (Text Input)
  // =================================================================
  // Generate: "zappi/text/zap_sch_start/set"
  snprintf(targetCmd, sizeof(targetCmd), "%s/text/zap_sch_start/set", baseTopic);

  if (receivedTopic.equals(targetCmd)) {
    int h, m;
    // Attempt to parse HH:MM
    if (sscanf(payloadStr.c_str(), "%d:%d", &h, &m) == 2) {
      schedStartHour = h;
      schedStartMin = m;
      prefs.putInt("schedStartH", h);
      prefs.putInt("schedStartM", m);

      // CONFIRM BACK TO HA (Force strict %02d:%02d format)
      char timeBuf[16];
      snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d", h, m);
      snprintf(targetState, sizeof(targetState), "%s/text/zap_sch_start/state", baseTopic);
      mqttClient.publish(targetState, 1, true, timeBuf);

      DEBUG_PRINTF("Action: Start Time Set -> %s\n", timeBuf);
    } else {
      DEBUG_PRINTLN("Error: Failed to parse Start Time format");
    }
  }


  // =================================================================
  // 7.3. END TIME (Text Input)
  // =================================================================
  // Generate: "zappi/text/zap_sch_end/set"
  snprintf(targetCmd, sizeof(targetCmd), "%s/text/zap_sch_end/set", baseTopic);

  if (receivedTopic.equals(targetCmd)) {
    int h, m;
    if (sscanf(payloadStr.c_str(), "%d:%d", &h, &m) == 2) {
      schedEndHour = h;
      schedEndMin = m;
      prefs.putInt("schedEndH", h);
      prefs.putInt("schedEndM", m);

      // CONFIRM BACK TO HA (Force strict %02d:%02d format)
      char timeBuf[16];
      snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d", h, m);
      snprintf(targetState, sizeof(targetState), "%s/text/zap_sch_end/state", baseTopic);
      mqttClient.publish(targetState, 1, true, timeBuf);

      DEBUG_PRINTF("Action: End Time Set -> %s\n", timeBuf);
    } else {
      DEBUG_PRINTLN("Error: Failed to parse End Time format");
    }
  }

  // =================================================================
  // 8. FORCE TIME SYNC (From Home Assistant)
  // =================================================================
  snprintf(targetCmd, sizeof(targetCmd), "%s/time/set_epoch", baseTopic);

  if (receivedTopic.equals(targetCmd)) {
    // Payload should be a large number (Epoch timestamp)
    long epoch = atol(payloadStr.c_str());
    
    if (epoch > 1600000000) { // Basic sanity check (Must be > Year 2020)
      struct timeval tv;
      tv.tv_sec = epoch;  // Set seconds
      tv.tv_usec = 0;     // Zero microseconds
      settimeofday(&tv, NULL); // <--- FORCE SYSTEM TIME
      
      DEBUG_PRINTF("✅ MQTT: Forced Time Update to Epoch: %ld\n", epoch);
      
      // Optional: Trigger your "System Time" publisher immediately to confirm
      publishSystemTime();
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
  
  char statTopic[128]; snprintf(statTopic, sizeof(statTopic), "%s/button/button%d/state", baseTopic, buttonIndex);
  discoveryDoc["stat_t"] = statTopic;
  
  char cmdTopic[128]; snprintf(cmdTopic, sizeof(cmdTopic), "%s/button/button%d/command", baseTopic, buttonIndex);
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
  
  char confTopic[128]; snprintf(confTopic, sizeof(confTopic), "%s/button/button%d/config", haPrefix, buttonIndex);
  
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
  dev["name"] = deviceName; 
  dev["ids"] = deviceIdentifiers; 
  dev["mdl"] = deviceModel; 
  dev["mf"] = deviceManufacturer; 
  dev["sw"] = deviceSwVersion;

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

void publishSystemTime() {
  if (!mqttClient.connected()) return;

  // 1. SAFETY CHECK (The Anti-Crash Fix)
  time_t now;
  time(&now);
  if (now < 1600000000) {
    // Time not valid yet. Do nothing.
    return; 
  }

  // 2. Safe to get details now
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return; 

  // Only publish if minute changed
  if (timeinfo.tm_min != lastPublishedMin) {
    lastPublishedMin = timeinfo.tm_min;

    char timeStr[16];
    char topicBuf[128];

    snprintf(timeStr, sizeof(timeStr), "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
    
    // Dynamic Topic
    snprintf(topicBuf, sizeof(topicBuf), "%s/sensor/system_time/state", baseTopic);
    mqttClient.publish(topicBuf, 1, true, timeStr);
  }
}

void sendTimeDiscovery() {
  JsonDocument discoveryDoc;
  char payloadBuffer[1024];
  char topicBuffer[128];
  char stateTopic[128];

  discoveryDoc.clear();
  discoveryDoc["name"] = "System Time";
  
  // Dynamic Topic
  snprintf(stateTopic, sizeof(stateTopic), "%s/sensor/system_time/state", baseTopic);
  discoveryDoc["stat_t"] = stateTopic;
  
  discoveryDoc["unique_id"] = "zappi_sys_time";
  discoveryDoc["icon"] = "mdi:clock-check"; // Nice checkmark clock icon
  discoveryDoc["val_tpl"] = "{{ value }}";  // Just show the raw text (HH:MM)

  JsonObject dev = discoveryDoc["dev"].to<JsonObject>();
  dev["name"] = deviceName;
  dev["ids"] = deviceIdentifiers;
  dev["mdl"] = deviceModel;
  dev["mf"] = deviceManufacturer;
  dev["sw"] = deviceSwVersion;

  // Publish Config
  snprintf(topicBuffer, sizeof(topicBuffer), "%s/sensor/system_time/config", haPrefix);
  serializeJson(discoveryDoc, payloadBuffer);
  
  if (mqttClient.connected()) {
    mqttClient.publish(topicBuffer, 1, true, payloadBuffer);
    DEBUG_PRINTLN("System Time Discovery Sent");
  }
}
// Function to send button discovery message
void sendStatusDiscoveryButton(const char *name)
{
  // 1. Define the topic that tracks Online/Offline status
  // We use a shorter, standard topic for LWT
  String statusTopic = String(baseTopic) + "/status"; 
  
  String discoveryTopic = String(haPrefix) + "/binary_sensor/zappi_status/config";

  JsonDocument discoveryDoc;
  discoveryDoc["name"] = name;
  discoveryDoc["stat_t"] = statusTopic; // Listen to the Status Topic
  
  discoveryDoc["pl_on"] = "online";     // Payload for "Connected"
  discoveryDoc["pl_off"] = "offline";   // Payload for "Disconnected"
  
  discoveryDoc["ent_cat"] = "diagnostic";
  discoveryDoc["dev_cla"] = "connectivity";
  discoveryDoc["uniq_id"] = "zappi_status_132";

  discoveryDoc["dev"]["name"] = deviceName;
  discoveryDoc["dev"]["ids"] = deviceIdentifiers;
  discoveryDoc["dev"]["mdl"] = deviceModel;
  discoveryDoc["dev"]["mf"] = deviceManufacturer;
  discoveryDoc["dev"]["sw"] = deviceSwVersion;

  String discoveryPayload;
  serializeJson(discoveryDoc, discoveryPayload);

  // Publish the discovery message
  if (mqttClient.connected()) {
    mqttClient.publish(discoveryTopic.c_str(), 1, true, discoveryPayload.c_str());
    
    // NOTE: We do NOT publish "ON" here anymore. 
    // That happens in the 'Birth Certificate' in onMqttConnect.
  }
  DEBUG_PRINTLN("Status Sensor Discovery Sent");
}

// Funtion to send schedule-related discovery messages
void sendScheduleDiscovery() {
  JsonDocument discoveryDoc;
  char payloadBuffer[1024];
  char topicBuffer[128];
  
  // Temporary buffers to hold the dynamic topic strings
  char cmdTopic[128];
  char statTopic[128];

  // ==========================================
  // 1. SCHEDULE ENABLE SWITCH
  // ==========================================
  discoveryDoc.clear();
  discoveryDoc["name"] = "Schedule Enable";
  
  // DYNAMIC TOPIC GENERATION
  snprintf(cmdTopic, sizeof(cmdTopic), "%s/switch/zap_sch_en/command", baseTopic);
  snprintf(statTopic, sizeof(statTopic), "%s/switch/zap_sch_en/state", baseTopic);
  discoveryDoc["cmd_t"] = cmdTopic;
  discoveryDoc["stat_t"] = statTopic;
  
  discoveryDoc["unique_id"] = "zap_sch_en";
  discoveryDoc["icon"] = "mdi:calendar-clock";
  discoveryDoc["dev_cla"] = "switch"; 

  JsonObject dev = discoveryDoc["dev"].to<JsonObject>();
  dev["name"] = deviceName;
  dev["ids"] = deviceIdentifiers;
  dev["mdl"] = deviceModel;
  dev["mf"] = deviceManufacturer;
  dev["sw"] = deviceSwVersion;

  snprintf(topicBuffer, sizeof(topicBuffer), "%s/switch/zap_sch_en/config", haPrefix);
  serializeJson(discoveryDoc, payloadBuffer);
  if (mqttClient.connected()) mqttClient.publish(topicBuffer, 1, true, payloadBuffer);


  // ==========================================
  // 2. SCHEDULE START TIME (Text Input)
  // ==========================================
  discoveryDoc.clear();
  discoveryDoc["name"] = "Schedule Start";
  
  // DYNAMIC TOPICS (Using 'text' to match your handler)
  snprintf(cmdTopic, sizeof(cmdTopic), "%s/text/zap_sch_start/set", baseTopic);
  snprintf(statTopic, sizeof(statTopic), "%s/text/zap_sch_start/state", baseTopic);
  discoveryDoc["cmd_t"] = cmdTopic; 
  discoveryDoc["stat_t"] = statTopic;
  
  discoveryDoc["unique_id"] = "zap_sch_start";
  discoveryDoc["icon"] = "mdi:clock-digital";
  discoveryDoc["pattern"] = "^([0-1]?[0-9]|2[0-3]):[0-5][0-9]$"; 

  dev = discoveryDoc["dev"].to<JsonObject>();
  dev["name"] = deviceName;
  dev["ids"] = deviceIdentifiers;
  dev["mdl"] = deviceModel;
  dev["mf"] = deviceManufacturer;
  dev["sw"] = deviceSwVersion;

  snprintf(topicBuffer, sizeof(topicBuffer), "%s/text/zap_sch_start/config", haPrefix);
  serializeJson(discoveryDoc, payloadBuffer);
  if (mqttClient.connected()) mqttClient.publish(topicBuffer, 1, true, payloadBuffer);


  // ==========================================
  // 3. SCHEDULE END TIME (Text Input)
  // ==========================================
  discoveryDoc.clear();
  discoveryDoc["name"] = "Schedule End";
  
  // DYNAMIC TOPICS
  snprintf(cmdTopic, sizeof(cmdTopic), "%s/text/zap_sch_end/set", baseTopic);
  snprintf(statTopic, sizeof(statTopic), "%s/text/zap_sch_end/state", baseTopic);
  discoveryDoc["cmd_t"] = cmdTopic;
  discoveryDoc["stat_t"] = statTopic;
  
  discoveryDoc["unique_id"] = "zap_sch_end";
  discoveryDoc["icon"] = "mdi:clock-digital";
  discoveryDoc["pattern"] = "^([0-1]?[0-9]|2[0-3]):[0-5][0-9]$";

  dev = discoveryDoc["dev"].to<JsonObject>();
  dev["name"] = deviceName;
  dev["ids"] = deviceIdentifiers;
  dev["mdl"] = deviceModel;
  dev["mf"] = deviceManufacturer;
  dev["sw"] = deviceSwVersion;

  snprintf(topicBuffer, sizeof(topicBuffer), "%s/text/zap_sch_end/config", haPrefix);
  serializeJson(discoveryDoc, payloadBuffer);
  if (mqttClient.connected()) mqttClient.publish(topicBuffer, 1, true, payloadBuffer);
  
  DEBUG_PRINTLN("Schedule Discovery Sent (Dynamic)");
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
  publishDiscovery("Zappi Menu", "menuActive", "", "zap_menu_ctx", "mdi:menu-open", "diagnostic");
  publishDiscovery("Zappi Menu Display", "menu_layout", "", "zap_menu_disp", "mdi:monitor-dashboard", "diagnostic");
  
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
  sendScheduleDiscovery();
  sendTimeDiscovery();

  // 4. WiFi Info
  char ipTopic[128]; snprintf(ipTopic, sizeof(ipTopic), "%s/sensor/zappi_ip/state", baseTopic);
  mqttClient.publish(ipTopic, 1, true, WiFi.localIP().toString().c_str()); // Publish IP Address

  char ipTopicRssi[128]; snprintf(ipTopicRssi, sizeof(ipTopicRssi), "%s/sensor/zappi_rssi/state", baseTopic);
  mqttClient.publish(ipTopicRssi, 1, true, String(WiFi.RSSI()).c_str()); // Publish RSSI

  char ipTopicRssiPer[128]; snprintf(ipTopicRssiPer, sizeof(ipTopicRssiPer), "%s/sensor/zappi_rssi_per/state", baseTopic);  
  mqttClient.publish(ipTopicRssiPer, 1, true, String(min(max(2 * (WiFi.RSSI() + 100.0), 0.0), 100.0)).c_str()); // Publish RSSI %

  // 5. Schedule Initial States
  char stateTopic[128];
  char timeBuf[16];

  // 1. Send Switch State
  snprintf(stateTopic, sizeof(stateTopic), "%s/switch/zap_sch_en/state", baseTopic);
  mqttClient.publish(stateTopic, 1, true, schedEnabled ? "ON" : "OFF");

  // 2. Send Start Time Text
  snprintf(stateTopic, sizeof(stateTopic), "%s/text/zap_sch_start/state", baseTopic);
  snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d", schedStartHour, schedStartMin);
  mqttClient.publish(stateTopic, 1, true, timeBuf);

  // 3. Send End Time Text
  snprintf(stateTopic, sizeof(stateTopic), "%s/text/zap_sch_end/state", baseTopic);
  snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d", schedEndHour, schedEndMin);
  mqttClient.publish(stateTopic, 1, true, timeBuf);
  
  DEBUG_PRINTLN("Initial Schedule States Sent");
}

void publishMenuState(const char* state) {
  char topic[128];
  snprintf(topic, sizeof(topic), "%s/sensor/menuActive/state", baseTopic);

  if (mqttClient.connected()) {
    mqttClient.publish(topic, 1, true, state);
  }
}

void publishMenuLayout(const char* layoutText) {
  if (mqttClient.connected()) {
    char topic[128];
    snprintf(topic, sizeof(topic), "%s/sensor/menu_layout/state", baseTopic);
    mqttClient.publish(topic, 1, false, layoutText);
  }
}