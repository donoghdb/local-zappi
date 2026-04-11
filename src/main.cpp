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
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"



void setup() {
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0); // Disable Brownout Detector
  Serial.begin(115200);
  DEBUG_PRINTLN("Zappi Controller Starting...");

  // 1. Data & Globals
  prefs.begin("zappi", false);
  currentMode = prefs.getInt("currentMode", 1);
  counterDirty = false;
  // Load Schedule from Memory (NVS)
  schedEnabled   = prefs.getBool("schedEnabled", true);
  schedStartHour = prefs.getInt("schedStartH", 2); // 2 AM
  schedStartMin  = prefs.getInt("schedStartM", 0);
  schedEndHour   = prefs.getInt("schedEndH", 6); // 6 AM
  schedEndMin    = prefs.getInt("schedEndM", 0);
  

  // 2. Hardware (Pins & Interrupts)
  setupHardware();
  setupLogMenus();
  setupChargeSettingsMenus();

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
  // WiFi.config(staticIP, gateway, subnet); 
  connectToWifi(); 
  
  // 6. Setup Services 
  setupMqtt();
  setupWebServer(); 
  
  // 7. OTA with Watchdog Handling
  ArduinoOTA.onStart([]() {
    mqttLog("System: Wireless OTA Update Started...");
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
    mqttLog("System: OTA Update Successful! Rebooting...");
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
    mqttLog("System ERROR: OTA Update Failed!");
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
  updateChargerState(currentMode);

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

unsigned long lastScheduleCheck = 0;

void checkSchedule() {
  // 1. Throttle: Only run this check once every 5 seconds
  if (millis() - lastScheduleCheck < 5000) return;
  lastScheduleCheck = millis();

  if (!schedEnabled) return;

  // 2. THE FIX: Check 'Epoch Time' first (Non-Blocking)
  time_t now;
  time(&now);
  
  if (now < 1600000000) {
    // DEBUG_PRINTLN("Skipping Schedule: Waiting for Time Sync...");
    return; // EXIT IMMEDIATELY -> Frees up processor for OTA/MQTT
  }

  // 3. Now it is safe to use getLocalTime 
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return;

  // Convert everything to "Minutes from Midnight" for comparison
  int currentMins = (timeinfo.tm_hour * 60) + timeinfo.tm_min;
  int startMins   = (schedStartHour * 60) + schedStartMin;
  int endMins     = (schedEndHour * 60) + schedEndMin;

  // Handle overnight schedules (e.g. Start 23:00, End 05:00)
  bool inSchedule = false;
  if (startMins < endMins) {
    inSchedule = (currentMins >= startMins && currentMins < endMins);
  } else {
    inSchedule = (currentMins >= startMins || currentMins < endMins);
  }

  // ============================================================
  // THE NEW LOGIC: EDGE-TRIGGERED SCHEDULING
  // ============================================================
  // 'static' variables remember their value between function calls
  static bool wasInSchedule = false;
  static bool scheduleInitialized = false;

  // A. First run after boot & time sync
  if (!scheduleInitialized) {
    wasInSchedule = inSchedule;
    scheduleInitialized = true;
    
    // Safety: If the ESP32 reboots in the middle of the night (inside the window),
    // we want it to resume charging immediately.
    if (inSchedule && currentMode != 2) {
        DEBUG_PRINTLN("Boot: Woke up inside schedule window -> Forcing FAST");
        transitionToMode(2);
    }
    return; 
  }

  // B. Did we JUST ENTER the schedule window?
  if (inSchedule && !wasInSchedule) {
    DEBUG_PRINTLN("Schedule: Window STARTED -> Forcing FAST Mode");
    mqttLog("Schedule Action: Window STARTED -> Forcing FAST Mode");
    if (currentMode != 2) transitionToMode(2);
    wasInSchedule = true;
  } 
  
  // C. Did we JUST LEAVE the schedule window?
  else if (!inSchedule && wasInSchedule) {
    DEBUG_PRINTLN("Schedule: Window ENDED -> Forcing STOP");
    mqttLog("Schedule Action: Window ENDED -> Forcing STOP");
    // Only stop if we were actually Fast charging (so we don't kill a manual Eco session)
    if (currentMode == 2) transitionToMode(1);
    wasInSchedule = false;
  }
}

void loop() {
  ArduinoOTA.handle();
  esp_task_wdt_reset();

  // Checks WiFi signal every 30 seconds and reconnects if weak
  checkSignalHealth();
  
  // Print Memory Stats every 60 seconds
  if (millis() - lastHeapPrint > 60000) {
    lastHeapPrint = millis();
    DEBUG_PRINTF("Free Heap: %u bytes | Max Block: %u bytes\n", ESP.getFreeHeap(), ESP.getMaxAllocHeap());
  }

  checkSchedule(); // <--- Run the checkSchedule function
  publishSystemTime();
  vTaskDelay(pdMS_TO_TICKS(1));
}