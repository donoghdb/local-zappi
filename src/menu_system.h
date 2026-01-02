#ifndef MENU_SYSTEM_H
#define MENU_SYSTEM_H

#include <Arduino.h>
extern int defaultMode; // 1=Stopped, 2=Eco, 3=Eco+, 4=Fast, 5=MEM

// 1. The Structure Definition
// This MUST be here so globals.cpp knows what a "MenuItem" is.
struct MenuItem {
  const char* name;
  MenuItem* parent;
  MenuItem** children;
  uint8_t childCount;
  void (*action)();
  uint8_t lastSelected;  // remembers last position in this menu
};

// 2. Control Function Prototypes
void showMenu();
void resetMenuToOff();
void resetAllMenuPositions(MenuItem* menu);
void setupLogMenus();
void setupChargeSettingsMenus();
void toggleDefaultMode();

// Navigation Button Handlers
void handleEnterButton();
void handleSelectButton();
void handleUpButton();
void handleDownButton();

// Action Handlers (Optional to expose, but good practice if needed elsewhere)
void act_Default();
void act_ManualBoost();
void act_SmartBoost();
void act_BoostTimer();

// --- Timer Handle ---
extern TimerHandle_t menuTimeoutTimer;

#endif // MENU_SYSTEM_H