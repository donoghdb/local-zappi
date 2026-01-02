#include "globals.h"

// ===== Mutex =====
portMUX_TYPE synch = portMUX_INITIALIZER_UNLOCKED;

// ===== Preferences =====
Preferences prefs;

// ===== Pins  =====
const int switchPin = 17;

const int buttonPins[] = {7, 5, 6, 4}; // GPIO pins for the buttons
const int returnButtonPins[] = {12, 10, 11, 9}; // 
const int numReturnButtons = 4;
const unsigned long buttonPressDuration = 100; //   
unsigned long buttonPressTime[4] = {0, 0, 0, 0};

const int no_trys_connect_wifi = 20; // 

const int adcPin = 8;          // GPIO 8 for ADC measurement
const int adcInterruptPin = 3; // GPIO 3 for ADC interrupt measurement

const int sampleInterval = 250;       // Interval between samples in milliseconds
const int sampleInterval_dia = 30000; //    Interval between samples in milliseconds
const int numSamples = 250;

const int updateFrequencyDutyCycle = 2000; // 2000 ms

// ===== Timing State =====
unsigned long lastSampleTimeDutyCycle = 0;
unsigned long lastDataTime = 0;

// ===== Pulse Measurement =====
volatile bool pulseDetected = false;
volatile unsigned long pulseStart = 0;
volatile unsigned long pulseEnd = 0;

volatile int activeMenuIndex = -1;

// ===== Return Button ISR State =====
volatile bool returnTriggered[4] = {false, false, false, false};
volatile unsigned long returnTriggerTime[4] = {0, 0, 0, 0};
volatile bool ignoreNextReturn[4] = {false, false, false, false};
volatile unsigned long maskUntil[4] = {0, 0, 0, 0};

const unsigned long maskDuration = 100; // ms
const TickType_t maskDurationTicks = pdMS_TO_TICKS(100);

// ===== Button State =====
bool boostSwitchState = false;
bool restartTriggered = false;

// ===== Menu System =====
MenuItem* currentMenu = nullptr;
uint8_t selectedIndex = 0;
bool menuActive = false;
unsigned long menuActiveStart = 0;
const unsigned long MENU_TIMEOUT = 3600000UL; // 1 Hour
bool webSerialEnabled = false;

// ===== Device Info  =====
const char* baseTopic = "zappi";
const char* haPrefix = "homeassistant";
const char* deviceName = "Zappi";          // UPDATED
const char* deviceIdentifiers = "zappi_device"; // UPDATED
const char* deviceModel = "Zappi Local";   // UPDATED
const char* deviceManufacturer = "Not-Myenergi"; // UPDATED
const char* deviceSwVersion = "2.0";       // UPDATED

// ===== MQTT System =====
AsyncMqttClient mqttClient;
TimerHandle_t mqttReconnectTimer = nullptr;
TimerHandle_t wifiReconnectTimer = nullptr;
WiFiEventId_t wifiConnectHandler;
WiFiEventId_t wifiDisconnectHandler;

// ===== Web Server =====
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

// ===== Counter / Mode =====
int currentMode = 1; // 1=Stopped, 2=Fast, 3=Eco, 4=Eco+, 5=MEM (out of bounds, handled in logic)
const int maxCount = 4; 
int direction = 1;
volatile bool counterDirty = false;
unsigned long lastHeapPrint = 0; // For monitoring heap usage   

// ===== Charging State =====
ChargingState newState = STATE_A;
ChargingState oldState = STATE_A;

bool charging_state = false;
bool boot_callback = false;

String charging_state_string = "not_charging";
String car_connection_status = "disconnected";
String icon_car = "mdi:car";
String charger_status = "idle";

// ===== MQTT Topic Buffers =====
char discoveryTopic[128];
char deviceTopic[128];
char dutyCycleStateTopicConfig[128];
char dutyCycleStateTopic[128];
char dutyCycleAmpsStateTopicConfig[128];
char dutyCycleAmpsStateTopic[128];
char chargingStateTopicConfig[128];
char chargingStateTopic[128];
char chargerStateTopicConfig[128];
char chargerStateTopic[128];
char carStateTopicConfig[128];
char carStateTopic[128];
char charger_rebootConfig[128];
char charger_reboot[128];
char zappi_charging_stateConfig[128];
char zappi_charging_state[128];
char menuResetTopicConfig[128];
char menuResetTopic[128];
char chargerResetTopicConfig[128];
char chargerResetTopic[128];