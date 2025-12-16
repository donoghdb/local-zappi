#include "esphome.h"

// --- VARIABLES ---
volatile unsigned long pulseStart = 0;
volatile unsigned long pulseEnd = 0;
volatile bool pulseDetected = false;

float storedDuty = 0.0;
unsigned long lastValidPulseTime = 0;
int PIN_INT_GLOBAL = 3; 

// --- INTERRUPT HANDLER ---
void IRAM_ATTR handlePilotInterrupt() {
  if (digitalRead(PIN_INT_GLOBAL) == HIGH) {
    pulseStart = micros();
  } else {
    pulseEnd = micros();
    pulseDetected = true;
  }
}

// --- SETUP FUNCTION ---
void setup_zappi(int pin_int) {
  PIN_INT_GLOBAL = pin_int;
  pinMode(PIN_INT_GLOBAL, INPUT);
  attachInterrupt(digitalPinToInterrupt(PIN_INT_GLOBAL), handlePilotInterrupt, CHANGE);
}

// --- DUTY CYCLE READER ---
float get_zappi_duty() {
  if (pulseDetected) {
    noInterrupts();
    unsigned long localStart = pulseStart;
    unsigned long localEnd = pulseEnd;
    pulseDetected = false;
    interrupts();

    if (localEnd > localStart) {
      long width = localEnd - localStart;
      if (width < 1200 && width > 0) {
        float duty = ((float)width / 1000.0) * 100.0;
        if (duty > 100.0) duty = 100.0;
        storedDuty = duty;
        lastValidPulseTime = millis();
      }
    }
  }

  if (millis() - lastValidPulseTime > 2000) {
    return;
  }
  return storedDuty;
}