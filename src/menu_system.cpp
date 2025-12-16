#include "menu_system.h"
#include "globals.h"


// --- Timer Handle ---
TimerHandle_t menuTimeoutTimer = NULL;

// ===============================
//  FORWARD DECLARATIONS
// ===============================
// We need these so we can use them in the children arrays below
extern MenuItem m_ChargeLog;
extern MenuItem m_EventLog;
extern MenuItem m_Readings;
extern MenuItem m_Information;
extern MenuItem m_LinkedDevicesInfo;
extern MenuItem m_ChargeSettings;
extern MenuItem m_OtherSettings;

extern MenuItem m_ECOPLUS;
extern MenuItem m_ManualBoost;
extern MenuItem m_SmartBoost;
extern MenuItem m_BoostTimer;
extern MenuItem m_Preconditioning;
extern MenuItem m_DefaultMode;
extern MenuItem m_ECO_ECOPlus;

extern MenuItem m_TimeDate;
extern MenuItem m_DisplaySound;
extern MenuItem m_LockFunc;
extern MenuItem m_DownloadFirmware;
extern MenuItem m_Internet;
extern MenuItem m_Advanced;
extern MenuItem m_InstallerSettings;

extern MenuItem m_SupplyGrid;
extern MenuItem m_Network;
extern MenuItem m_LinkedDevices;
extern MenuItem m_CTConfig;
extern MenuItem m_eSense;
extern MenuItem m_CompatMode;
extern MenuItem m_System;
extern MenuItem m_DownloadFW;

extern MenuItem m_C_L_Today;
extern MenuItem m_C_L_Yest;
extern MenuItem m_C_L_Week;
extern MenuItem m_C_L_Month;
extern MenuItem m_C_L_Year;
extern MenuItem m_C_L_Total;
extern MenuItem m_C_L_Custom;

extern MenuItem m_E_L_Today;
extern MenuItem m_E_L_Yest;
extern MenuItem m_E_L_Week;
extern MenuItem m_E_L_Custom;

extern MenuItem m_Read1;
extern MenuItem m_Read2;
extern MenuItem m_Read3;
extern MenuItem m_Read4;
extern MenuItem m_Read5;
extern MenuItem m_Read6;
extern MenuItem m_Read7;
extern MenuItem m_Read8;
extern MenuItem m_Read9;
extern MenuItem m_Read10;
extern MenuItem m_Read11;

extern MenuItem m_Info1;
extern MenuItem m_Info2;
extern MenuItem m_Info3;
extern MenuItem m_Info4;
extern MenuItem m_Info5;

extern MenuItem eddi;
extern MenuItem zappi;
extern MenuItem vhub;


// ===============================
//  ACTION FUNCTIONS
// ===============================
void act_Default() { }
void act_ChargeLog() {}
void act_EventLog() { }
void act_Readings() { }
void act_Information() { }
void act_LinkedDevices() { }
void act_RGBLED() { }
void act_ManualBoost() { }
void act_SmartBoost() { }
void act_BoostTimer() { }

// ===============================
//  CHILD ARRAYS
// ===============================

static MenuItem* information_LinkedDevices[] = { &eddi, &zappi, &vhub };
static MenuItem* information_children[] = { &m_Info1, &m_Info2, &m_Info3, &m_Info4, &m_Info5 };
static MenuItem* eventlog_children[] = { &m_E_L_Today, &m_E_L_Yest, &m_E_L_Week, &m_E_L_Custom };
static MenuItem* chargelog_children[] = { &m_C_L_Today, &m_C_L_Yest, &m_C_L_Week, &m_C_L_Month, &m_C_L_Year, &m_C_L_Total, &m_C_L_Custom };
static MenuItem* readings_children[] = { &m_Read1, &m_Read2, &m_Read3, &m_Read4, &m_Read5, &m_Read6, &m_Read7, &m_Read8, &m_Read9, &m_Read10, &m_Read11 };
static MenuItem* chargesettings_children[] = { &m_ECOPLUS, &m_ManualBoost, &m_SmartBoost, &m_BoostTimer, &m_Preconditioning, &m_DefaultMode, &m_ECO_ECOPlus };
static MenuItem* othersettings_children[] = { &m_TimeDate, &m_DisplaySound, &m_LockFunc, &m_DownloadFirmware, &m_Internet, &m_Advanced, &m_InstallerSettings };
static MenuItem* advanced_children[] = { &m_SupplyGrid, &m_Network, &m_LinkedDevices, &m_CTConfig, &m_eSense, &m_CompatMode, &m_System, &m_DownloadFW };
static MenuItem* main_children[] = { &m_ChargeLog, &m_EventLog, &m_Readings, &m_Information, &m_LinkedDevicesInfo, &m_ChargeSettings, &m_OtherSettings };

// ===============================
//  MENU ITEM DEFINITIONS
// ===============================
// NOTE: m_Main is declared extern in globals.h, but DEFINED here.

MenuItem m_Main            = { "Main Menu", nullptr, main_children, (uint8_t)(sizeof(main_children)/sizeof(main_children[0])), nullptr };
MenuItem m_ChargeLog       = { "Charge Log", &m_Main, chargelog_children, (uint8_t)(sizeof(chargelog_children)/sizeof(chargelog_children[0])), act_ChargeLog };
MenuItem m_EventLog        = { "Event Log", &m_Main, eventlog_children, (uint8_t)(sizeof(eventlog_children)/sizeof(eventlog_children[0])), act_EventLog };
MenuItem m_Readings        = { "Readings", &m_Main, readings_children, (uint8_t)(sizeof(readings_children)/sizeof(readings_children[0])), act_Readings };
MenuItem m_Information     = { "Information", &m_Main, information_children, (uint8_t)(sizeof(information_children)/sizeof(information_children[0])), act_Information };
MenuItem m_LinkedDevicesInfo = { "Linked Devices Info", &m_Main, information_LinkedDevices, (uint8_t)(sizeof(information_LinkedDevices)/sizeof(information_LinkedDevices[0])), act_LinkedDevices };
MenuItem m_ChargeSettings  = { "Charge Settings", &m_Main, chargesettings_children, (uint8_t)(sizeof(chargesettings_children)/sizeof(chargesettings_children[0])), nullptr };
MenuItem m_OtherSettings   = { "Other Settings", &m_Main, othersettings_children, (uint8_t)(sizeof(othersettings_children)/sizeof(othersettings_children[0])), nullptr };

// Charge Settings submenu
MenuItem m_ECOPLUS         = { "ECO+ Settings", &m_ChargeSettings, nullptr, 0, act_Default };
MenuItem m_ManualBoost     = { "Manual Boost", &m_ChargeSettings, nullptr, 0, act_ManualBoost };
MenuItem m_SmartBoost      = { "Smart Boost", &m_ChargeSettings, nullptr, 0, act_SmartBoost };
MenuItem m_BoostTimer      = { "Boost Timer", &m_ChargeSettings, nullptr, 0, act_BoostTimer };
MenuItem m_Preconditioning = { "Preconditioning", &m_ChargeSettings, nullptr, 0, act_Default };
MenuItem m_DefaultMode     = { "Default Mode", &m_ChargeSettings, nullptr, 0, act_Default };
MenuItem m_ECO_ECOPlus     = { "ECO / ECO+ Phases", &m_ChargeSettings, nullptr, 0, act_Default };

// Other Settings submenu
MenuItem m_TimeDate        = { "Time & Date", &m_OtherSettings, nullptr, 0, act_Default };
MenuItem m_DisplaySound    = { "Display & Sound", &m_OtherSettings, nullptr, 0, act_Default };
MenuItem m_LockFunc        = { "Lock Function", &m_OtherSettings, nullptr, 0, act_Default };
MenuItem m_DownloadFirmware = { "Download Firmware", &m_OtherSettings, nullptr, 0, act_Default };
MenuItem m_Internet        = { "Internet", &m_OtherSettings, nullptr, 0, act_Default };
MenuItem m_Advanced        = { "Advanced", &m_OtherSettings, nullptr, 0, act_Default };
MenuItem m_InstallerSettings = { "Installer Settings", &m_OtherSettings, nullptr, 0, act_Default };

// Advanced submenu
MenuItem m_SupplyGrid      = { "Supply Grid", &m_Advanced, nullptr, 0, act_Default };
MenuItem m_Network         = { "Network", &m_Advanced, nullptr, 0, act_Default };
MenuItem m_LinkedDevices   = { "Linked Devices", &m_Advanced, nullptr, 0, act_Default };
MenuItem m_CTConfig        = { "CT Config", &m_Advanced, nullptr, 0, act_Default };
MenuItem m_eSense          = { "eSense Input", &m_Advanced, nullptr, 0, act_Default };
MenuItem m_CompatMode      = { "Compatibility Mode", &m_Advanced, nullptr, 0, act_Default };
MenuItem m_System          = { "System", &m_Advanced, nullptr, 0, act_Default };
MenuItem m_DownloadFW      = { "Download Firmware", &m_Advanced, nullptr, 0, act_Default };

// Charge log submenu
MenuItem m_C_L_Today  = { "Today", &m_ChargeLog, nullptr, 0, act_ChargeLog };
MenuItem m_C_L_Yest   = { "Yesterday", &m_ChargeLog, nullptr, 0, act_ChargeLog };
MenuItem m_C_L_Week   = { "Week", &m_ChargeLog, nullptr, 0, act_ChargeLog };
MenuItem m_C_L_Month  = { "Month", &m_ChargeLog, nullptr, 0, act_ChargeLog };
MenuItem m_C_L_Year   = { "Year", &m_ChargeLog, nullptr, 0, act_ChargeLog };
MenuItem m_C_L_Total  = { "Total", &m_ChargeLog, nullptr, 0, act_ChargeLog };
MenuItem m_C_L_Custom = { "Custom", &m_ChargeLog, nullptr, 0, act_ChargeLog };

// Event log submenu
MenuItem m_E_L_Today  = { "Today", &m_ChargeLog, nullptr, 0, act_EventLog };
MenuItem m_E_L_Yest   = { "Yesterday", &m_ChargeLog, nullptr, 0, act_EventLog };
MenuItem m_E_L_Week   = { "Week", &m_ChargeLog, nullptr, 0, act_EventLog };
MenuItem m_E_L_Custom  = { "Custom", &m_ChargeLog, nullptr, 0, act_EventLog };

// Readings submenu
MenuItem m_Read1 = { "Readings 1/11", &m_Readings, nullptr, 0, act_Readings };
MenuItem m_Read2 = { "Readings 2/11", &m_Readings, nullptr, 0, act_Readings };
MenuItem m_Read3 = { "Readings 3/11", &m_Readings, nullptr, 0, act_Readings };
MenuItem m_Read4 = { "Readings 4/11", &m_Readings, nullptr, 0, act_Readings };
MenuItem m_Read5 = { "Readings 5/11", &m_Readings, nullptr, 0, act_Readings };
MenuItem m_Read6 = { "Readings 6/11", &m_Readings, nullptr, 0, act_Readings };
MenuItem m_Read7 = { "Readings 7/11", &m_Readings, nullptr, 0, act_Readings };
MenuItem m_Read8 = { "Readings 8/11", &m_Readings, nullptr, 0, act_Readings };
MenuItem m_Read9 = { "Readings 9/11", &m_Readings, nullptr, 0, act_Readings };
MenuItem m_Read10 = { "Readings 10/11", &m_Readings, nullptr, 0, act_Readings };
MenuItem m_Read11 = { "Readings 11/11", &m_Readings, nullptr, 0, act_Readings };

// Information submenu
MenuItem m_Info1 = { "Information 1/5", &m_Information, nullptr, 0, act_Information };
MenuItem m_Info2 = { "Information 2/5", &m_Information, nullptr, 0, act_Information };
MenuItem m_Info3 = { "Information 3/5", &m_Information, nullptr, 0, act_Information };
MenuItem m_Info4 = { "Information 4/5", &m_Information, nullptr, 0, act_Information };
MenuItem m_Info5 = { "Information 5/5", &m_Information, nullptr, 0, act_Information };

// Linked Devices submenu
MenuItem eddi = { "1 eddi  00000 W", &m_LinkedDevicesInfo, nullptr, 0, act_LinkedDevices };
MenuItem zappi = { "1-ZAPPI 00000 W~XM", &m_LinkedDevicesInfo, nullptr, 0, act_LinkedDevices };
MenuItem vhub = { "  vHub", &m_LinkedDevicesInfo, nullptr, 0, act_LinkedDevices };

// ===============================
//  LOGIC & NAVIGATION
// ===============================

void showMenu() {
  if (mqttClient.connected()) {
      // 1. Build the topic dynamically
      char topicBuf[128];
      snprintf(topicBuf, sizeof(topicBuf), "%s/sensor/menuActive/state", baseTopic);
      
      String payload = String(currentMenu->name);
      
      // 2. Use the buffer instead of "zappi/..."
      mqttClient.publish(topicBuf, 1, true, payload.c_str());
  }

  // WebSocket broadcast (unchanged)
  String json = "{";
  json += "\"menu_title\":\"" + String(currentMenu->name) + "\",";
  json += "\"items\":[";

  for (uint8_t i = 0; i < currentMenu->childCount; i++) {
    json += "{\"name\":\"" + String(currentMenu->children[i]->name) + "\",";
    json += "\"selected\":" + String(i == selectedIndex ? "true" : "false") + "}";
    if (i < currentMenu->childCount - 1) json += ",";
  }
  json += "]}";

  ws.textAll(json); 
}

void resetAllMenuPositions(MenuItem* menu) {
  if (menu == nullptr) return;
  menu->lastSelected = 0;
  for (uint8_t i = 0; i < menu->childCount; ++i) {
    resetAllMenuPositions(menu->children[i]);
  }
}

void resetMenuToOff() {
  menuActive = false;
  currentMenu = &m_Main;
  selectedIndex = 0;
  resetAllMenuPositions(&m_Main);

  ws.textAll("{\"event\":\"menu_closed\"}");

  if (mqttClient.connected()) {
    // 1. Build the topic dynamically
    char topicBuf[128];
    snprintf(topicBuf, sizeof(topicBuf), "%s/sensor/menuActive/state", baseTopic);
    
    // 2. Publish "OFF"
    mqttClient.publish(topicBuf, 1, true, "OFF");
  }

  extern TimerHandle_t menuTimeoutTimer; 
  if (menuTimeoutTimer != NULL) {
    xTimerStop(menuTimeoutTimer, 0);
  }
}

void handleEnterButton() {
  extern TimerHandle_t menuTimeoutTimer;
  extern unsigned long menuActiveStart;

  // Prepare the topic buffer once
  char topicBuf[128];
  snprintf(topicBuf, sizeof(topicBuf), "%s/sensor/menuActive/state", baseTopic);

  if (!menuActive) {
    // Activate Menu
    menuActive = true;
    if (menuTimeoutTimer != NULL) {
      xTimerStop(menuTimeoutTimer, 0);
      xTimerStart(menuTimeoutTimer, 0);
    }
    currentMenu = &m_Main;
    selectedIndex = 0;
    menuActiveStart = millis(); 
    showMenu();

    if (mqttClient.connected()) {
      mqttClient.publish(topicBuf, 1, true, "ON");
    }
    return;
  }

  // If current menu has a parent, go up
  if (currentMenu->parent != nullptr) {
    currentMenu->lastSelected = selectedIndex;
    currentMenu = currentMenu->parent;
    selectedIndex = currentMenu->lastSelected;
    showMenu();
    return;
  }

  // Close Menu
  menuActive = false;
  resetAllMenuPositions(&m_Main);
  selectedIndex = 0;
  ws.textAll("{\"event\":\"menu_closed\"}");

  if (mqttClient.connected()) {
    mqttClient.publish(topicBuf, 1, true, "OFF");
  }
}

void handleSelectButton() {
  if (!menuActive) return;
  MenuItem* next = currentMenu->children[selectedIndex];

  if (next->childCount == 0) {
    if (next->action) next->action();
    return;
  }

  currentMenu->lastSelected = selectedIndex;
  currentMenu = next;
  selectedIndex = currentMenu->lastSelected;
  showMenu();
}

void handleUpButton() {
  if (!menuActive || currentMenu->childCount == 0) return;
  selectedIndex = (selectedIndex == 0) ? currentMenu->childCount - 1 : selectedIndex - 1;
  showMenu();
}

void handleDownButton() {
  if (!menuActive || currentMenu->childCount == 0) return;
  selectedIndex = (selectedIndex + 1) % currentMenu->childCount;
  showMenu();
}