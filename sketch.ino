#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <DHTesp.h>
#include <math.h>

// ======================================
// LAB 3 - ESP32 FREERTOS MQTT
// AUTO / MANUAL GARDEN CONTROL
// ======================================

// WIFI
const char* WIFI_SSID = "Wokwi-GUEST";
const char* WIFI_PASS = "";

// MQTT
const char* MQTT_HOST = "broker.emqx.io";
const int MQTT_PORT = 1883;

const char* PREFIX =
  "lab3/smartgarden/esp32_2026_new01";

// GPIO
#define PIN_DHT   15
#define PIN_PUMP  18
#define PIN_FAN   19
#define PIN_MIST  21

// TIMING
#define SENSOR_MS 2500
#define MQTT_RETRY_MS 5000
#define WIFI_RETRY_MS 10000

// PWM
#define PWM_FREQ 5000
#define PWM_RESOLUTION 8

// ======================================
// DATA STRUCTURES
// ======================================
struct SensorData {
  float temperature;
  float humidity;
  uint32_t uptimeMs;
};

struct AlertData {
  float temperature;
  float humidity;
};

// ======================================
// FREERTOS QUEUES
// ======================================
QueueHandle_t dataQueue = NULL;
QueueHandle_t dataAlert = NULL;

// ======================================
// GLOBAL OBJECTS
// ======================================
DHTesp dht;
WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);

// Shared control state
SemaphoreHandle_t controlMutex = NULL;

enum ControlMode {
  MODE_AUTO,
  MODE_MANUAL
};

ControlMode currentMode = MODE_AUTO;

bool pumpState = false;
bool fanState = false;
bool mistState = false;
int mistPower = 0;

bool manualPump = false;
bool manualFan = false;
bool manualMist = false;

float latestTemperature = NAN;
float latestHumidity = NAN;

// Mist cycle
uint32_t mistEndTime = 0;

// ======================================
// MQTT TOPICS
// ======================================
String base = String(PREFIX);

String topicTemp = base + "/temperature";
String topicHum = base + "/humidity";

String topicModeSet = base + "/mode/set";
String topicModeState = base + "/mode/state";

String topicPumpSet = base + "/pump/set";
String topicPumpState = base + "/pump/state";

String topicFanSet = base + "/fan/set";
String topicFanState = base + "/fan/state";

String topicMistSet = base + "/mist/set";
String topicMistState = base + "/mist/state";
String topicMistPower = base + "/mist/power";

// ======================================
// HARDWARE OUTPUT
// Called while controlMutex is held
// ======================================
void applyOutputsLocked() {

  digitalWrite(PIN_PUMP, pumpState ? HIGH : LOW);
  digitalWrite(PIN_FAN, fanState ? HIGH : LOW);

  int duty = map(mistPower, 0, 100, 0, 255);
  ledcWrite(PIN_MIST, duty);

  mistState = (mistPower > 0);
}

// ======================================
// MQTT CALLBACK - CORE 0
// ======================================
void mqttCallback(char* topic, byte* payload,
                  unsigned int length) {

  String message = "";

  for (unsigned int i = 0; i < length; i++) {
    message += (char)payload[i];
  }

  message.trim();
  message.toUpperCase();

  String receivedTopic = String(topic);

  if (xSemaphoreTake(controlMutex,
                     pdMS_TO_TICKS(100)) != pdTRUE) {
    return;
  }

  // CHANGE MODE
  if (receivedTopic == topicModeSet) {

    if (message == "AUTO" ||
        message == "MANUAL") {

      currentMode = (message == "AUTO")
                    ? MODE_AUTO : MODE_MANUAL;

      pumpState = false;
      fanState = false;
      mistPower = 0;
      mistEndTime = 0;

      manualPump = false;
      manualFan = false;
      manualMist = false;

      Serial.printf(
        "[MODE] Changed to %s\n",
        currentMode == MODE_AUTO ? "AUTO" : "MANUAL"
      );
    }
  }

  // MANUAL COMMANDS
  if (currentMode == MODE_MANUAL) {

    if (receivedTopic == topicPumpSet) {
      if (message == "ON" || message == "OFF") {
        manualPump = (message == "ON");
        pumpState = manualPump;
      }
    }

    if (receivedTopic == topicFanSet) {
      if (message == "ON" || message == "OFF") {
        manualFan = (message == "ON");
        fanState = manualFan;
      }
    }

    if (receivedTopic == topicMistSet) {
      if (message == "ON" || message == "OFF") {
        manualMist = (message == "ON");
        mistPower = manualMist ? 100 : 0;
      }
    }
  }

  applyOutputsLocked();

  String mode = currentMode == MODE_AUTO
                ? "AUTO" : "MANUAL";

  bool p = pumpState;
  bool f = fanState;
  bool m = mistState;
  int power = mistPower;

  xSemaphoreGive(controlMutex);

  // MQTT is only accessed from Core 0
  mqtt.publish(topicModeState.c_str(),
               mode.c_str(), true);

  mqtt.publish(topicPumpState.c_str(),
               p ? "ON" : "OFF", true);

  mqtt.publish(topicFanState.c_str(),
               f ? "ON" : "OFF", true);

  mqtt.publish(topicMistState.c_str(),
               m ? "ON" : "OFF", true);

  char buffer[8];
  snprintf(buffer, sizeof(buffer), "%d", power);
  mqtt.publish(topicMistPower.c_str(), buffer, true);
}

// ======================================
// WIFI CONNECTION
// ======================================
void connectWiFi() {

  if (WiFi.status() == WL_CONNECTED) return;

  Serial.println("[WIFI] Connecting...");

  WiFi.begin(WIFI_SSID, WIFI_PASS);

  unsigned long start = millis();

  while (WiFi.status() != WL_CONNECTED &&
         millis() - start < 10000) {
    vTaskDelay(pdMS_TO_TICKS(500));
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("[WIFI] Connected!");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("[WIFI] Connection failed");
  }
}

// ======================================
// MQTT CONNECTION
// ======================================
void connectMQTT() {

  if (mqtt.connected()) return;

  String clientId = "ESP32_LAB3_" +
                    String((uint32_t)ESP.getEfuseMac(), HEX);

  Serial.println("[MQTT] Connecting...");

  if (mqtt.connect(clientId.c_str())) {

    Serial.println("[MQTT] Connected!");

    mqtt.subscribe(topicModeSet.c_str());
    mqtt.subscribe(topicPumpSet.c_str());
    mqtt.subscribe(topicFanSet.c_str());
    mqtt.subscribe(topicMistSet.c_str());

    // Publish initial states
    if (xSemaphoreTake(controlMutex,
                       pdMS_TO_TICKS(100)) == pdTRUE) {

      mqtt.publish(topicModeState.c_str(),
        currentMode == MODE_AUTO ? "AUTO" : "MANUAL",
        true);

      mqtt.publish(topicPumpState.c_str(),
        pumpState ? "ON" : "OFF", true);

      mqtt.publish(topicFanState.c_str(),
        fanState ? "ON" : "OFF", true);

      mqtt.publish(topicMistState.c_str(),
        mistState ? "ON" : "OFF", true);

      char buffer[8];
      snprintf(buffer, sizeof(buffer), "%d", mistPower);
      mqtt.publish(topicMistPower.c_str(), buffer, true);

      xSemaphoreGive(controlMutex);
    }

  } else {
    Serial.printf("[MQTT] Failed rc=%d\n", mqtt.state());
  }
}

// ======================================
// TASK 1 - READ SENSOR - CORE 1
// ======================================
void taskReadData(void* parameter) {

  SensorData data;
  AlertData alert;

  TickType_t lastWake = xTaskGetTickCount();

  while (true) {

    TempAndHumidity sensor = dht.getTempAndHumidity();

    if (!isnan(sensor.temperature) &&
        !isnan(sensor.humidity)) {

      data.temperature = sensor.temperature;
      data.humidity = sensor.humidity;
      data.uptimeMs = millis();

      alert.temperature = sensor.temperature;
      alert.humidity = sensor.humidity;

      xQueueOverwrite(dataQueue, &data);
      xQueueOverwrite(dataAlert, &alert);

      Serial.printf(
        "[SENSOR][CORE %d] T=%.1f C | H=%.1f %%\n",
        xPortGetCoreID(),
        data.temperature,
        data.humidity
      );

    } else {
      Serial.println("[SENSOR] DHT22 read error!");
    }

    vTaskDelayUntil(
      &lastWake,
      pdMS_TO_TICKS(SENSOR_MS)
    );
  }
}

// ======================================
// TASK 2 - TRANSMIT MQTT - CORE 0
// ======================================
void taskTransmitData(void* parameter) {

  SensorData data;

  uint32_t lastWiFiRetry = 0;
  uint32_t lastMQTTRetry = 0;
  uint32_t lastStatePublish = 0;

  while (true) {

    if (WiFi.status() != WL_CONNECTED) {

      if (lastWiFiRetry == 0 ||
          millis() - lastWiFiRetry >= WIFI_RETRY_MS) {

        lastWiFiRetry = millis();
        connectWiFi();
      }

    } else if (!mqtt.connected()) {

      if (lastMQTTRetry == 0 ||
          millis() - lastMQTTRetry >= MQTT_RETRY_MS) {

        lastMQTTRetry = millis();
        connectMQTT();
      }

    } else {

      mqtt.loop();

      // Publish sensor data
      if (xQueueReceive(dataQueue, &data, 0) == pdTRUE) {

        char t[16], h[16];

        snprintf(t, sizeof(t), "%.1f", data.temperature);
        snprintf(h, sizeof(h), "%.1f", data.humidity);

        mqtt.publish(topicTemp.c_str(), t);
        mqtt.publish(topicHum.c_str(), h);

        Serial.printf(
          "[MQTT][CORE %d] T=%.1f H=%.1f\n",
          xPortGetCoreID(),
          data.temperature,
          data.humidity
        );
      }

      // Periodic state synchronization
      if (millis() - lastStatePublish >= 1000) {

        lastStatePublish = millis();

        if (xSemaphoreTake(controlMutex,
                           pdMS_TO_TICKS(100)) == pdTRUE) {

          String mode = currentMode == MODE_AUTO
                        ? "AUTO" : "MANUAL";

          bool p = pumpState;
          bool f = fanState;
          bool m = mistState;
          int power = mistPower;

          xSemaphoreGive(controlMutex);

          mqtt.publish(topicModeState.c_str(),
                       mode.c_str(), true);

          mqtt.publish(topicPumpState.c_str(),
                       p ? "ON" : "OFF", true);

          mqtt.publish(topicFanState.c_str(),
                       f ? "ON" : "OFF", true);

          mqtt.publish(topicMistState.c_str(),
                       m ? "ON" : "OFF", true);

          char buffer[8];
          snprintf(buffer, sizeof(buffer), "%d", power);
          mqtt.publish(topicMistPower.c_str(),
                       buffer, true);
        }
      }
    }

    vTaskDelay(pdMS_TO_TICKS(50));
  }
}

// ======================================
// TASK 3 - AUTO CONTROL - CORE 1
// ======================================
void taskAlert(void* parameter) {

  AlertData alert;

  float temperature = NAN;
  float humidity = NAN;

  while (true) {

    if (xQueueReceive(dataAlert, &alert,
                      pdMS_TO_TICKS(100)) == pdTRUE) {
      temperature = alert.temperature;
      humidity = alert.humidity;
    }

    if (xSemaphoreTake(controlMutex,
                       pdMS_TO_TICKS(100)) != pdTRUE) {
      continue;
    }

    if (currentMode == MODE_AUTO &&
        !isnan(temperature) &&
        !isnan(humidity)) {

      // AUTO PUMP - humidity hysteresis
      if (humidity < 40.0) {
        pumpState = true;
      } else if (humidity >= 60.0) {
        pumpState = false;
      }

      // AUTO FAN - temperature hysteresis
      if (temperature > 35.0) {
        fanState = true;
      } else if (temperature < 30.0) {
        fanState = false;
      }

      // AUTO MIST
      int targetPower = 0;
      uint32_t duration = 0;

      if (humidity < 30.0) {
        targetPower = 100;
        duration = 30000;
      } else if (humidity < 35.0) {
        targetPower = 50;
        duration = 20000;
      } else if (humidity < 40.0) {
        targetPower = 25;
        duration = 10000;
      }

      if (targetPower == 0) {
        mistPower = 0;
        mistEndTime = 0;
      } else if (mistPower == 0 &&
                 (int32_t)(millis() - mistEndTime) >= 0) {

        mistPower = targetPower;
        mistEndTime = millis() + duration;

        Serial.printf(
          "[AUTO MIST] H=%.1f Power=%d%% Time=%lu s\n",
          humidity,
          mistPower,
          (unsigned long)(duration / 1000)
        );
      }

      if (mistPower > 0 &&
          (int32_t)(millis() - mistEndTime) >= 0) {

        mistPower = 0;
        Serial.println("[AUTO MIST] Cycle finished");
      }

      applyOutputsLocked();
    }

    xSemaphoreGive(controlMutex);
  }
}

// ======================================
// SETUP
// ======================================
void setup() {

  Serial.begin(115200);

  pinMode(PIN_PUMP, OUTPUT);
  pinMode(PIN_FAN, OUTPUT);

  digitalWrite(PIN_PUMP, LOW);
  digitalWrite(PIN_FAN, LOW);

  // Arduino ESP32 Core 3.x
  ledcAttach(PIN_MIST, PWM_FREQ, PWM_RESOLUTION);
  ledcWrite(PIN_MIST, 0);

  dht.setup(PIN_DHT, DHTesp::DHT22);

  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setCallback(mqttCallback);
  mqtt.setKeepAlive(30);
  mqtt.setSocketTimeout(2);

  dataQueue = xQueueCreate(1, sizeof(SensorData));
  dataAlert = xQueueCreate(1, sizeof(AlertData));

  controlMutex = xSemaphoreCreateMutex();

  if (!dataQueue || !dataAlert || !controlMutex) {
    Serial.println("[ERROR] FreeRTOS resource failed!");
    while (true) delay(1000);
  }

  xTaskCreatePinnedToCore(
    taskTransmitData,
    "TransmitData",
    8192,
    NULL,
    2,
    NULL,
    0
  );

  xTaskCreatePinnedToCore(
    taskReadData,
    "ReadData",
    4096,
    NULL,
    2,
    NULL,
    1
  );

  xTaskCreatePinnedToCore(
    taskAlert,
    "Alert",
    4096,
    NULL,
    2,
    NULL,
    1
  );

  Serial.println("================================");
  Serial.println(" LAB 3 - ESP32 FREERTOS MQTT");
  Serial.println(" MODE: AUTO");
  Serial.println("================================");
}

// ======================================
// LOOP
// ======================================
void loop() {
  vTaskDelay(pdMS_TO_TICKS(1000));
}