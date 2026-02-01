#ifndef HARDWARE_H
#define HARDWARE_H

#include <Arduino.h>
#include "globals.h"

// --- Setup ---
void setupHardware();

// --- Tasks ---
void adcMeasurementTask(void *pvParameters);
void buttonReleaseTask(void *pvParameters);
void returnHandlerTask(void *pvParameters);
void counterNvsTask(void *pvParameters);
void rssiPublishTask(void *pvParameters);

// --- Task Handle ---
extern TaskHandle_t updateADC;
extern TaskHandle_t buttonReleaseTaskHandle;
extern TaskHandle_t returnHandlerTaskHandle;
extern TaskHandle_t rssiPublishTaskHandle;
extern TaskHandle_t counterNvsTaskHandle;

// --- Interrupt Handlers ---
// These must be marked IRAM_ATTR to run safely from RAM
void IRAM_ATTR handleReturn0();
void IRAM_ATTR handleReturn1();
void IRAM_ATTR handleReturn2();
void IRAM_ATTR handleReturn3();
void IRAM_ATTR handleInterrupt(); // ADC Pulse interrupt

// --- Logic Helpers ---
void measureDutyCycle();
void handleThreshold(int maxAdcValue);
String handleBoost(); // Toggles the relay
void handleButtons(const char *buttontopic, int index);
void resetChargerModeToDefault();
void transitionToMode(int targetMode); // Move the Zappi physically to the target mode for Scheduling
void simulateButtonPress(int pinIndex); // Simulate a physical button press on the Zappi

#endif // HARDWARE_H