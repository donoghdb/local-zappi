#ifndef GLOBALS_H
#define GLOBALS_H

#include <Arduino.h>
#include <WiFi.h>
#include <AsyncMqttClient.h>
#include <ESPAsyncWebServer.h>
#include <Preferences.h>
#include "menu_system.h" 
#include <WebSerial.h>

// ===============================
//  DEBUG SETTINGS
// ===============================
#define DEBUG1 1   
#define DEBUG2 1   

#if DEBUG1
  #define DEBUG_PRINT(x)    { Serial.print(x); if(webSerialEnabled) WebSerial.print(x); }
  #define DEBUG_PRINTLN(x)  { Serial.println(x); if(webSerialEnabled) WebSerial.println(x); }
#else
  #define DEBUG_PRINT(x)
  #define DEBUG_PRINTLN(x)
#endif

#if DEBUG2
  #define DEBUG_PRINTF(...) { Serial.printf(__VA_ARGS__); if(webSerialEnabled) WebSerial.printf(__VA_ARGS__); }
#else
  #define DEBUG_PRINTF(...)
#endif


// ===== Enums =====
enum ChargingState {
    STATE_A,
    STATE_B,
    STATE_C,
    STATE_D,
    STATE_E,
    STATE_F
};

// ===== Extern Globals =====

// Add the external flag declaration
extern bool webSerialEnabled;

// Preferences
extern Preferences prefs;
extern portMUX_TYPE synch; 

// Pins
extern const int switchPin;
extern const int buttonPins[];
extern const int returnButtonPins[];
extern const int numReturnButtons;
extern const unsigned long buttonPressDuration;
extern unsigned long buttonPressTime[];

extern const int no_trys_connect_wifi;
extern const int adcPin;
extern const int adcInterruptPin;
extern const int sampleInterval;
extern const int sampleInterval_dia;
extern const int numSamples;
extern const int updateFrequencyDutyCycle;

// Monitoring
extern unsigned long lastHeapPrint;

// Timing state
extern unsigned long lastSampleTimeDutyCycle;
extern unsigned long lastDataTime;

// Pulse measurement
extern volatile bool pulseDetected;
extern volatile unsigned long pulseStart;
extern volatile unsigned long pulseEnd;

extern volatile int activeMenuIndex;

extern volatile bool returnTriggered[];
extern volatile unsigned long returnTriggerTime[];
extern volatile bool ignoreNextReturn[];
extern volatile unsigned long maskUntil[];
extern const unsigned long maskDuration;
extern const TickType_t maskDurationTicks;

// Button state
extern bool boostSwitchState;
extern bool restartTriggered;

// Menu system
extern MenuItem m_Main;
extern MenuItem* currentMenu;
extern uint8_t selectedIndex;
extern bool menuActive;
extern unsigned long menuActiveStart;
extern const unsigned long MENU_TIMEOUT; 

// Device Info
extern const char* baseTopic;
extern const char* haPrefix;
extern const char* deviceName;
extern const char* deviceIdentifiers;
extern const char* deviceModel;
extern const char* deviceManufacturer;
extern const char* deviceSwVersion;

extern String charging_state_string;
extern String car_connection_status;
extern String icon_car;
extern String charger_status;

// MQTT
extern AsyncMqttClient mqttClient;
extern TimerHandle_t mqttReconnectTimer;
extern TimerHandle_t wifiReconnectTimer;
extern WiFiEventId_t wifiConnectHandler;
extern WiFiEventId_t wifiDisconnectHandler;

// Web Server
extern AsyncWebServer server;
extern AsyncWebSocket ws;

// Counter / Charger mode
extern int currentMode;
extern const int maxCount;
extern int direction;
extern volatile bool counterDirty;

// Charging State Data
extern ChargingState newState;
extern ChargingState oldState;
extern bool charging_state;
extern bool boot_callback; 

// --- SCHEDULE VARIABLES (Extern) ---
extern bool schedEnabled;
extern int schedStartHour;
extern int schedStartMin;
extern int schedEndHour;
extern int schedEndMin;

// ===== MQTT Topics =====
#define TOPIC_BUF_LEN 128

extern char discoveryTopic[TOPIC_BUF_LEN];
extern char deviceTopic[TOPIC_BUF_LEN];

extern char dutyCycleStateTopicConfig[TOPIC_BUF_LEN];
extern char dutyCycleStateTopic[TOPIC_BUF_LEN];

extern char dutyCycleAmpsStateTopicConfig[TOPIC_BUF_LEN];
extern char dutyCycleAmpsStateTopic[TOPIC_BUF_LEN];

extern char chargingStateTopicConfig[TOPIC_BUF_LEN];
extern char chargingStateTopic[TOPIC_BUF_LEN];

extern char chargerStateTopicConfig[TOPIC_BUF_LEN];
extern char chargerStateTopic[TOPIC_BUF_LEN];

extern char carStateTopicConfig[TOPIC_BUF_LEN];
extern char carStateTopic[TOPIC_BUF_LEN];

extern char charger_rebootConfig[TOPIC_BUF_LEN];
extern char charger_reboot[TOPIC_BUF_LEN];

extern char zappi_charging_stateConfig[TOPIC_BUF_LEN];
extern char zappi_charging_state[TOPIC_BUF_LEN];

extern char menuResetTopicConfig[TOPIC_BUF_LEN];
extern char menuResetTopic[TOPIC_BUF_LEN];

extern char chargerResetTopicConfig[TOPIC_BUF_LEN];
extern char chargerResetTopic[TOPIC_BUF_LEN];

// ===== Function Prototypes =====
void setupTopics();
void handleEnterButton();
void handleUpButton();
void handleDownButton();
void handleSelectButton();
void resetMenuToOff();
void updateChargerState(int &count);
void publishChargingState(ChargingState &newState);
void broadcastStatus();
void handleButtons(const char *topic, int index);
void MqttDiscoveryInitial();

#endif // GLOBALS_H