#ifndef CREDENTIALS_H
#define CREDENTIALS_H

#include <Arduino.h> // for IPAddress

extern const char* ssid;
extern const char* password;

extern const char* mqttServer;
extern const int mqttPort;
extern const char* mqttUser;
extern const char* mqttPassword;
extern const char* mqttClientId;

extern IPAddress staticIP;
extern IPAddress gateway;
extern IPAddress subnet;

#endif // CREDENTIALS_H