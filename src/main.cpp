#include <Arduino.h>
#include "globals.h"
#include "hardware.h"
#include "menu_system.h"
#include "wifi_mqtt.h"
#include "web_server.h"
#include <ArduinoOTA.h>
#include "credentials.h"
#include <ESPmDNS.h>
#include <esp_task_wdt.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>



void setup() {
  Serial.begin(115200);
  DEBUG_PRINTLN("Zappi Controller Starting...");

  // 1. Data & Globals
  prefs.begin("zappi", false);
  counter = prefs.getInt("counter", 1);
  counterDirty = false;

  // 2. Hardware (Pins & Interrupts)
  setupHardware();

  // 3. START TASKS
  // ============================================================
  xTaskCreatePinnedToCore(counterNvsTask, "CounterNVS", 4096, NULL, 1, &counterNvsTaskHandle, 1);
  xTaskCreatePinnedToCore(buttonReleaseTask, "BtnRelTask", 4096, NULL, 1, &buttonReleaseTaskHandle, 1);
  xTaskCreatePinnedToCore(returnHandlerTask, "ReturnTask", 4096, NULL, 2, &returnHandlerTaskHandle, 1);
  xTaskCreatePinnedToCore(rssiPublishTask, "RSSITask", 4096, NULL, 1, &rssiPublishTaskHandle, 1);
  xTaskCreatePinnedToCore(adcMeasurementTask, "ADCTask", 10000, NULL, 1, &updateADC, 0); // Core 0 for ADC
  // ============================================================

  // 4. Create Timers
  mqttReconnectTimer = xTimerCreate("mqttTimer", pdMS_TO_TICKS(5000), pdFALSE, nullptr, [](TimerHandle_t) { connectToMqtt(); });
  wifiReconnectTimer = xTimerCreate("wifiTimer", pdMS_TO_TICKS(5000), pdFALSE, nullptr, [](TimerHandle_t) { connectToWifi(); });
  
  // Menu Timer (Prevent crash on menu open)
  menuTimeoutTimer = xTimerCreate("menuTimer", pdMS_TO_TICKS(3600000), pdFALSE, (void*)0, [](TimerHandle_t xTimer){
      extern void resetMenuToOff();
      resetMenuToOff(); 
  });

  // 5. Network Configuration
  WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info){
      // Call your original callback
      wifiConnected(event, info);
      
      // AND restart mDNS now that we are 100% sure we have an IP
      if (MDNS.begin("evcharger")) {
         MDNS.addService("http", "tcp", 80);
         DEBUG_PRINTLN("mDNS restarted: http://evcharger.local");
      }
  }, ARDUINO_EVENT_WIFI_STA_GOT_IP);
  WiFi.onEvent(wifiDisconnected, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
  
  // Start WiFi BEFORE WebServer to prevent "Invalid mbox" crash
  WiFi.config(staticIP, gateway, subnet); 
  connectToWifi(); 
  
  // 6. Setup Services 
  setupMqtt();
  setupWebServer(); 
  
  // 7. OTA with Watchdog Handling
  ArduinoOTA.onStart([]() {
    String type = (ArduinoOTA.getCommand() == U_FLASH) ? "sketch" : "filesystem";
    DEBUG_PRINTLN("OTA Update Start: " + type);

    // 1. DISABLE WATCHDOG (Crucial!)
    // We tell the watchdog: "Ignore the main loop for a while, I'm busy updating."
    esp_task_wdt_delete(NULL);

    // 2. Disable Sleep & Detach Interrupts (Prevent hardware interference)
    WiFi.setSleep(false);
    detachInterrupt(digitalPinToInterrupt(adcInterruptPin)); 
    for(int i=0; i<4; i++) {
        detachInterrupt(digitalPinToInterrupt(returnButtonPins[i]));
    }

    // 3. Suspend Tasks
    if (updateADC != NULL) vTaskSuspend(updateADC);
    if (rssiPublishTaskHandle != NULL) vTaskSuspend(rssiPublishTaskHandle);
    if (buttonReleaseTaskHandle != NULL) vTaskSuspend(buttonReleaseTaskHandle);
    if (counterNvsTaskHandle != NULL) vTaskSuspend(counterNvsTaskHandle);
    
    // 4. Stop Menu Timer
    if (menuTimeoutTimer != NULL) xTimerStop(menuTimeoutTimer, 0);
  });

  ArduinoOTA.onEnd([]() {
    DEBUG_PRINTLN("\nOTA Update End");
    // Device usually reboots here, but if not, re-enable safety:
    esp_task_wdt_add(NULL); 
  });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    // Only print every 10% to save CPU cycles
    static int lastPercent = 0;
    int percent = (progress / (total / 100));
    if (percent - lastPercent >= 10) {
        DEBUG_PRINTF("OTA: %u%%\r", percent);
        lastPercent = percent;
    }
  });

  ArduinoOTA.onError([](ota_error_t error) {
    DEBUG_PRINTF("Error[%u]: ", error);
    
    // If update failed, we MUST restart the Watchdog and Hardware
    esp_task_wdt_add(NULL); 
    
    // Restore Interrupts
    attachInterrupt(digitalPinToInterrupt(adcInterruptPin), handleInterrupt, CHANGE);
    attachInterrupt(digitalPinToInterrupt(returnButtonPins[0]), handleReturn0, CHANGE);
    attachInterrupt(digitalPinToInterrupt(returnButtonPins[1]), handleReturn1, CHANGE);
    attachInterrupt(digitalPinToInterrupt(returnButtonPins[2]), handleReturn2, CHANGE);
    attachInterrupt(digitalPinToInterrupt(returnButtonPins[3]), handleReturn3, CHANGE);

    // Resume Tasks
    if (updateADC != NULL) vTaskResume(updateADC);
    if (rssiPublishTaskHandle != NULL) vTaskResume(rssiPublishTaskHandle);
    if (buttonReleaseTaskHandle != NULL) vTaskResume(buttonReleaseTaskHandle);
    if (counterNvsTaskHandle != NULL) vTaskResume(counterNvsTaskHandle);
  });

  ArduinoOTA.begin();
  
  // 8. Initial State
  updateChargerState(counter);

  // Initialize Watchdog with a 5-second timeout
  // If the loop() doesn't run for 5 seconds, the ESP32 reboots.
  esp_task_wdt_init(5, true); 
  esp_task_wdt_add(NULL); // Add the current thread (Loop) to the watchdog


  // 9. CHECK FOR OTA UPDATE
  const esp_partition_t *running = esp_ota_get_running_partition();
  esp_ota_img_states_t ota_state;
  if (esp_ota_get_state_partition(running, &ota_state) == ESP_OK) {
      if (ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
          DEBUG_PRINTLN("OTA Update Detected! Verifying stability...");
          // We do NOT mark valid yet. We wait for MQTT connection.
          // If we crash or watchdog reset before MQTT connects, 
          // the bootloader will rollback to the old version automatically.
      }
  }
}

void loop() {
  ArduinoOTA.handle();
  esp_task_wdt_reset();

  // Print Memory Stats every 60 seconds
  if (millis() - lastHeapPrint > 60000) {
    lastHeapPrint = millis();
    DEBUG_PRINTF("Free Heap: %u bytes | Max Block: %u bytes\n", ESP.getFreeHeap(), ESP.getMaxAllocHeap());
  }

  vTaskDelay(pdMS_TO_TICKS(1));
}