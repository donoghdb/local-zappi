#include "hardware.h"
#include "globals.h"
#include "menu_system.h"
#include "web_server.h" 
#include "wifi_mqtt.h"

extern void mqttLog(const char* message);

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

  // Setup Buttons (Enter, Up, Down, Select)
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
    // 1. Snapshot variables safely (CRITICAL SECTION)
    portENTER_CRITICAL(&synch);
    bool detected = pulseDetected;
    unsigned long localStart = pulseStart;
    unsigned long localEnd = pulseEnd;
    
    if(detected) pulseDetected = false; // Reset flag
    portEXIT_CRITICAL(&synch);

    // 2. Process New Pulse
    if (detected) {
        // J1772 Pulse logic: Width is the high time.
        // Basic check: End time must be after Start time
        if (localEnd > localStart) {
            long pulseWidth = localEnd - localStart;

            // Sanity Filter: J1772 is 1kHz (1000us period). 
            // Valid pulses are usually 100us (10%) to 960us (96%).
            // We ignore glitches > 1200us.
            if (pulseWidth < 1200 && pulseWidth > 80) { 
                
                // VALID PULSE: Update Memory
                // Calculation: (HighTime / 1000us) * 100%
                storedDuty = (static_cast<float>(pulseWidth) / 1000.0f) * 100.0f;

                // Clamp to legal range
                if (storedDuty > 99.9f) storedDuty = 100.0f;
                else if (storedDuty <= 5.0f) storedDuty = 0.0f; // J1772 usually min 6A (10%)

                storedAmps = storedDuty * 0.6f;
                lastValidPulseTime = millis(); // Refresh timestamp
            }
        }
    } 

    // 3. Publish based on Timer (Throttle MQTT)
    if ((long)(millis() - lastSampleTimeDutyCycle) >= (long)updateFrequencyDutyCycle) {
        
        float dutyToReport = 0.0;
        float ampsToReport = 0.0;
        
        // ONLY report values if we are technically in a Charging State (C or D)
        if (newState == STATE_C || newState == STATE_D) {
            
            // We are Charging. Now check if the signal is fresh.
            if (millis() - lastValidPulseTime < 2500) {
                 // Signal is Strong -> Use live data
                 dutyToReport = storedDuty;
                 ampsToReport = storedAmps;
            } else {
                 // Signal is Weak/Missing, but Voltage says C -> LATCH
                 // (Assume we are still charging at the last known rate)
                 dutyToReport = storedDuty;
                 ampsToReport = storedAmps;
            }

        } else {
            // We are in State A, B (Charge Complete), E, or F.
            // Even if Zappi is sending a PWM "Offer", the car is taking 0.
            dutyToReport = 0.0;
            ampsToReport = 0.0;
            
            // Optional: Clear stored memory so the next charge starts fresh
            storedDuty = 0.0;
            storedAmps = 0.0;
        }

        // 4. Send to MQTT & Web
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

  // Check if State Has Changed
  if (newState != oldState) {
    
    // ============================================
    // 1. DETECT PLUG-IN (A -> B)
    // ============================================
if (oldState == STATE_A && newState == STATE_B) {
        DEBUG_PRINTLN("Car Plugged In! Checking Default Mode...");
        mqttLog("Car Plugged In! Checking Default Mode...");

        int modeToApply = 1; // Default to Stopped

        // LOGIC: Since Internal Order matches Default Order, 
        // we just map directly (except for MEM).
        
        if (defaultMode == 5) {
            // MEM: Keep current mode
            DEBUG_PRINTLN("Default Mode is MEM. Keeping current settings.");
            modeToApply = currentMode; 
        } else {
            // DIRECT MAPPING (1=Stopped, 2=Fast, 3=Eco, 4=Eco+)
            modeToApply = defaultMode;
        }

        // Apply if different
        if (currentMode != modeToApply) {
            DEBUG_PRINTF("Applying Default Mode: %d\n", modeToApply);
            currentMode = modeToApply;
            updateChargerState(currentMode);
        }
    }
    // ============================================

    // 2. Existing Logic
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
  DEBUG_PRINTLN("currentMode NVS Task Started");
  (void) pvParameters;
  
  // Check every 20 seconds
  const TickType_t checkDelay = pdMS_TO_TICKS(20000); 

  for (;;) {
    // Only wake up if a change occurred
    if (counterDirty) {
      
      // --- LOGIC UPDATE ---
      // Only save to Flash if Default Mode is "MEM" (5).
      // If it is 1-4 (Fixed), we don't care what the last state was.
      if (defaultMode == 5) {
        
        DEBUG_PRINTLN("MEM Mode Active: Saving settings to NVS...");
        prefs.putInt("currentMode", currentMode); 
        
      } else {
        DEBUG_PRINTLN("Fixed Mode Active: Skipping NVS Save (Not needed)");
      }

      // Reset the flag regardless. 
      // We have "handled" the change (either by saving it or intentionally ignoring it).
      counterDirty = false;
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
  currentMode = 1; // reset to Stopped
  counterDirty = false;
  prefs.putInt("currentMode", currentMode); 
  updateChargerState(currentMode);
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
    if (!menuActive) currentMode += direction;
    handleUpButton();
  } else if (index == 2) {
    snprintf(buf, sizeof(buf), "{\"event\":\"button_press\",\"button\":\"downButton\"}");
    ws.textAll(buf);
    if (!menuActive) currentMode -= direction;
    handleDownButton();
  } else if (index == 3) {
    snprintf(buf, sizeof(buf), "{\"event\":\"button_press\",\"button\":\"selectButton\"}");
    ws.textAll(buf);
    handleSelectButton();
  }

  if (currentMode > maxCount) currentMode = 1;
  else if (currentMode < 1) currentMode = 4;

  updateChargerState(currentMode); 
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

void transitionToMode(int targetMode) {
  if (currentMode == targetMode) return; // Already there

  DEBUG_PRINTF("Transitioning from Mode %d to %d\n", currentMode, targetMode);

  // LOGIC: Calculate steps
  // Order: 1=Stopped, 2=Fast, 3=Eco, 4=Eco+
  
  int diff = targetMode - currentMode;

  if (diff > 0) {
    // Target is higher (e.g., Stopped(1) -> Fast(2))
    // We need to press UP 'diff' times
    for (int i = 0; i < diff; i++) {
      simulateButtonPress(1); // Index 1 = UP Button (could go either way)
    }
  } 
  else {
    // Target is lower (e.g., Fast(2) -> Stopped(1))
    // We need to press DOWN 'diff' times (diff is negative, so use -diff)
    for (int i = 0; i < -diff; i++) {
      simulateButtonPress(2); // Index 2 = DOWN Button (could go either way)
    }
  }

  // Now that the physical clicks are done, update the internal memory
  currentMode = targetMode;
  updateChargerState(currentMode); // Update MQTT and other variables
  
  // Optional: Save to NVS immediately if you want this to persist
  prefs.putInt("currentMode", currentMode); 
}

// Helper to physically click a button
void simulateButtonPress(int pinIndex) {
  DEBUG_PRINTF("Simulating Button Click on Pin Index: %d\n", pinIndex);
  
  digitalWrite(buttonPins[pinIndex], HIGH); // Press
  delay(200);                               // Hold for 200ms (ensure Zappi registers it)
  digitalWrite(buttonPins[pinIndex], LOW);  // Release
  delay(400);                               // Wait 400ms before next click (debounce/UI lag)
}