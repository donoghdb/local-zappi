#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include "globals.h"

// --- Setup ---
void setupWebServer();

// --- WebSocket Handlers ---
void onWebSocketEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len);
void broadcastStatus(); // Sends the JSON status update to all clients

#endif // WEB_SERVER_H