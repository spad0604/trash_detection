/*
 * ============================================================
 *  ESP32 #1 — Sensor & Power Management Node
 *  Project : Smart Trash Bin — Edge AI & IoT Cloud
 * ============================================================
 *  Responsibilities:
 *    • Read 3x DHT22   (temperature & humidity)
 *    • Read 3x MQ-2    (smoke / flammable gas)
 *    • Read 3x MQ-135  (air quality / odour)
 *    • Read 3x HC-SR04 (bin fill-level %)
 *    • Read battery voltage via voltage divider
 *    • Send data to Raspberry Pi over UART (Serial2)
 *    • Push data to Firebase Realtime Database over WiFi
 *  
 *  UART Protocol  (ESP32 #1 ↔ Pi):
 *    ESP→Pi:  "SENSOR:<t1>,<h1>,<t2>,<h2>,<t3>,<h3>,<mq2_1>,<mq2_2>,<mq2_3>,<mq135_1>,<mq135_2>,<mq135_3>,<lvl1>,<lvl2>,<lvl3>,<vbat>\n"
 *    ESP→Pi:  "ALERT:FIRE\n"   — sudden temp rise or smoke
 *    ESP→Pi:  "ALERT:GAS\n"    — hazardous gas detected
 *    Pi→ESP:  "CMD:READ_SENSORS\n"   — force immediate read
 *    Pi→ESP:  "CMD:READ_LEVELS\n"    — force level read
 * ============================================================
 */

#include <WiFi.h>
#include <Firebase_ESP_Client.h>
#include <DHT.h>

// ── Provide token-generation helpers ────────────────────────
#include <addons/TokenHelper.h>
#include <addons/RTDBHelper.h>

// ════════════════════════════════════════════════════════════
//  WiFi & Firebase credentials
// ════════════════════════════════════════════════════════════
#define WIFI_SSID       "YOUR_WIFI_SSID"
#define WIFI_PASSWORD   "YOUR_WIFI_PASSWORD"

#define FIREBASE_HOST   "trash-detection-9d793-default-rtdb.firebaseio.com"
#define FIREBASE_API_KEY "YOUR_FIREBASE_API_KEY"
#define BIN_ID          "bin_001"

// ════════════════════════════════════════════════════════════
//  Pin Definitions  —  ESP32 #1 (Sensor Node)
// ════════════════════════════════════════════════════════════

// HC-SR04 Ultrasonic  (3 bins)
#define TRIG1  12
#define ECHO1  13
#define TRIG2  26
#define ECHO2  27
#define TRIG3   2
#define ECHO3  15

// DHT22 Temperature & Humidity  (3 sensors)
#define DHT1_PIN  4
#define DHT2_PIN 14
#define DHT3_PIN 18
#define DHT_TYPE DHT22

// MQ-2 Smoke / Gas  (Analog)
#define MQ2_1_PIN 32
#define MQ2_2_PIN 33
#define MQ2_3_PIN 25

// MQ-135 Air Quality  (Analog)
#define MQ135_1_PIN 36   // VP
#define MQ135_2_PIN 39   // VN
#define MQ135_3_PIN 35

// Battery voltage  (Analog — through voltage divider)
#define VBAT_PIN 34

// UART to Raspberry Pi  (Serial2)
#define UART_RX2 16
#define UART_TX2 17

// ════════════════════════════════════════════════════════════
//  Thresholds  (tweak for your environment)
// ════════════════════════════════════════════════════════════
#define TEMP_FIRE_THRESHOLD   60.0   // °C
#define MQ2_SMOKE_THRESHOLD   800    // ADC raw (0-4095)
#define MQ135_GAS_THRESHOLD   700    // ADC raw (0-4095)
#define BIN_HEIGHT_CM         40.0   // physical bin depth in cm
#define SENSOR_INTERVAL_MS    3000   // read every 3 s
#define FIREBASE_INTERVAL_MS  5000   // push every 5 s

// ════════════════════════════════════════════════════════════
//  Objects
// ════════════════════════════════════════════════════════════
DHT dht1(DHT1_PIN, DHT_TYPE);
DHT dht2(DHT2_PIN, DHT_TYPE);
DHT dht3(DHT3_PIN, DHT_TYPE);

FirebaseData   fbdo;
FirebaseAuth   auth;
FirebaseConfig config;

// ════════════════════════════════════════════════════════════
//  Sensor data struct
// ════════════════════════════════════════════════════════════
struct SensorData {
  float temp[3];
  float hum[3];
  int   mq2[3];
  int   mq135[3];
  int   level[3];      // fill percentage 0-100
  float vbat;           // battery voltage
};

SensorData sensorData;

// Timing
unsigned long lastSensorRead   = 0;
unsigned long lastFirebasePush = 0;
bool firebaseReady = false;

// ════════════════════════════════════════════════════════════
//  SETUP
// ════════════════════════════════════════════════════════════
void setup() {
  Serial.begin(115200);
  Serial2.begin(9600, SERIAL_8N1, UART_RX2, UART_TX2);

  // ── Ultrasonic pins ───────────────────────────────────────
  pinMode(TRIG1, OUTPUT); pinMode(ECHO1, INPUT);
  pinMode(TRIG2, OUTPUT); pinMode(ECHO2, INPUT);
  pinMode(TRIG3, OUTPUT); pinMode(ECHO3, INPUT);

  // ── DHT sensors ───────────────────────────────────────────
  dht1.begin();
  dht2.begin();
  dht3.begin();

  // ── Analog inputs (MQ / VBAT) ─────────────────────────────
  analogReadResolution(12);   // 0-4095
  analogSetAttenuation(ADC_11db);

  // ── WiFi ──────────────────────────────────────────────────
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("[WiFi] Connecting");
  unsigned long wifiStart = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - wifiStart < 15000) {
    delay(400);
    Serial.print(".");
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("\n[WiFi] Connected — IP: %s\n", WiFi.localIP().toString().c_str());
  } else {
    Serial.println("\n[WiFi] FAILED — running in offline mode");
  }

  // ── Firebase ──────────────────────────────────────────────
  config.api_key      = FIREBASE_API_KEY;
  config.database_url = FIREBASE_HOST;
  config.token_status_callback = tokenStatusCallback;
  Firebase.begin(&config, &auth);
  Firebase.reconnectNetwork(true);
  fbdo.setBSSLBufferSize(4096, 1024);
  firebaseReady = true;

  Serial.println("[ESP32 #1] Sensor Node — Ready");
}

// ════════════════════════════════════════════════════════════
//  LOOP
// ════════════════════════════════════════════════════════════
void loop() {
  // ── Handle commands from Pi ───────────────────────────────
  handlePiCommands();

  // ── Periodic sensor read ──────────────────────────────────
  if (millis() - lastSensorRead >= SENSOR_INTERVAL_MS) {
    lastSensorRead = millis();
    readAllSensors();
    checkAlerts();
    sendSensorDataToPi();
  }

  // ── Periodic Firebase push ────────────────────────────────
  if (firebaseReady && Firebase.ready() &&
      millis() - lastFirebasePush >= FIREBASE_INTERVAL_MS) {
    lastFirebasePush = millis();
    pushToFirebase();
  }
}

// ════════════════════════════════════════════════════════════
//  Ultrasonic helper — returns distance in cm
// ════════════════════════════════════════════════════════════
float readUltrasonic(uint8_t trigPin, uint8_t echoPin) {
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);

  long duration = pulseIn(echoPin, HIGH, 30000);  // 30 ms timeout
  if (duration == 0) return BIN_HEIGHT_CM;         // no echo → assume empty
  return (duration * 0.034) / 2.0;
}

// ════════════════════════════════════════════════════════════
//  Calculate fill percentage
// ════════════════════════════════════════════════════════════
int calcFillPercent(float distanceCm) {
  float fill = ((BIN_HEIGHT_CM - distanceCm) / BIN_HEIGHT_CM) * 100.0;
  if (fill < 0)   fill = 0;
  if (fill > 100)  fill = 100;
  return (int)fill;
}

// ════════════════════════════════════════════════════════════
//  Read all sensors
// ════════════════════════════════════════════════════════════
void readAllSensors() {
  // DHT22
  sensorData.temp[0] = dht1.readTemperature();
  sensorData.hum[0]  = dht1.readHumidity();
  sensorData.temp[1] = dht2.readTemperature();
  sensorData.hum[1]  = dht2.readHumidity();
  sensorData.temp[2] = dht3.readTemperature();
  sensorData.hum[2]  = dht3.readHumidity();

  // Replace NaN with -1 (sensor error)
  for (int i = 0; i < 3; i++) {
    if (isnan(sensorData.temp[i])) sensorData.temp[i] = -1;
    if (isnan(sensorData.hum[i]))  sensorData.hum[i]  = -1;
  }

  // MQ-2
  sensorData.mq2[0] = analogRead(MQ2_1_PIN);
  sensorData.mq2[1] = analogRead(MQ2_2_PIN);
  sensorData.mq2[2] = analogRead(MQ2_3_PIN);

  // MQ-135
  sensorData.mq135[0] = analogRead(MQ135_1_PIN);
  sensorData.mq135[1] = analogRead(MQ135_2_PIN);
  sensorData.mq135[2] = analogRead(MQ135_3_PIN);

  // Ultrasonic fill level
  float d1 = readUltrasonic(TRIG1, ECHO1);
  float d2 = readUltrasonic(TRIG2, ECHO2);
  float d3 = readUltrasonic(TRIG3, ECHO3);
  sensorData.level[0] = calcFillPercent(d1);
  sensorData.level[1] = calcFillPercent(d2);
  sensorData.level[2] = calcFillPercent(d3);

  // Battery voltage  (voltage divider: R1=30k, R2=10k → factor 4)
  int rawAdc = analogRead(VBAT_PIN);
  sensorData.vbat = (rawAdc / 4095.0) * 3.3 * 4.0;   // scale to real voltage

  Serial.printf("[Sensor] T: %.1f/%.1f/%.1f  H: %.1f/%.1f/%.1f  Levels: %d%%/%d%%/%d%%  VBAT: %.1fV\n",
    sensorData.temp[0], sensorData.temp[1], sensorData.temp[2],
    sensorData.hum[0],  sensorData.hum[1],  sensorData.hum[2],
    sensorData.level[0], sensorData.level[1], sensorData.level[2],
    sensorData.vbat);
}

// ════════════════════════════════════════════════════════════
//  Alert check — fire / gas
// ════════════════════════════════════════════════════════════
void checkAlerts() {
  // Fire risk: high temp OR smoke
  for (int i = 0; i < 3; i++) {
    if (sensorData.temp[i] > TEMP_FIRE_THRESHOLD ||
        sensorData.mq2[i]  > MQ2_SMOKE_THRESHOLD) {
      Serial2.println("ALERT:FIRE");
      Serial.println("[ALERT] FIRE risk detected!");

      // Also push alert to Firebase immediately
      if (firebaseReady && Firebase.ready()) {
        String path = String("/bins/") + BIN_ID + "/alerts/fire_risk";
        Firebase.RTDB.setBool(&fbdo, path, true);
      }
      break;
    }
  }

  // Gas leak
  for (int i = 0; i < 3; i++) {
    if (sensorData.mq135[i] > MQ135_GAS_THRESHOLD) {
      Serial2.println("ALERT:GAS");
      Serial.println("[ALERT] Gas / odour anomaly detected!");

      if (firebaseReady && Firebase.ready()) {
        String path = String("/bins/") + BIN_ID + "/alerts/gas_leak";
        Firebase.RTDB.setBool(&fbdo, path, true);
      }
      break;
    }
  }
}

// ════════════════════════════════════════════════════════════
//  Send data to Raspberry Pi via UART
// ════════════════════════════════════════════════════════════
void sendSensorDataToPi() {
  char buf[256];
  snprintf(buf, sizeof(buf),
    "SENSOR:%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%d,%d,%d,%d,%d,%d,%d,%d,%d,%.1f",
    sensorData.temp[0], sensorData.hum[0],
    sensorData.temp[1], sensorData.hum[1],
    sensorData.temp[2], sensorData.hum[2],
    sensorData.mq2[0],  sensorData.mq2[1],  sensorData.mq2[2],
    sensorData.mq135[0],sensorData.mq135[1],sensorData.mq135[2],
    sensorData.level[0],sensorData.level[1],sensorData.level[2],
    sensorData.vbat);
  Serial2.println(buf);
}

// ════════════════════════════════════════════════════════════
//  Handle incoming UART commands from Pi
// ════════════════════════════════════════════════════════════
void handlePiCommands() {
  if (!Serial2.available()) return;

  String cmd = Serial2.readStringUntil('\n');
  cmd.trim();

  if (cmd == "CMD:READ_SENSORS") {
    Serial.println("[CMD] Pi requested sensor read");
    readAllSensors();
    checkAlerts();
    sendSensorDataToPi();
  }
  else if (cmd == "CMD:READ_LEVELS") {
    Serial.println("[CMD] Pi requested level read");
    float d1 = readUltrasonic(TRIG1, ECHO1);
    float d2 = readUltrasonic(TRIG2, ECHO2);
    float d3 = readUltrasonic(TRIG3, ECHO3);
    sensorData.level[0] = calcFillPercent(d1);
    sensorData.level[1] = calcFillPercent(d2);
    sensorData.level[2] = calcFillPercent(d3);

    char buf[64];
    snprintf(buf, sizeof(buf), "LEVELS:%d,%d,%d",
      sensorData.level[0], sensorData.level[1], sensorData.level[2]);
    Serial2.println(buf);
  }
  else if (cmd == "CMD:READ_BATTERY") {
    int rawAdc = analogRead(VBAT_PIN);
    sensorData.vbat = (rawAdc / 4095.0) * 3.3 * 4.0;
    char buf[32];
    snprintf(buf, sizeof(buf), "BATTERY:%.1f", sensorData.vbat);
    Serial2.println(buf);
  }
  else {
    Serial.printf("[CMD] Unknown command: %s\n", cmd.c_str());
  }
}

// ════════════════════════════════════════════════════════════
//  Push sensor data to Firebase Realtime Database
// ════════════════════════════════════════════════════════════
void pushToFirebase() {
  String basePath = String("/bins/") + BIN_ID;

  // Sensors
  Firebase.RTDB.setFloat(&fbdo, basePath + "/sensors/temperature1", sensorData.temp[0]);
  Firebase.RTDB.setFloat(&fbdo, basePath + "/sensors/humidity1",    sensorData.hum[0]);
  Firebase.RTDB.setFloat(&fbdo, basePath + "/sensors/temperature2", sensorData.temp[1]);
  Firebase.RTDB.setFloat(&fbdo, basePath + "/sensors/humidity2",    sensorData.hum[1]);
  Firebase.RTDB.setFloat(&fbdo, basePath + "/sensors/temperature3", sensorData.temp[2]);
  Firebase.RTDB.setFloat(&fbdo, basePath + "/sensors/humidity3",    sensorData.hum[2]);
  Firebase.RTDB.setInt(&fbdo,   basePath + "/sensors/mq2_1",       sensorData.mq2[0]);
  Firebase.RTDB.setInt(&fbdo,   basePath + "/sensors/mq2_2",       sensorData.mq2[1]);
  Firebase.RTDB.setInt(&fbdo,   basePath + "/sensors/mq2_3",       sensorData.mq2[2]);
  Firebase.RTDB.setInt(&fbdo,   basePath + "/sensors/mq135_1",     sensorData.mq135[0]);
  Firebase.RTDB.setInt(&fbdo,   basePath + "/sensors/mq135_2",     sensorData.mq135[1]);
  Firebase.RTDB.setInt(&fbdo,   basePath + "/sensors/mq135_3",     sensorData.mq135[2]);

  // Fill levels
  Firebase.RTDB.setInt(&fbdo, basePath + "/levels/bin1_percent", sensorData.level[0]);
  Firebase.RTDB.setInt(&fbdo, basePath + "/levels/bin2_percent", sensorData.level[1]);
  Firebase.RTDB.setInt(&fbdo, basePath + "/levels/bin3_percent", sensorData.level[2]);

  // Battery
  Firebase.RTDB.setFloat(&fbdo, basePath + "/battery/voltage", sensorData.vbat);
  int batPercent = map(constrain((int)(sensorData.vbat * 10), 100, 126), 100, 126, 0, 100);
  Firebase.RTDB.setInt(&fbdo,   basePath + "/battery/percent", batPercent);

  // Bin full alerts
  Firebase.RTDB.setBool(&fbdo, basePath + "/alerts/bin1_full", sensorData.level[0] >= 95);
  Firebase.RTDB.setBool(&fbdo, basePath + "/alerts/bin2_full", sensorData.level[1] >= 95);
  Firebase.RTDB.setBool(&fbdo, basePath + "/alerts/bin3_full", sensorData.level[2] >= 95);

  // Timestamp
  Firebase.RTDB.setTimestamp(&fbdo, basePath + "/status/last_update");

  Serial.println("[Firebase] Data pushed");
}
