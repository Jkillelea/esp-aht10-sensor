#include <AHT10.h>
#include <ArduinoJson.h>
#include <ArduinoOTA.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <WiFiManager.h>
#include <cstdio>

uint8_t mqttRetryCounter = 0;

AHT10        aht;
WiFiManager  wifiManager;
WiFiClient   wifiClient;
PubSubClient mqttClient;

char mqtt_server[80];
char mqtt_username[80];
char mqtt_password[80];

WiFiManagerParameter mqtt_server_param("server", "MQTT Server", mqtt_server, sizeof(mqtt_server));
WiFiManagerParameter mqtt_user_param("user", "MQTT Username", mqtt_username, sizeof(mqtt_username));
WiFiManagerParameter mqtt_pass_param("pass", "MQTT Password", mqtt_password, sizeof(mqtt_password));

uint32_t lastMqttConnectionAttempt = 0;

// 1 minute = 60 seconds = 60000 milliseconds
const uint16_t mqttConnectionInterval = 60000;

uint32_t statusPublishPreviousMillis = 0;
const uint16_t statusPublishInterval = 30000; // 30 seconds = 30000 milliseconds

char identifier[24];

#define FIRMWARE_PREFIX "aht10-env-sensor"
#define AVAILABILITY_ONLINE "online"
#define AVAILABILITY_OFFLINE "offline"
char MQTT_TOPIC_AVAILABILITY[128];
char MQTT_TOPIC_STATE[128];
char MQTT_TOPIC_COMMAND[128];

char MQTT_TOPIC_AUTOCONF_WIFI_SENSOR[128];
char MQTT_TOPIC_AUTOCONF_PM25_SENSOR[128];

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

void saveConfigCallback() {}

void mqttCallback(char *topic, uint8_t *payload, unsigned int length) {}

void setupWifi() {
    Serial.printf("WiFi hostname: %s\n", identifier);
    WiFi.hostname(identifier);

    wifiManager.setDebugOutput(true);
    wifiManager.addParameter(&mqtt_server_param);
    wifiManager.addParameter(&mqtt_user_param);
    wifiManager.addParameter(&mqtt_pass_param);

    wifiManager.setSaveConfigCallback(saveConfigCallback);
    wifiManager.autoConnect(identifier);

    mqttClient.setClient(wifiClient);
    strcpy(mqtt_server, mqtt_server_param.getValue());
    strcpy(mqtt_username, mqtt_user_param.getValue());
    strcpy(mqtt_password, mqtt_pass_param.getValue());

    if (mqtt_server_param.getValueLength() < 1) {
        // No values? Reset and request from user
        Serial.printf("Missing MQTT server, resetting and asking user.\n");
        wifiManager.resetSettings();
        wifiManager.autoConnect();
    }
    
    Serial.printf("MQTT Server: %s\n", mqtt_server);

    mqttClient.setServer(mqtt_server, 1883);
    mqttClient.setKeepAlive(10);
    mqttClient.setBufferSize(2048);
    mqttClient.setCallback(mqttCallback);
}

void setupOTA() {
    ArduinoOTA.onStart([]() { Serial.println("OTA Start"); });
    ArduinoOTA.onEnd([]() { Serial.println("\nOTA End"); });

    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        Serial.printf("OTA Progress: %u%%\r", (progress / (total / 100)));
    });

    ArduinoOTA.onError([](ota_error_t error) {
        Serial.printf("Error[%u]: ", error);
        if (error == OTA_AUTH_ERROR) {
            Serial.println("Auth Failed");
        } else if (error == OTA_BEGIN_ERROR) {
            Serial.println("Begin Failed");
        } else if (error == OTA_CONNECT_ERROR) {
            Serial.println("Connect Failed");
        } else if (error == OTA_RECEIVE_ERROR) {
            Serial.println("Receive Failed");
        } else if (error == OTA_END_ERROR) {
            Serial.println("End Failed");
        }
    });

    ArduinoOTA.setHostname(identifier);

    // This is less of a security measure and more a accidential flash prevention
    // ArduinoOTA.setPassword(identifier);
    ArduinoOTA.begin();
}

void mqttReconnect() {
    Serial.println("Reconnect mqtt...");
    for (uint8_t attempt = 0; attempt < 10; attempt++) {
        Serial.printf("Attempt %d\n", attempt);
        if (mqttClient.connect(identifier, mqtt_username, mqtt_password,
                    MQTT_TOPIC_AVAILABILITY, 1, true,
                    AVAILABILITY_OFFLINE)) {

            mqttClient.publish(MQTT_TOPIC_AVAILABILITY, AVAILABILITY_ONLINE, true);
            publishAutoConfig();

            // Make sure to subscribe after polling the status so that we never
            // execute commands with the default data
            mqttClient.subscribe(MQTT_TOPIC_COMMAND);
            break;
        }
        delay(1000);
    }
}

void setup() {
    delay(300);

    Serial.begin(115200);
    aht.begin(SDA, SCL);
    aht.setCycleMode();

    Serial.println("\n");
    Serial.println("\n");
    Serial.println("\n");
    Serial.printf("Core Version: %s\n", ESP.getCoreVersion().c_str());
    Serial.printf("Boot Version: %u\n", ESP.getBootVersion());
    Serial.printf("Boot Mode: %u\n", ESP.getBootMode());
    Serial.printf("CPU Frequency: %u MHz\n", ESP.getCpuFreqMHz());
    Serial.printf("Reset reason: %s\n", ESP.getResetReason().c_str());

    // Create topic strings
    snprintf(identifier,              sizeof(identifier),              "AHT10-%X",      ESP.getChipId());  
    snprintf(MQTT_TOPIC_AVAILABILITY, sizeof(MQTT_TOPIC_AVAILABILITY), "%s/%s/status",  FIRMWARE_PREFIX, identifier);
    snprintf(MQTT_TOPIC_STATE,        sizeof(MQTT_TOPIC_STATE),        "%s/%s/state",   FIRMWARE_PREFIX, identifier);
    snprintf(MQTT_TOPIC_COMMAND,      sizeof(MQTT_TOPIC_COMMAND),      "%s/%s/command", FIRMWARE_PREFIX, identifier);

    snprintf(MQTT_TOPIC_AUTOCONF_PM25_SENSOR, sizeof(MQTT_TOPIC_AUTOCONF_PM25_SENSOR),
            "homeassistant/sensor/%s/%s_pm25/config", FIRMWARE_PREFIX,
            identifier);
    snprintf(MQTT_TOPIC_AUTOCONF_WIFI_SENSOR, sizeof(MQTT_TOPIC_AUTOCONF_WIFI_SENSOR),
            "homeassistant/sensor/%s/%s_wifi/config", FIRMWARE_PREFIX,
            identifier);

    Serial.printf("MQTT_TOPIC_AVAILABILITY: %s\n", MQTT_TOPIC_AVAILABILITY);
    Serial.printf("MQTT_TOPIC_STATE: %s\n", MQTT_TOPIC_STATE);
    Serial.printf("MQTT_TOPIC_COMMAND: %s\n", MQTT_TOPIC_COMMAND);
    Serial.printf("MQTT_TOPIC_AUTOCONF_PM25_SENSOR: %s\n", MQTT_TOPIC_AUTOCONF_PM25_SENSOR);
    Serial.printf("MQTT_TOPIC_AUTOCONF_WIFI_SENSOR: %s\n", MQTT_TOPIC_AUTOCONF_WIFI_SENSOR);

    setupWifi();
    setupOTA();

    Serial.printf("Hostname: %s\n", identifier);
    Serial.printf("IP: %s\n", WiFi.localIP().toString().c_str());

    mqttReconnect();
}

void publishState(float degC, float relHumid) {
    DynamicJsonDocument wifiJson(192);
    DynamicJsonDocument stateJson(604);
    char payload[256];

    wifiJson["ssid"] = WiFi.SSID();
    wifiJson["ip"]   = WiFi.localIP().toString();
    wifiJson["rssi"] = WiFi.RSSI();

    stateJson["degC"] = degC;
    stateJson["relHumid"] = relHumid;
    stateJson["wifi"] = wifiJson.as<JsonObject>();

    serializeJson(stateJson, payload);
    Serial.println("Publish");
    Serial.println(payload);
    mqttClient.publish(MQTT_TOPIC_STATE, payload, true);
}

void loop() {
    ArduinoOTA.handle();
    mqttClient.loop();

    const uint32_t currentMillis = millis();
    if (currentMillis - statusPublishPreviousMillis >= statusPublishInterval) {
        statusPublishPreviousMillis = currentMillis;

        Serial.println("Read sensor");
        if (aht.readRawData() != AHT10_ERROR) {
            publishState(aht.readTemperature(), aht.readHumidity());
        }
    }

    if (!mqttClient.connected() &&
            currentMillis - lastMqttConnectionAttempt >= mqttConnectionInterval) {
        lastMqttConnectionAttempt = currentMillis;
        mqttReconnect();
    }
}
