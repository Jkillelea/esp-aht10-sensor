#include <AHT10.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ArduinoJson.h>
#include <ArduinoOTA.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>
#include <LittleFS.h>
#include <PubSubClient.h>
#include <WiFiManager.h>
#include <cstdio>
#include <cstring>
#include <debugstream.hpp>
#include <iterator>
#include <memory>
#include <vector>

uint8_t mqttRetryCounter = 0;

Adafruit_SSD1306 display(128, 32, &Wire, -1); // pixels
AHT10 aht;
WiFiManager wifiManager;
WiFiClient wifiClient;
PubSubClient mqttClient;
DebugStream debug(&Serial);

char ssid[80];
char pass[80];
char mqtt_server[80];

WiFiManagerParameter mqtt_server_param("server", "MQTT Server", mqtt_server,
        sizeof(mqtt_server));

uint32_t lastMqttConnectionAttempt = 0;

// 1 minute = 60 seconds = 60000 milliseconds
const uint16_t mqttConnectionInterval = 60000;

const uint16_t PUBLISH_INTERVAL = 30000; // 30 seconds = 30000 milliseconds

char identifier[24];

#define FIRMWARE_PREFIX "aht10-env-sensor"
#define AVAILABILITY_ONLINE "online"
#define AVAILABILITY_OFFLINE "offline"
char MQTT_TOPIC_AVAILABILITY[128];
char MQTT_TOPIC_STATE[128];
char MQTT_TOPIC_COMMAND[128];

char MQTT_TOPIC_AUTOCONF_WIFI_SENSOR[128];
char MQTT_TOPIC_AUTOCONF_PM25_SENSOR[128];

void resetDisplayText(Adafruit_SSD1306 &display) {
    display.clearDisplay();
    display.setTextColor(WHITE);
    display.setCursor(0, 0);
    display.setTextSize(1);
}

void publishAutoConfig() {
    char mqttPayload[2048];
    DynamicJsonDocument device(256);
    DynamicJsonDocument autoconfPayload(1024);
    StaticJsonDocument<64> identifiersDoc;
    JsonArray identifiers = identifiersDoc.to<JsonArray>();

    identifiers.add(identifier);

    device["identifiers"] = identifiers;
    // device["manufacturer"] = "Ikea";
    device["model"] = "AHT10";
    device["name"] = identifier;
    device["sw_version"] = "2022.06.14";

    autoconfPayload["device"] = device.as<JsonObject>();
    autoconfPayload["availability_topic"] = MQTT_TOPIC_AVAILABILITY;
    autoconfPayload["state_topic"] = MQTT_TOPIC_STATE;
    autoconfPayload["name"] = identifier + String(" WiFi");
    autoconfPayload["value_template"] = "{{value_json.wifi.rssi}}";
    autoconfPayload["unique_id"] = identifier + String("_wifi");
    autoconfPayload["unit_of_measurement"] = "dBm";
    autoconfPayload["json_attributes_topic"] = MQTT_TOPIC_STATE;
    autoconfPayload["json_attributes_template"] =
        "{\"ssid\": \"{{value_json.wifi.ssid}}\", \"ip\": "
        "\"{{value_json.wifi.ip}}\"}";
    autoconfPayload["icon"] = "mdi:wifi";

    serializeJson(autoconfPayload, mqttPayload);
    mqttClient.publish(&MQTT_TOPIC_AUTOCONF_WIFI_SENSOR[0], &mqttPayload[0],
            true);

    autoconfPayload.clear();

    autoconfPayload["device"] = device.as<JsonObject>();
    autoconfPayload["availability_topic"] = MQTT_TOPIC_AVAILABILITY;
    autoconfPayload["state_topic"] = MQTT_TOPIC_STATE;
    autoconfPayload["name"] = identifier + String(" PM 2.5");
    autoconfPayload["unit_of_measurement"] = "μg/m³";
    autoconfPayload["value_template"] = "{{value_json.pm25}}";
    autoconfPayload["unique_id"] = identifier + String("_pm25");
    autoconfPayload["icon"] = "mdi:air-filter";

    serializeJson(autoconfPayload, mqttPayload);
    mqttClient.publish(&MQTT_TOPIC_AUTOCONF_WIFI_SENSOR[0], &mqttPayload[0],
            true);

    autoconfPayload.clear();

    autoconfPayload["device"] = device.as<JsonObject>();
    autoconfPayload["availability_topic"] = MQTT_TOPIC_AVAILABILITY;
    autoconfPayload["state_topic"] = MQTT_TOPIC_STATE;
    autoconfPayload["name"] = identifier + String(" Degrees C");
    autoconfPayload["unit_of_measurement"] = "C";
    autoconfPayload["value_template"] = "{{value_json.degC}}";
    autoconfPayload["unique_id"] = identifier + String("_degC");
    autoconfPayload["icon"] = "mdi:thermometer";

    serializeJson(autoconfPayload, mqttPayload);
    mqttClient.publish(&MQTT_TOPIC_AUTOCONF_WIFI_SENSOR[0], &mqttPayload[0],
            true);

    autoconfPayload.clear();

    autoconfPayload["device"] = device.as<JsonObject>();
    autoconfPayload["availability_topic"] = MQTT_TOPIC_AVAILABILITY;
    autoconfPayload["state_topic"] = MQTT_TOPIC_STATE;
    autoconfPayload["name"] = identifier + String(" Relative Humidity");
    autoconfPayload["unit_of_measurement"] = "Percent RH";
    autoconfPayload["value_template"] = "{{value_json.relHumid}}";
    autoconfPayload["unique_id"] = identifier + String("_relHumid");
    autoconfPayload["icon"] = "mdi:thermometer";

    serializeJson(autoconfPayload, mqttPayload);
    mqttClient.publish(&MQTT_TOPIC_AUTOCONF_PM25_SENSOR[0], &mqttPayload[0],
            true);

    autoconfPayload.clear();
}

void saveConfigCallback() {
    debug.println("Saving config");
    // Minimum reservation size in LittleFS is 4K?
    DynamicJsonDocument config_json(512);

    // Store Wifi SSID and password in plaintext lol
    config_json["mqtt_server"] = mqtt_server_param.getValue();

    File config = LittleFS.open("/config.json", "w");
    if (config) {
        debug.println(config_json.as<String>());
        serializeJson(config_json, config);
        config.close();
    }
}

void mqttCallback(char *topic, uint8_t *payload, unsigned int length) {
    debug.printf("mqttCallback: %s: %s (%d)\n", topic, payload, length);
}

void configModeCallback(WiFiManager *myWiFiManager) {
    debug.println("Entered config mode");
    debug.println(WiFi.softAPIP());

    debug.println(myWiFiManager->getConfigPortalSSID());
}

void setupOTA() {
    display.println("Setup OTA...");
    display.display();

    ArduinoOTA.onStart([]() { debug.println("OTA Start"); });
    ArduinoOTA.onEnd([]() { debug.println("\nOTA End"); });

    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
            debug.printf("OTA Progress: %u%%\r", (progress / (total / 100)));
            });

    ArduinoOTA.onError([](ota_error_t error) {
            debug.printf("Error[%u]: ", error);
            if (error == OTA_AUTH_ERROR) {
            debug.println("Auth Failed");
            } else if (error == OTA_BEGIN_ERROR) {
            debug.println("Begin Failed");
            } else if (error == OTA_CONNECT_ERROR) {
            debug.println("Connect Failed");
            } else if (error == OTA_RECEIVE_ERROR) {
            debug.println("Receive Failed");
            } else if (error == OTA_END_ERROR) {
            debug.println("End Failed");
            }
            });

    ArduinoOTA.setHostname(identifier);

    // This is less of a security measure and more a accidential flash prevention
    // ArduinoOTA.setPassword(identifier);
    ArduinoOTA.begin();
}

void mqttReconnect() {
    display.println("Reconnect MQTT...");
    display.display();

    debug.println("Reconnect MQTT...");

    if (std::strcmp(mqtt_server_param.getValue(), "") == 0) {
        // No values? Reset and request from user
        debug.printf("Missing MQTT server, resetting and asking user.\n");

        resetDisplayText(display);
        display.println("Connect to ESP wifi...");
        display.display();

        wifiManager.resetSettings();
        wifiManager.autoConnect();
    }

    for (uint8_t attempt = 0; attempt < 10; attempt++) {
        debug.printf("Attempt %d\n", attempt);
        debug.printf("mqtt: %s\n", mqtt_server);
        if (mqttClient.connect(identifier, "", "", MQTT_TOPIC_AVAILABILITY, 1, true,
                    AVAILABILITY_OFFLINE)) {
            debug.println("Mqtt connected");
            mqttClient.publish(MQTT_TOPIC_AVAILABILITY, AVAILABILITY_ONLINE, true);
            publishAutoConfig();

            // Make sure to subscribe after polling the status so that we never
            // execute commands with the default data
            mqttClient.subscribe(MQTT_TOPIC_COMMAND);
            break;
        }
        delay(3000);
    }
}

void publishState(float degC, float relHumid) {
    DynamicJsonDocument wifiJson(192);
    DynamicJsonDocument stateJson(604);
    resetDisplayText(display);
    display.printf("%2.1f C\n", degC);
    display.printf("%3.1f %% RH\n", relHumid);

    char payload[256];

    wifiJson["ssid"] = WiFi.SSID();
    wifiJson["ip"]   = WiFi.localIP().toString();
    wifiJson["rssi"] = WiFi.RSSI();

    stateJson["degC"] = degC;
    stateJson["relHumid"] = relHumid;
    stateJson["wifi"] = wifiJson.as<JsonObject>();

    serializeJson(stateJson, payload, sizeof(payload));
    debug.println("Publish");
    debug.println(payload);
    mqttClient.publish(MQTT_TOPIC_STATE, payload, true);
}

void setup() {
    Serial.begin(921600);
    aht.begin();
    display.begin();
    resetDisplayText(display);

    debug.println("\n");
    debug.println("Hello from esp8266-vindriktning-particle-sensor");
    debug.printf("Core Version: %s\n", ESP.getCoreVersion().c_str());
    debug.printf("Boot Version: %u\n", ESP.getBootVersion());
    debug.printf("Boot Mode: %u\n", ESP.getBootMode());
    debug.printf("CPU Frequency: %u MHz\n", ESP.getCpuFreqMHz());
    debug.printf("Reset reason: %s\n", ESP.getResetReason().c_str());

    snprintf(identifier, sizeof(identifier), "VINDRIKTNING-%X", ESP.getChipId());
    snprintf(MQTT_TOPIC_AVAILABILITY, 127, "%s/%s/status", FIRMWARE_PREFIX,
            identifier);
    snprintf(MQTT_TOPIC_STATE, 127, "%s/%s/state", FIRMWARE_PREFIX, identifier);
    snprintf(MQTT_TOPIC_COMMAND, 127, "%s/%s/command", FIRMWARE_PREFIX,
            identifier);

    snprintf(MQTT_TOPIC_AUTOCONF_PM25_SENSOR, 127,
            "homeassistant/sensor/%s/%s_pm25/config", FIRMWARE_PREFIX,
            identifier);
    snprintf(MQTT_TOPIC_AUTOCONF_WIFI_SENSOR, 127,
            "homeassistant/sensor/%s/%s_wifi/config", FIRMWARE_PREFIX,
            identifier);

    wifiManager.addParameter(&mqtt_server_param);
    wifiManager.setAPCallback(configModeCallback);
    wifiManager.setSaveConfigCallback(saveConfigCallback);
    wifiManager.setConfigPortalTimeout(3 * 60);
    wifiManager.autoConnect();

    mqttClient.setClient(wifiClient);
    mqttClient.setServer(mqtt_server_param.getValue(), 1883);
    mqttClient.setKeepAlive(10);
    mqttClient.setBufferSize(2048);
    mqttClient.setCallback(mqttCallback);

    setupOTA();
    mqttReconnect();
}

void loop() {
    static uint32_t previousPublishMs = 0;

    ArduinoOTA.handle();
    mqttClient.loop();

    const uint32_t currentMillis = millis();
    if (currentMillis - previousPublishMs >= PUBLISH_INTERVAL) {
        previousPublishMs = currentMillis;

        debug.println("Read sensor");
        if (aht.readRawData() != AHT10_ERROR) {
            publishState(aht.readTemperature(), aht.readHumidity());
        }
    }

    bool mqttConnectionTimeout =
        currentMillis - lastMqttConnectionAttempt >= mqttConnectionInterval;
    if (!mqttClient.connected() && mqttConnectionTimeout) {
        lastMqttConnectionAttempt = currentMillis;
        mqttReconnect();
    }
}
