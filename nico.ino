#include <WiFi.h>
#include <HTTPClient.h>
#include <WebSocketsClient.h>
#include <ArduinoJson.h>

///////////////////////////
// WiFi SETTINGS
///////////////////////////
#define MYWIFI_SSID "JNTU2"
#define MYWIFI_PWD   ""

// WiFi login portal URL
const char* loginUrl = "http://172.16.7.253:8090/login.xml";

///////////////////////////
// Supabase WebSocket SETTINGS
///////////////////////////
const char* supabaseHost = "uorbdplqtxmcdhbnkbmf.supabase.co";
const int supabasePort = 443;
const char* supabasePath = "/realtime/v1/websocket?apikey=eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJpc3MiOiJzdXBhYmFzZSIsInJlZiI6InVvcmJkcGxxdHhtY2RoYm5rYm1mIiwicm9sZSI6InNlcnZpY2Vfcm9sZSIsImlhdCI6MTc0MjYyMTQxMiwiZXhwIjoyMDU4MTk3NDEyfQ.7jcCez98a57cjrPAv7l_wc-rWin55Y_H80mw-G7swSw&log_level=info&vsn=1.0.0";

// LED Pin
#define LED_PIN 19

// WebSocket client
WebSocketsClient webSocket;

// Connection status tracking
bool wifiAuthenticated = false;
bool wsConnected = false;
unsigned long lastWifiCheckTime = 0;
unsigned long lastHeartbeatTime = 0;
const unsigned long WIFI_CHECK_INTERVAL = 60000;  // Check WiFi every 60 seconds
const unsigned long HEARTBEAT_INTERVAL = 30000;   // Send heartbeat every 30 seconds

// Phoenix Channel message counters
int joinRef = 1;
int messageRef = 1;

// Function to connect to WiFi
bool connectToWiFi() {
  Serial.printf("Connecting to %s...\n", MYWIFI_SSID);
  WiFi.begin(MYWIFI_SSID, MYWIFI_PWD);
  
  // Wait for connection with timeout
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connected!");
    Serial.printf("IP address: %s\n", WiFi.localIP().toString().c_str());
    return true;
  } else {
    Serial.println("\nFailed to connect to WiFi");
    return false;
  }
}

// Function to authenticate with WiFi portal
bool authenticateWiFi() {
  if (WiFi.status() != WL_CONNECTED) {
    return false;
  }
  
  HTTPClient http;
  http.begin(loginUrl);
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  http.addHeader("Accept", "*/*");
  http.addHeader("Accept-Language", "en-US,en;q=0.8");
  http.addHeader("Referer", "http://172.16.7.253:8090/httpclient.html");
  http.addHeader("Referrer-Policy", "strict-origin-when-cross-origin");

  // Prepare URL-encoded body
  String postData = "mode=191";
  postData += "&username=16011A0552";
  postData += "&password=jntu12345";
  postData += "&a=" + String(millis());  // Dynamic timestamp
  postData += "&producttype=0";

  // Send POST request
  Serial.println("Sending WiFi authentication request...");
  int httpResponseCode = http.POST(postData);

  // Check response
  bool success = false;
  if (httpResponseCode > 0) {
    Serial.printf("HTTP Response code: %d\n", httpResponseCode);
    String payload = http.getString();
    Serial.println("Response payload:");
    Serial.println(payload);
    
    // Check if authentication was successful
    // You may need to adjust this condition based on the actual response
    if (httpResponseCode == 200) {
      Serial.println("WiFi authentication successful!");
      success = true;
    } else {
      Serial.println("WiFi authentication failed!");
    }
  } else {
    Serial.printf("Error on sending POST: %s\n", http.errorToString(httpResponseCode).c_str());
  }

  // End connection
  http.end();
  return success;
}

// Function to handle WebSocket events
void webSocketEvent(WStype_t type, uint8_t * payload, size_t length) {
  switch(type) {
    case WStype_DISCONNECTED:
      Serial.println("[WSc] Disconnected!");
      wsConnected = false;
      break;
      
    case WStype_CONNECTED:
      Serial.println("[WSc] Connected to Supabase Realtime!");
      wsConnected = true;
      
      // Subscribe to the ledState channel
      subscribeToLedStateChannel();
      break;
      
    case WStype_TEXT:
      handleWebSocketMessage(payload, length);
      break;
      
    case WStype_ERROR:
      Serial.println("[WSc] Connection error!");
      break;
      
    case WStype_PING:
      // Automatically sends pong in response to ping
      Serial.println("[WSc] Received ping");
      break;
      
    case WStype_PONG:
      Serial.println("[WSc] Received pong");
      break;
  }
}

// Function to subscribe to the ledState channel
void subscribeToLedStateChannel() {
  // Create the Phoenix Channel join message
  StaticJsonDocument<512> doc;
  
  doc["topic"] = "realtime:ledState";
  doc["event"] = "phx_join";
  doc["join_ref"] = String(joinRef);
  doc["ref"] = String(messageRef);
  
  JsonObject payload = doc.createNestedObject("payload");
  JsonObject config = payload.createNestedObject("config");
  
  JsonObject broadcast = config.createNestedObject("broadcast");
  broadcast["self"] = true;
  
  JsonObject presence = config.createNestedObject("presence");
  presence["key"] = "";
  
  // Serialize to JSON string
  String jsonString;
  serializeJson(doc, jsonString);
  
  // Send the subscription message
  Serial.println("[WSc] Subscribing to ledState channel...");
  Serial.println(jsonString);
  webSocket.sendTXT(jsonString);
  
  // Increment message reference counters
  joinRef++;
  messageRef++;
}

// Function to send heartbeat to keep the connection alive
void sendHeartbeat() {
  // Create the Phoenix Channel heartbeat message
  StaticJsonDocument<256> doc;
  
  doc["topic"] = "phoenix";
  doc["event"] = "heartbeat";
  doc["join_ref"] = String(joinRef);
  doc["ref"] = String(messageRef);
  
  JsonObject payload = doc.createNestedObject("payload");
  
  // Serialize to JSON string
  String jsonString;
  serializeJson(doc, jsonString);
  
  // Send the heartbeat message
  Serial.println("[WSc] Sending heartbeat...");
  webSocket.sendTXT(jsonString);
  
  // Increment message reference counter
  messageRef++;
}

// Function to handle incoming WebSocket messages
void handleWebSocketMessage(uint8_t * payload, size_t length) {
  // Convert payload to string
  String message = String((char*)payload);
  Serial.println("[WSc] Received: " + message);
  
  // Parse the JSON message
  StaticJsonDocument<1024> doc;
  DeserializationError error = deserializeJson(doc, message);
  
  if (error) {
    Serial.print("JSON parsing error: ");
    Serial.println(error.c_str());
    return;
  }
  
  // Check if this is a broadcast message
  String event = doc["event"];
  
  if (event == "broadcast") {
    // Extract the payload
    JsonObject broadcastPayload = doc["payload"];
    
    // Check the event type in the payload
    if (broadcastPayload.containsKey("event")) {
      String eventName = broadcastPayload["event"];
      
      if (eventName == "ledOn") {
        // Turn on the LED
        digitalWrite(LED_PIN, HIGH);
        Serial.println("LED turned ON");
      } 
      else if (eventName == "ledOff") {
        // Turn off the LED
        digitalWrite(LED_PIN, LOW);
        Serial.println("LED turned OFF");
      }
    }
  }
  // Check if this is a reply to our join request
  else if (event == "phx_reply") {
    JsonObject replyPayload = doc["payload"];
    String status = replyPayload["status"];
    
    if (status == "ok") {
      Serial.println("Successfully joined ledState channel!");
    } else {
      Serial.println("Failed to join ledState channel!");
    }
  }
}

// Function to setup WebSocket connection
void setupWebSocket() {
  Serial.println("Setting up WebSocket connection to Supabase...");
  
  // Use beginSSL for secure WebSocket connections (wss://)
  webSocket.beginSSL(supabaseHost, supabasePort, supabasePath);
  

  
  // Set the callback function
  webSocket.onEvent(webSocketEvent);
  
  // Try to reconnect every 5000ms if connection has failed
  webSocket.setReconnectInterval(5000);
  
  Serial.println("WebSocket connection initialized");
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println(F("\n\n----- ESP32 WiFi Login & LED Controller (Direct WebSocket) -----"));
  
  // Initialize LED pin
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  Serial.println(F("LED initialized on pin 19"));
  
  // Connect to WiFi
  if (connectToWiFi()) {
    // Authenticate with WiFi portal
    wifiAuthenticated = authenticateWiFi();
    
    if (wifiAuthenticated) {
      // Setup WebSocket connection
      setupWebSocket();
    }
  }
}

void loop() {
  unsigned long currentMillis = millis();
  
  // Check WiFi connection periodically
  if (currentMillis - lastWifiCheckTime > WIFI_CHECK_INTERVAL) {
    lastWifiCheckTime = currentMillis;
    
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println(F("WiFi disconnected, reconnecting..."));
      wifiAuthenticated = false;
      wsConnected = false;
      
      if (connectToWiFi()) {
        wifiAuthenticated = authenticateWiFi();
        
        if (wifiAuthenticated) {
          setupWebSocket();
        }
      }
    } else if (!wifiAuthenticated) {
      // Try to authenticate if connected but not authenticated
      wifiAuthenticated = authenticateWiFi();
      
      if (wifiAuthenticated && !wsConnected) {
        setupWebSocket();
      }
    }
  }
  
  // Handle WebSocket connection
  if (WiFi.status() == WL_CONNECTED && wifiAuthenticated) {
    webSocket.loop();
    
    // Send heartbeat periodically to keep the connection alive
    if (wsConnected && (currentMillis - lastHeartbeatTime > HEARTBEAT_INTERVAL)) {
      lastHeartbeatTime = currentMillis;
      sendHeartbeat();
    }
  }
  
  // Small delay to prevent CPU hogging
  delay(10);
}