#include "web_server.h"
#include "globals.h"
#include "hardware.h"     // For handleBoost, handleButtons
#include "menu_system.h"  // For showMenu, resetMenuToOff
#include "wifi_mqtt.h"   
#include <LittleFS.h>
#include <WebSerial.h>
#include <esp_ota_ops.h>


// ===============================
//  WEBSOCKET HANDLERS
// ===============================

void broadcastStatus() {
  JsonDocument doc;
  doc["charger_status"] = charger_status;
  doc["car_connection_status"] = car_connection_status;
  doc["charging_state"] = charging_state_string;
  
  char jsonBuf[256];
  serializeJson(doc, jsonBuf, sizeof(jsonBuf));
  ws.textAll(jsonBuf);
}

void onWebSocketEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
  
  if (type == WS_EVT_CONNECT) {
    DEBUG_PRINTF("WebSocket client connected: %u\n", client->id());

    String json = "{";
    json += "\"event\":\"state_update\",";
    json += "\"boost_state\":\"" + String(boostSwitchState ? "ON" : "OFF") + "\",";
    json += "\"charger_status\":\"" + String(charger_status) + "\",";
    json += "\"car_connection_status\":\"" + String(car_connection_status) + "\",";
    json += "\"charging_state\":\"" + String(charging_state_string) + "\"";
    json += "}";
    client->text(json);

    if (menuActive) showMenu();

  } else if (type == WS_EVT_DISCONNECT) {
    DEBUG_PRINTF("WebSocket client #%u disconnected.\n", client->id());

  } else if (type == WS_EVT_DATA) {
    AwsFrameInfo *info = (AwsFrameInfo *)arg;
    if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
      String msg = String((char*)data).substring(0, len);
      if (msg.indexOf("request_state") > 0) {
        broadcastStatus();
      }
    }
  }
}

// ===============================
//  SERVER SETUP
// ===============================

void setupWebServer() {
  // 1. INITIALIZE LITTLEFS
  if(!LittleFS.begin()){
    DEBUG_PRINTLN("An Error has occurred while mounting LittleFS");
    //return;
  }

  // 2. INITIALIZE WEBSERIAL
  WebSerial.begin(&server);
  WebSerial.msgCallback([](uint8_t *data, size_t len){
      String msg = "";
      for(size_t i=0; i < len; i++) msg += char(data[i]);
      
      DEBUG_PRINTLN("User sent command: " + msg);
      
      // Existing Restart Command
      if(msg == "restart") {
        WebSerial.println("Restarting...");
        delay(100);
        ESP.restart();
      }

      // Roll back Command
      if (msg == "rollback") {
        WebSerial.println("Rolling back to previous firmware...");
        delay(500); // Give it time to print
        esp_ota_mark_app_invalid_rollback_and_reboot();
      }
      // ---------------------------
  });
  
  // CRITICAL: Tell the globals it is safe to print now
  webSerialEnabled = true;

  // 3. WEBSOCKET SETUP
  ws.onEvent(onWebSocketEvent);
  server.addHandler(&ws);

  // Serve HTML
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    // This looks for "index.html" in your data folder
    request->send(LittleFS, "/index.html", "text/html");
  });
  
  server.on("/favicon.ico", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(204);
  });

  // Commands
  server.on("/resetmode", HTTP_GET, [](AsyncWebServerRequest *request) {
    // You need to expose resetChargerModeToDefault in hardware.h or globals.h
    // Assuming you moved it to hardware.cpp and prototyped in hardware.h:
    extern void resetChargerModeToDefault(); 
    resetChargerModeToDefault();
    request->send(200, "text/plain", "Mode Reset to Stopped");
  });

  server.on("/restart", HTTP_GET, [](AsyncWebServerRequest *request) {
    restartTriggered = true;
    request->send(200, "text/plain", "ESP32 Restarting...");
    ws.textAll("{\"event\":\"restarting\"}");
    delay(500);
    ESP.restart();
  });

  // Buttons
  server.on("/enter", HTTP_GET, [](AsyncWebServerRequest *request) {
    char topic[128];
    snprintf(topic, sizeof(topic), "%s/switch/button1/state", baseTopic);
    handleButtons(topic, 0); 
    request->send(200, "text/plain", "Enter pressed");
  });
  
  server.on("/up", HTTP_GET, [](AsyncWebServerRequest *request) {
    char topic[128];
    snprintf(topic, sizeof(topic), "%s/switch/button2/state", baseTopic);
    handleButtons(topic, 1);
    request->send(200, "text/plain", "Up pressed");
  });

  server.on("/down", HTTP_GET, [](AsyncWebServerRequest *request) {
    char topic[128];
    snprintf(topic, sizeof(topic), "%s/switch/button3/state", baseTopic);
    handleButtons(topic, 2);
    request->send(200, "text/plain", "Down pressed");
  });

  server.on("/select", HTTP_GET, [](AsyncWebServerRequest *request) {
    char topic[128];
    snprintf(topic, sizeof(topic), "%s/switch/button4/state", baseTopic);
    handleButtons(topic, 3);
    request->send(200, "text/plain", "Select pressed");
  });

  server.on("/menuReset", HTTP_GET, [](AsyncWebServerRequest *request) {
    resetMenuToOff();
    request->send(200, "text/plain", "Menu reset to OFF");
  });

  server.on("/boost", HTTP_GET, [](AsyncWebServerRequest *request) {
    String state = handleBoost(); // Toggles the boolean variable
    
    // 1. Update Hardware
    if (boostSwitchState) digitalWrite(switchPin, HIGH);
    else digitalWrite(switchPin, LOW);
    
    // 2. Notify Web Clients (WebSocket)
    String msg = "{\"boost_state\":\"" + String(boostSwitchState ? "ON" : "OFF") + "\"}";
    ws.textAll(msg);
    
    // 3. Notify MQTT
    if (mqttClient.connected()) {
      char topic[128];
      snprintf(topic, sizeof(topic), "%s/switch/relay/state", baseTopic);
      mqttClient.publish(topic, 1, true, boostSwitchState ? "ON" : "OFF");
    }

    request->send(200, "text/plain", state);
  });

  // ==================================================
  //  THE "LAZY" FILE UPLOADER
  //  Access via: http://<IP_ADDRESS>/upload
  // ==================================================

  // 1. GET Request: Serve the UI with JS Progress Bar
  server.on("/upload", HTTP_GET, [](AsyncWebServerRequest *request){
    String html = R"rawliteral(
      <!DOCTYPE html>
      <html>
      <head>
        <meta name="viewport" content="width=device-width, initial-scale=1">
        <style>
          body { font-family: Arial, sans-serif; text-align: center; margin-top: 50px; background-color: #f4f4f4; }
          .container { background: white; max-width: 400px; margin: auto; padding: 20px; border-radius: 10px; box-shadow: 0px 0px 10px rgba(0,0,0,0.1); }
          h2 { color: #333; }
          #fileInput { margin-bottom: 20px; }
          button { background-color: #2b5cbe; color: white; border: none; padding: 10px 20px; border-radius: 5px; cursor: pointer; font-size: 16px; }
          button:hover { background-color: #1a3a7a; }
          
          /* Progress Bar Styles */
          #progressContainer { width: 100%; background-color: #ddd; border-radius: 5px; margin-top: 20px; display: none; }
          #progressBar { width: 0%; height: 25px; background-color: #4CAF50; border-radius: 5px; text-align: center; line-height: 25px; color: white; transition: width 0.2s; }
          #status { margin-top: 15px; font-weight: bold; color: #555; }
        </style>
      </head>
      <body>
        <div class="container">
          <h2>Update UI (index.html)</h2>
          <p>Select your new HTML file to upload.</p>
          
          <input type="file" id="fileInput" accept=".html,.js,.css">
          <br>
          <button onclick="uploadFile()">Upload File</button>
          
          <div id="progressContainer">
            <div id="progressBar">0%</div>
          </div>
          <div id="status"></div>
        </div>

        <script>
          function uploadFile() {
            var fileInput = document.getElementById("fileInput");
            var file = fileInput.files[0];
            
            if (!file) {
              alert("Please select a file first!");
              return;
            }

            var formdata = new FormData();
            formdata.append("data", file);
            
            var ajax = new XMLHttpRequest();
            
            // --- PROGRESS EVENT ---
            ajax.upload.addEventListener("progress", function(event) {
              var percent = Math.round((event.loaded / event.total) * 100);
              
              document.getElementById("progressContainer").style.display = "block";
              var bar = document.getElementById("progressBar");
              bar.style.width = percent + "%";
              bar.innerText = percent + "%";
              document.getElementById("status").innerText = "Uploading... " + percent + "%";
            }, false);
            
            // --- COMPLETE EVENT ---
            ajax.addEventListener("load", function(event) {
              document.getElementById("progressBar").style.backgroundColor = "green";
              document.getElementById("status").innerText = "Upload Complete! Rebooting...";
              // Reload the main page after 5 seconds to show the new UI
              setTimeout(function(){ window.location.href = "/"; }, 5000);
            }, false);
            
            // --- ERROR EVENT ---
            ajax.addEventListener("error", function(event) {
              document.getElementById("status").innerText = "Upload Failed";
              document.getElementById("progressBar").style.backgroundColor = "red";
            }, false);
            
            // --- ABORT EVENT ---
            ajax.addEventListener("abort", function(event) {
              document.getElementById("status").innerText = "Upload Aborted";
            }, false);
            
            ajax.open("POST", "/upload");
            ajax.send(formdata);
          }
        </script>
      </body>
      </html>
    )rawliteral";
    request->send(200, "text/html", html);
  });

  // 2. POST Request: Handle the file stream and save to LittleFS
  server.on("/upload", HTTP_POST, [](AsyncWebServerRequest *request){
    request->send(200, "text/plain", "File Uploaded Successfully! Rebooting...");
    delay(1000);
    ESP.restart(); // Reboot to ensure the new file is served correctly
  }, 
  [](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final){
    // This function runs repeatedly as data packets arrive
    
    if(!index){
      // First packet: Create/Open the file
      // Force filename to index.html if you want to be safe, 
      // or use "filename" to allow uploading anything.
      
      // OPTION A: Always overwrite index.html (Safest for this specific task)
      String saveName = "/index.html"; 
      
      // OPTION B: Save with original filename (Good if you upload style.css separate)
      // if(!filename.startsWith("/")) filename = "/" + filename;
      // String saveName = filename;

      DEBUG_PRINTLN("Upload Start: " + saveName);
      request->_tempFile = LittleFS.open(saveName, "w");
    }
    
    if(request->_tempFile){
      // Write current packet
      request->_tempFile.write(data, len);
    }
    
    if(final){
      // Last packet: Close file
      if(request->_tempFile){
        request->_tempFile.close();
      }
      DEBUG_PRINTLN("Upload Complete: " + filename);
    }
  });

  server.begin();
  DEBUG_PRINTLN("Web server started");
  
}