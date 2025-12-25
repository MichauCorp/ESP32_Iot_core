#include <ArduinoBLE.h>
#include <WiFi.h>
#include <Preferences.h>
#include <WebServer.h>

#define LED_PIN 2 
#define RESET_BUTTON_PIN 0 

Preferences preferences;
WebServer server(80);

// BLE Provisioning Configuration
#define PROV_SERVICE_UUID "19B10000-E8F2-537E-4F6C-D104768A1214"
BLEService provService(PROV_SERVICE_UUID);
BLEStringCharacteristic ssidChar("19B10001-E8F2-537E-4F6C-D104768A1214", BLEWrite, 32);
BLEStringCharacteristic passChar("19B10002-E8F2-537E-4F6C-D104768A1214", BLEWrite, 64);

bool isBLEActive = false;
bool ssidReceived = false; 
unsigned long lastWifiCheck = 0;
const unsigned long wifiCheckInterval = 10000;

// --- Helper: Visual Feedback ---
void blinkLED(int count, int delayMs) {
  for (int i = 0; i < count; i++) {
    digitalWrite(LED_PIN, HIGH);
    delay(delayMs);
    digitalWrite(LED_PIN, LOW); 
    delay(delayMs);
  }
}

// --- WiFi Watchdog ---
void checkWiFiHealth() {
  if (WiFi.status() != WL_CONNECTED && !isBLEActive) {
    Serial.println("\n[WATCHDOG] Connection lost!");
    WiFi.reconnect();
    
    int retries = 0;
    while (WiFi.status() != WL_CONNECTED && retries < 10) {
      digitalWrite(LED_PIN, HIGH); delay(100);
      digitalWrite(LED_PIN, LOW); delay(100);
      retries++;
    }

    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("[WATCHDOG] Reconnect failed. Reverting to BLE Mode.");
      startBLESetup();
    } else {
      Serial.println("[WATCHDOG] WiFi Restored.");
      digitalWrite(LED_PIN, LOW);
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n\n====================================");
  Serial.println("     UNIVERSAL IOT CORE CLEAN       ");
  Serial.println("====================================");

  pinMode(LED_PIN, OUTPUT);
  pinMode(RESET_BUTTON_PIN, INPUT_PULLUP);
  digitalWrite(LED_PIN, LOW); 

  preferences.begin("wifi-creds", true);
  String s = preferences.getString("ssid", "");
  String p = preferences.getString("pass", "");
  preferences.end();

  if (s != "") {
    Serial.printf("[INIT] Saved WiFi: %s. Connecting", s.c_str());
    WiFi.begin(s.c_str(), p.c_str());
    int timeout = 0;
    while (WiFi.status() != WL_CONNECTED && timeout < 20) { 
      digitalWrite(LED_PIN, !digitalRead(LED_PIN));
      delay(500); Serial.print("."); timeout++;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
      startWiFiServices();
    } else {
      Serial.println("\n[INIT] WiFi failed. Opening BLE.");
      startBLESetup();
    }
  } else {
    Serial.println("[INIT] No credentials found.");
    startBLESetup();
  }
}

void loop() {
  if (digitalRead(RESET_BUTTON_PIN) == LOW) {
    delay(3000);
    if (digitalRead(RESET_BUTTON_PIN) == LOW) {
      Serial.println("[SYSTEM] Resetting storage...");
      blinkLED(5, 100);
      preferences.begin("wifi-creds", false);
      preferences.clear();
      preferences.end();
      ESP.restart();
    }
  }

  if (isBLEActive) {
    BLEDevice central = BLE.central();
    if (central) {
      digitalWrite(LED_PIN, HIGH);
      Serial.println("[BLE] App connected.");
      while (central.connected()) {
        if (ssidChar.written() && !ssidReceived) {
          ssidReceived = true;
          Serial.println("[BLE] SSID Received: " + ssidChar.value());
          blinkLED(2, 200);
          digitalWrite(LED_PIN, HIGH);
        }
        if (passChar.written() && ssidReceived) {
          attemptProvisioning();
          break;
        }
      }
      if (isBLEActive && !central.connected()) {
        digitalWrite(LED_PIN, LOW);
        ssidReceived = false;
      }
    }
  }

  if (WiFi.status() == WL_CONNECTED) {
    server.handleClient();
    if (millis() - lastWifiCheck > wifiCheckInterval) {
      lastWifiCheck = millis();
      checkWiFiHealth();
    }
  } else if (!isBLEActive) {
    checkWiFiHealth();
  }
}

void startBLESetup() {
  if (BLE.begin()) {
    BLE.setLocalName("CORE-CONFIG-MODE");
    BLE.setAdvertisedService(provService);
    provService.addCharacteristic(ssidChar);
    provService.addCharacteristic(passChar);
    BLE.addService(provService);
    BLE.advertise();
    isBLEActive = true;
    ssidReceived = false;
    digitalWrite(LED_PIN, LOW);
  }
}

void attemptProvisioning() {
  String s = ssidChar.value();
  String p = passChar.value();
  WiFi.begin(s.c_str(), p.c_str());
  int t = 0;
  while (WiFi.status() != WL_CONNECTED && t < 20) { 
    digitalWrite(LED_PIN, !digitalRead(LED_PIN));
    delay(500); Serial.print("."); t++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    preferences.begin("wifi-creds", false);
    preferences.putString("ssid", s);
    preferences.putString("pass", p);
    preferences.end();
    blinkLED(3, 500);
    digitalWrite(LED_PIN, LOW);
    ESP.restart(); 
  } else {
    ssidReceived = false;
    blinkLED(10, 50);
  }
}

void startWiFiServices() {
  server.on("/status", HTTP_GET, [](){
    server.send(200, "application/json", "{\"status\":\"online\"}");
  });
  server.on("/reconfigure", HTTP_POST, [](){
    preferences.begin("wifi-creds", false);
    preferences.clear();
    preferences.end();
    server.send(200, "text/plain", "restarting");
    delay(100);
    ESP.restart();
  });
  server.begin();
  digitalWrite(LED_PIN, LOW);
  Serial.println("\n[WIFI] Online. IP: " + WiFi.localIP().toString());
}