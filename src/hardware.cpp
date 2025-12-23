#include "hardware.h"
#include "globals.h"
#include "menu_system.h"
#include "web_server.h" 

// --- Task Handles ---
TaskHandle_t updateADC = NULL;
TaskHandle_t buttonReleaseTaskHandle = NULL;
TaskHandle_t returnHandlerTaskHandle = NULL;
TaskHandle_t rssiPublishTaskHandle = NULL;
TaskHandle_t counterNvsTaskHandle = NULL;

// --- Measurement Memory ---
// Prevents "flickering" 0 values if the loop is slightly out of sync with the 1kHz signal
float storedDuty = 0.0;
float storedAmps = 0.0;
unsigned long lastValidPulseTime = 0;


// ===============================
//  SETUP FUNCTION
// ===============================
void setupHardware() {
  pinMode(switchPin, OUTPUT);
  digitalWrite(switchPin, LOW); // Default OFF

  // Setup Buttons
  for (int i = 0; i < 4; i++) {
    pinMode(buttonPins[i], OUTPUT);
    digitalWrite(buttonPins[i], LOW);
    pinMode(returnButtonPins[i], INPUT_PULLUP);
  }

  // Attach Interrupts
  attachInterrupt(digitalPinToInterrupt(returnButtonPins[0]), handleReturn0, CHANGE);
  attachInterrupt(digitalPinToInterrupt(returnButtonPins[1]), handleReturn1, CHANGE);
  attachInterrupt(digitalPinToInterrupt(returnButtonPins[2]), handleReturn2, CHANGE);
  attachInterrupt(digitalPinToInterrupt(returnButtonPins[3]), handleReturn3, CHANGE);

  // ADC Setup
  pinMode(adcPin, INPUT);
  pinMode(adcInterruptPin, INPUT);
  
  // Measurement Interrupt
  attachInterrupt(digitalPinToInterrupt(adcInterruptPin), handleInterrupt, CHANGE);

  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);

  DEBUG_PRINTLN("Hardware setup complete");
}

// ===============================
//  INTERRUPT SERVICE ROUTINES (ISRs)
// ===============================

// Generic handler for return buttons
void IRAM_ATTR handleReturnInterrupt(int index) {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    int state = digitalRead(returnButtonPins[index]);

    if (ignoreNextReturn[index]) {
        ignoreNextReturn[index] = false;
        return;
    }

    uint32_t ticks = (uint32_t)xTaskGetTickCountFromISR();
    if (ticks < maskUntil[index]) return;

    if (state == LOW) {
        portENTER_CRITICAL_ISR(&synch);
        returnTriggered[index] = true;
        returnTriggerTime[index] = ticks;
        portEXIT_CRITICAL_ISR(&synch);
    }
    maskUntil[index] = ticks + maskDurationTicks;
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

// Wrappers
void IRAM_ATTR handleReturn0() { handleReturnInterrupt(0); }
void IRAM_ATTR handleReturn1() { handleReturnInterrupt(1); }
void IRAM_ATTR handleReturn2() { handleReturnInterrupt(2); }
void IRAM_ATTR handleReturn3() { handleReturnInterrupt(3); }

// ---------------------------------------------------------
//  RESTORED: Interrupt Handler
// ---------------------------------------------------------
void IRAM_ATTR handleInterrupt() {
  portENTER_CRITICAL_ISR(&synch); // LOCK
  
  if (digitalRead(adcInterruptPin) == HIGH) {
    pulseStart = micros();
  } else {
    pulseEnd = micros();
    pulseDetected = true;
  }
  
  portEXIT_CRITICAL_ISR(&synch); // UNLOCK
}

// ===============================
//  ADC & MEASUREMENT LOGIC
// ===============================
void measureDutyCycle() {
    
    // 1. Snapshot variables safely
    portENTER_CRITICAL(&synch);
    bool detected = pulseDetected;
    unsigned long localStart = pulseStart;
    unsigned long localEnd = pulseEnd;
    
    if(detected) pulseDetected = false; // Reset flag
    portEXIT_CRITICAL(&synch);

    // 2. Process New Pulse
    if (detected) {
        if (localEnd > localStart) {
            long pulseWidth = localEnd - localStart;

            // Sanity Filter: J1772 is < 1000us. 
            // We ignore glitches > 1200us (like the 3600us you saw before).
            if (pulseWidth < 1200 && pulseWidth > 0) { 
                
                // VALID PULSE: Update Memory
                storedDuty = (static_cast<float>(pulseWidth) / 1000.0f) * 100.0f;

                // Clamp to legal range
                if (storedDuty > 99.9f) storedDuty = 100.0f;
                else if (storedDuty <= 0.3f) storedDuty = 0.0f;

                storedAmps = storedDuty * 0.6f;
                lastValidPulseTime = millis(); // Refresh timestamp
            }
        }
    } 

    // 3. Publish based on Timer (Throttle MQTT)
    if ((long)(millis() - lastSampleTimeDutyCycle) >= (long)updateFrequencyDutyCycle) {
        
        float dutyToReport = 0.0;
        float ampsToReport = 0.0;

        // CHECK MEMORY: Is the data fresh? (younger than 2 seconds)
        // If we haven't seen a pulse in 2 seconds, assume signal is DC (0 Amps).
        if (millis() - lastValidPulseTime < 2000) {
            dutyToReport = storedDuty;
            ampsToReport = storedAmps;
        } else {
            // Signal Lost / Steady DC
            dutyToReport = 0.0;
            ampsToReport = 0.0;
        }

        String json = "{\"dutyCycle\":" + String(dutyToReport) + ",\"dutyCycleAmps\":" + String(ampsToReport) + "}";
        
        if (mqttClient.connected()) {
            mqttClient.publish(dutyCycleStateTopic, 1, true, String(dutyToReport).c_str());
            mqttClient.publish(dutyCycleAmpsStateTopic, 1, true, String(ampsToReport).c_str());
        }
        ws.textAll(json);
        
        lastSampleTimeDutyCycle = millis();
    }
}
                


// Handle ADC Value Thresholds and State Changes
void handleThreshold(int maxAdcValue) {
  // Use tuned thresholds from previous steps
  if (maxAdcValue >= 3600 && maxAdcValue < 4096) newState = STATE_A; // Not Connected
  else if (maxAdcValue >= 3100 && maxAdcValue < 3600) newState = STATE_B; // Connected
  else if (maxAdcValue >= 2500 && maxAdcValue < 3100) newState = STATE_C; // Charging
  else if (maxAdcValue >= 1900 && maxAdcValue < 2500) newState = STATE_D; // Ventilation required
  else if (maxAdcValue >= 1500 && maxAdcValue < 1900) newState = STATE_E; // No Power
  else if (maxAdcValue >= 0 && maxAdcValue < 1500) newState = STATE_F; // Error

  if (newState != oldState) {
    if (oldState == STATE_A && newState != STATE_A) charging_state = false;
    if (oldState == STATE_C && newState == STATE_B) charging_state = true;
    
    oldState = newState;
    publishChargingState(newState); 
  }
  measureDutyCycle();
}

// ===============================
//  TASKS
// ===============================

// ADC Measurement Task
void adcMeasurementTask(void *pvParameters) {
  (void)pvParameters;
  
  const TickType_t xFrequency = pdMS_TO_TICKS(sampleInterval);
  TickType_t xLastWakeTime = xTaskGetTickCount();

  for (;;) {
    vTaskDelayUntil(&xLastWakeTime, xFrequency);

    int maxAdcValue = 0;
    for (int i = 0; i < numSamples; i++) {
      int val = analogRead(adcPin);
      if (val > maxAdcValue) maxAdcValue = val;
      delayMicroseconds(10); 
    }

    handleThreshold(maxAdcValue);
  }
}

// NVS Counter Save Task
void counterNvsTask(void *pvParameters) {
  DEBUG_PRINTLN("Counter NVS Task Started");
  (void) pvParameters;
  const TickType_t checkDelay = pdMS_TO_TICKS(20000); 
  for (;;) {
    if (counterDirty) {
      if (WiFi.status() == WL_CONNECTED) {
        DEBUG_PRINTLN("Saving counter to NVS");
        if(prefs.getInt("counter", 1) != counter) {
          DEBUG_PRINTLN("Counter changed, writing to NVS");
          prefs.putInt("counter", counter);
          counterDirty = false;
          vTaskDelay(pdMS_TO_TICKS(200));
        }
      }
    }
    vTaskDelay(checkDelay);
  }
}

// Button Release Task
void buttonReleaseTask(void *pvParameters) {
  (void) pvParameters;
  const TickType_t delayTicks = pdMS_TO_TICKS(50);
  for (;;) {
    unsigned long now = millis();
    for (int i = 0; i < 4; ++i) {
      if (buttonPressTime[i] != 0 && (now - buttonPressTime[i]) >= buttonPressDuration) {
        digitalWrite(buttonPins[i], LOW);
        buttonPressTime[i] = 0;
        
        //char buf[64];
        //snprintf(buf, sizeof(buf), "%s/switch/button%d/state", baseTopic, i + 1);
        //if (mqttClient.connected()) mqttClient.publish(buf, 1, true, "OFF");
      }
    }
    vTaskDelay(delayTicks);
  }
}

// Return Button Handler Task
void returnHandlerTask(void *pvParameters) {
  (void) pvParameters;
  const TickType_t delayTicks = pdMS_TO_TICKS(10);
  for (;;) {
    for (int i = 0; i < numReturnButtons; ++i) {
      bool triggered = false;
      portENTER_CRITICAL(&synch);
      if (returnTriggered[i]) {
        returnTriggered[i] = false;
        triggered = true;
      }
      portEXIT_CRITICAL(&synch);

      if (triggered) {
        char buf[64];
        snprintf(buf, sizeof(buf), "%s/switch/button%d/state", baseTopic, i + 1);
        handleButtons(buf, i);
      }
    }
    vTaskDelay(delayTicks);
  }
}

// RSSI Publish Task
void rssiPublishTask(void *pvParameters) {
  (void) pvParameters;
  const TickType_t publishDelay = pdMS_TO_TICKS((unsigned long)sampleInterval_dia);
  for (;;) {
    if (mqttClient.connected()) {
      char rssiTopic[128];
      snprintf(rssiTopic, sizeof(rssiTopic), "%s/sensor/zappi_rssi/state", baseTopic);
      mqttClient.publish(rssiTopic, 1, true, String(WiFi.RSSI()).c_str());

      char rssiPerTopic[128];
      snprintf(rssiPerTopic, sizeof(rssiPerTopic), "%s/sensor/zappi_rssi_per/state", baseTopic);
      mqttClient.publish(rssiPerTopic, 1, true, 
        String(min(max(2 * (WiFi.RSSI() + 100.0), 0.0), 100.0)).c_str());
    }
    vTaskDelay(publishDelay);
  }
}

// ===============================
//  CHARGER MODE LOGIC
// ===============================
void resetChargerModeToDefault() {
  counter = 1; // reset to Stopped
  counterDirty = false;
  prefs.putInt("counter", counter); 
  updateChargerState(counter);
}

// ===============================
//  BUTTON LOGIC
// ===============================
String handleBoost() {
  boostSwitchState = !boostSwitchState;
  return boostSwitchState ? "ON" : "OFF";
}

void handleButtons(const char *buttontopic, int index) {
  if (buttontopic != nullptr && mqttClient.connected()) {
      mqttClient.publish(buttontopic, 1, true, "ON");
  }
  
  ignoreNextReturn[index] = true;
  maskUntil[index] = xTaskGetTickCount() + maskDurationTicks;

  digitalWrite(buttonPins[index], HIGH);
  buttonPressTime[index] = millis(); 

  char buf[64];
  if(index == 0) {
    snprintf(buf, sizeof(buf), "{\"event\":\"button_press\",\"button\":\"enterButton\"}");
    ws.textAll(buf);
    handleEnterButton();
  } else if (index == 1) {
    snprintf(buf, sizeof(buf), "{\"event\":\"button_press\",\"button\":\"upButton\"}");
    ws.textAll(buf);
    if (!menuActive) counter += direction;
    handleUpButton();
  } else if (index == 2) {
    snprintf(buf, sizeof(buf), "{\"event\":\"button_press\",\"button\":\"downButton\"}");
    ws.textAll(buf);
    if (!menuActive) counter -= direction;
    handleDownButton();
  } else if (index == 3) {
    snprintf(buf, sizeof(buf), "{\"event\":\"button_press\",\"button\":\"selectButton\"}");
    ws.textAll(buf);
    handleSelectButton();
  }

  if (counter > maxCount) counter = 1;
  else if (counter < 1) counter = 4;

  updateChargerState(counter); 
}

void updateChargerState(int &count) {
  switch (count) {
    case 1: charger_status = "Stopped"; break;
    case 2: charger_status = "Fast"; break;
    case 3: charger_status = "Eco"; break;
    case 4: charger_status = "Eco++"; break;
  }
  counterDirty = true;
  if (mqttClient.connected()) {
    mqttClient.publish(chargerStateTopic, 1, true, charger_status.c_str());
  }
  broadcastStatus(); 
}

void publishChargingState(ChargingState &newState) {
  switch (newState) {
    case STATE_A:
      charging_state_string = "Not Connected";
      car_connection_status = "EV Disconnected";
      icon_car = "mdi:car-off";
      break;
    case STATE_B:
      if (charging_state) {
        charging_state_string = "Charge Complete";
        car_connection_status = "EV Connected";
      } else {
        charging_state_string = "Connected";
        car_connection_status = "EV Connected";
      }
      icon_car = "mdi:car-connected";
      break;
    case STATE_C:
      charging_state_string = "Charging";
      car_connection_status = "EV Connected";
      icon_car = "mdi:car-connected";
      break;
    case STATE_D:
      charging_state_string = "Ventilation Required";
      car_connection_status = "EV Connected";
      icon_car = "mdi:car-connected";
      break;
    case STATE_E:
      charging_state_string = "No Power, Ready to Connect";
      car_connection_status = "EV Connected";
      icon_car = "mdi:car-connected";
      break;
    case STATE_F:
      charging_state_string = "--- EVSE ERROR ---";
      car_connection_status = "Error";
      icon_car = "mdi:car-off";
      break;
  }
  
  if (mqttClient.connected()) {
     mqttClient.publish(chargingStateTopic, 1, true, charging_state_string.c_str());
     mqttClient.publish(carStateTopic, 1, true, car_connection_status.c_str());
  }
  broadcastStatus();
}