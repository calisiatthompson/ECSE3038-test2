
#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <PubSubClient.h>
#include <DHT.h>
#include <ArduinoJson.h>
#include <math.h>

#include "secrets.h"

// Hardware
#define DHT_PIN 15
#define DHT_TYPE DHT22
#define FAN_PIN 2

DHT dht(DHT_PIN, DHT_TYPE);

// WiFi
const char* WIFI_SSID = "Wokwi-GUEST";
const char* WIFI_PASSWORD = "";

// Azure API
const char* API_URL =
    "http://iot-calisia.northcentralus.cloudapp.azure.com/readings";

// MQTT
const char* MQTT_BROKER = "135.232.203.144";
const int MQTT_PORT = 1883;

const char* TOPIC_SETTINGS =
    "ecse3038/calisia/fan/settings";

const char* TOPIC_STATE =
    "ecse3038/calisia/fan/state";

const char* TOPIC_STATUS =
    "ecse3038/calisia/status";

WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);

String macAddress;

// Fan settings
String fanMode = "off";
float maxHumidity = 65.0;
float currentHumidity = NAN;

bool settingsReceived = false;
bool fanOn = false;
bool statePending = false;

// Timing
unsigned long lastReading = 0;
unsigned long lastMQTTAttempt = 0;

const unsigned long INTERVAL = 10000;


// B5: Publish fan state only when it changes
void publishFanState() {

    if (statePending && mqtt.connected()) {

        bool sent = mqtt.publish(
            TOPIC_STATE,
            fanOn ? "on" : "off",
            true
        );

        if (sent) {
            statePending = false;
        }
    }
}


// B4: Control the fan
void updateFan() {

    bool newState = false;

    // Fan stays off until settings arrive
    if (settingsReceived) {

        if (fanMode == "on") {
            newState = true;
        }

        else if (fanMode == "off") {
            newState = false;
        }

        else if (fanMode == "auto") {

            if (!isnan(currentHumidity)) {

                newState =
                    currentHumidity > maxHumidity;
            }
        }
    }

    // Change the LED only when necessary
    if (newState != fanOn) {

        fanOn = newState;

        digitalWrite(
            FAN_PIN,
            fanOn ? HIGH : LOW
        );

        statePending = true;

        Serial.print("Fan: ");
        Serial.println(fanOn ? "on" : "off");
    }

    publishFanState();
}


// B3: Receive and validate MQTT JSON
void mqttCallback(
    char* topic,
    byte* payload,
    unsigned int length
) {

    if (String(topic) != TOPIC_SETTINGS) {
        return;
    }

    StaticJsonDocument<256> doc;

    DeserializationError error =
        deserializeJson(doc, payload, length);

    // Reject invalid JSON
    if (error) {

        Serial.println("Invalid settings JSON");
        return;
    }

    // Reject missing fields
    if (!doc.containsKey("mode") ||
        !doc.containsKey("max_humidity")) {

        Serial.println("Missing settings field");
        return;
    }

    // Check the field types
    if (!doc["mode"].is<const char*>() ||
        !doc["max_humidity"].is<float>()) {

        Serial.println("Invalid settings type");
        return;
    }

    String newMode =
        doc["mode"].as<const char*>();

    float newLimit =
        doc["max_humidity"].as<float>();

    // Validate mode
    if (newMode != "auto" &&
        newMode != "on" &&
        newMode != "off") {

        Serial.println("Invalid fan mode");
        return;
    }

    // Validate humidity limit
    if (!isfinite(newLimit) ||
        newLimit < 0 ||
        newLimit > 100) {

        Serial.println("Invalid humidity limit");
        return;
    }

    // Update settings only after validation
    fanMode = newMode;
    maxHumidity = newLimit;
    settingsReceived = true;

    Serial.print("Mode: ");
    Serial.println(fanMode);

    Serial.print("Maximum humidity: ");
    Serial.println(maxHumidity);

    // React immediately to new settings
    updateFan();
}


// B2: Connect to MQTT with Last Will
void connectMQTT() {

    if (WiFi.status() != WL_CONNECTED) {
        return;
    }

    String clientId =
        "bathroom-" + macAddress;

    bool connected = mqtt.connect(
        clientId.c_str(),
        TOPIC_STATUS,
        0,
        true,
        "offline"
    );

    if (connected) {

        Serial.println("MQTT connected");

        mqtt.publish(
            TOPIC_STATUS,
            "online",
            true
        );

        mqtt.subscribe(TOPIC_SETTINGS);

        publishFanState();

    } else {

        Serial.print("MQTT connection failed: ");
        Serial.println(mqtt.state());
    }
}


// B1: Send humidity readings every 10 seconds
void sendReading() {

    float humidity = dht.readHumidity();

    if (isnan(humidity)) {

        Serial.println("DHT22 read failed");
        return;
    }

    currentHumidity = humidity;

    Serial.print("Humidity: ");
    Serial.println(currentHumidity);

    updateFan();

    // Create JSON
    StaticJsonDocument<128> doc;

    doc["mac"] = macAddress;
    doc["humidity"] = currentHumidity;

    String body;
    serializeJson(doc, body);

    // Send HTTP POST
    if (WiFi.status() == WL_CONNECTED) {

        HTTPClient http;

        http.begin(API_URL);
        http.setTimeout(3000);

        http.addHeader(
            "Content-Type",
            "application/json"
        );

        http.addHeader(
            "X-API-Key",
            DEVICE_KEY
        );

        int status = http.POST(body);

        Serial.print("HTTP status: ");
        Serial.println(status);

        http.end();
    }
}


void setup() {

    Serial.begin(115200);

    pinMode(FAN_PIN, OUTPUT);
    digitalWrite(FAN_PIN, LOW);

    dht.begin();

    // Connect to WiFi
    WiFi.begin(
        WIFI_SSID,
        WIFI_PASSWORD
    );

    WiFi.setAutoReconnect(true);

    while (WiFi.status() != WL_CONNECTED) {

        delay(500);
        Serial.print(".");
    }

    Serial.println("\nWiFi connected");

    // Obtain MAC address
    macAddress = WiFi.macAddress();

    Serial.print("MAC: ");
    Serial.println(macAddress);

    // Initial humidity
    currentHumidity = dht.readHumidity();

    // Configure MQTT
    mqtt.setServer(
        MQTT_BROKER,
        MQTT_PORT
    );

    mqtt.setCallback(mqttCallback);

    mqtt.setKeepAlive(15);
    mqtt.setSocketTimeout(3);

    connectMQTT();
}


void loop() {

    unsigned long now = millis();

    // Reconnect to MQTT if disconnected
    if (!mqtt.connected()) {

        if (now - lastMQTTAttempt >= 5000) {

            lastMQTTAttempt = now;

            connectMQTT();
        }
    }

    // Process incoming MQTT messages
    mqtt.loop();

    publishFanState();

    // Send humidity every 10 seconds
    if (now - lastReading >= INTERVAL) {

        lastReading = now;

        sendReading();
    }
}
