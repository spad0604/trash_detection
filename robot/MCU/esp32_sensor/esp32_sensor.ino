/*
 * ESP32 #1 - Sensor Node
 * Project: Smart Trash Bin
 *
 * Responsibilities:
 *   - Read 3x DHT22, 3x MQ-2, 3x MQ-135
 *   - Read 3x HC-SR04 bin fill sensors
 *   - Read battery voltage
 *   - Read IR object sensor
 *   - Send all data to Raspberry Pi over USB serial through the Type-C cable
 *
 * Pi is responsible for ROS orchestration and Firebase upload.
 *
 * USB serial protocol, ESP32 -> Pi:
 *   SENSOR:<t1>,<h1>,<t2>,<h2>,<t3>,<h3>,<mq2_1>,<mq2_2>,<mq2_3>,<mq135_1>,<mq135_2>,<mq135_3>,<lvl1>,<lvl2>,<lvl3>,<vbat>,<ir_state>
 *   LEVELS:<l1>,<l2>,<l3>
 *   BATTERY:<vbat>
 *   IR:<0|1>
 *   ALERT:FIRE
 *   ALERT:GAS
 *
 * USB serial protocol, Pi -> ESP32:
 *   CMD:READ_SENSORS
 *   CMD:READ_LEVELS
 *   CMD:READ_BATTERY
 *   CMD:READ_IR
 */

#include <DHT.h>

// Pinout copied from code_test/esp32_sensor_readout.
static const int DHT1_PIN = 5;
static const int DHT2_PIN = 18;
static const int DHT3_PIN = 19;
static const int DHT_TYPE = DHT22;

static const int IR_PIN = 4;

static const int MQ2_1_PIN = 32;
static const int MQ2_2_PIN = 33;
static const int MQ2_3_PIN = 25;

static const int MQ135_1_PIN = 36;  // VP
static const int MQ135_2_PIN = 39;  // VN
static const int MQ135_3_PIN = 34;

static const int US1_TRIG_PIN = 13;
static const int US1_ECHO_PIN = 14;
static const int US2_TRIG_PIN = 27;
static const int US2_ECHO_PIN = 26;
static const int US3_TRIG_PIN = 15;
static const int US3_ECHO_PIN = 2;

static const int VBAT_PIN = 35;
static const float VBAT_R1 = 10000.0f;
static const float VBAT_R2 = 3000.0f;

static const uint32_t USB_SERIAL_BAUD = 115200;
#define PI_SERIAL Serial

static const float TEMP_FIRE_THRESHOLD = 60.0f;
static const int MQ2_SMOKE_THRESHOLD = 800;
static const int MQ135_GAS_THRESHOLD = 700;
static const float BIN_HEIGHT_CM = 18.0f;

static const unsigned long SENSOR_INTERVAL_MS = 3000;
static const unsigned long IR_POLL_INTERVAL_MS = 50;
static const unsigned long ALERT_INTERVAL_MS = 5000;

DHT dht1(DHT1_PIN, DHT_TYPE);
DHT dht2(DHT2_PIN, DHT_TYPE);
DHT dht3(DHT3_PIN, DHT_TYPE);

struct SensorData {
  float temp[3];
  float hum[3];
  int mq2[3];
  int mq135[3];
  int level[3];
  float vbat;
  int irState;
};

SensorData sensorData;

unsigned long lastSensorRead = 0;
unsigned long lastIrPoll = 0;
unsigned long lastFireAlert = 0;
unsigned long lastGasAlert = 0;
int lastSentIrState = -1;

void setupUltrasonicPin(int trigPin, int echoPin) {
  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);
  digitalWrite(trigPin, LOW);
}

void setupAnalogPin(int pin) {
  pinMode(pin, INPUT);
  analogSetPinAttenuation(pin, ADC_11db);
}

void setup() {
  PI_SERIAL.begin(USB_SERIAL_BAUD);
  PI_SERIAL.setTimeout(20);
  delay(300);

  dht1.begin();
  dht2.begin();
  dht3.begin();

  pinMode(IR_PIN, INPUT);

  analogReadResolution(12);
  setupAnalogPin(MQ2_1_PIN);
  setupAnalogPin(MQ2_2_PIN);
  setupAnalogPin(MQ2_3_PIN);
  setupAnalogPin(MQ135_1_PIN);
  setupAnalogPin(MQ135_2_PIN);
  setupAnalogPin(MQ135_3_PIN);
  setupAnalogPin(VBAT_PIN);

  setupUltrasonicPin(US1_TRIG_PIN, US1_ECHO_PIN);
  setupUltrasonicPin(US2_TRIG_PIN, US2_ECHO_PIN);
  setupUltrasonicPin(US3_TRIG_PIN, US3_ECHO_PIN);

  sensorData.irState = digitalRead(IR_PIN);
  sendIrToPi(true);

  PI_SERIAL.println("STATUS:SENSOR_READY");
}

void loop() {
  handlePiCommands();
  pollIr();

  unsigned long now = millis();
  if (now - lastSensorRead >= SENSOR_INTERVAL_MS) {
    lastSensorRead = now;
    readAllSensors();
    checkAlerts();
    sendSensorDataToPi();
  }
}

float readUltrasonicCm(int trigPin, int echoPin) {
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);

  unsigned long duration = pulseIn(echoPin, HIGH, 30000UL);
  if (duration == 0) {
    return BIN_HEIGHT_CM;
  }
  return duration * 0.0343f / 2.0f;
}

int calcFillPercent(float distanceCm) {
  if (distanceCm < 0) {
    return 0;
  }
  float fill = ((BIN_HEIGHT_CM - distanceCm) / BIN_HEIGHT_CM) * 100.0f;
  if (fill < 0) fill = 0;
  if (fill > 100) fill = 100;
  return (int)(fill + 0.5f);
}

float readBatteryVoltage() {
  int mv = analogReadMilliVolts(VBAT_PIN);
  return (mv / 1000.0f) * ((VBAT_R1 + VBAT_R2) / VBAT_R2);
}

float cleanDhtValue(float value) {
  return isnan(value) ? -1.0f : value;
}

void readAllSensors() {
  sensorData.temp[0] = cleanDhtValue(dht1.readTemperature());
  sensorData.hum[0] = cleanDhtValue(dht1.readHumidity());
  sensorData.temp[1] = cleanDhtValue(dht2.readTemperature());
  sensorData.hum[1] = cleanDhtValue(dht2.readHumidity());
  sensorData.temp[2] = cleanDhtValue(dht3.readTemperature());
  sensorData.hum[2] = cleanDhtValue(dht3.readHumidity());

  sensorData.mq2[0] = analogRead(MQ2_1_PIN);
  sensorData.mq2[1] = analogRead(MQ2_2_PIN);
  sensorData.mq2[2] = analogRead(MQ2_3_PIN);

  sensorData.mq135[0] = analogRead(MQ135_1_PIN);
  sensorData.mq135[1] = analogRead(MQ135_2_PIN);
  sensorData.mq135[2] = analogRead(MQ135_3_PIN);

  sensorData.level[0] = calcFillPercent(readUltrasonicCm(US1_TRIG_PIN, US1_ECHO_PIN));
  sensorData.level[1] = calcFillPercent(readUltrasonicCm(US2_TRIG_PIN, US2_ECHO_PIN));
  sensorData.level[2] = calcFillPercent(readUltrasonicCm(US3_TRIG_PIN, US3_ECHO_PIN));

  sensorData.vbat = readBatteryVoltage();
  sensorData.irState = digitalRead(IR_PIN);

}

void pollIr() {
  unsigned long now = millis();
  if (now - lastIrPoll < IR_POLL_INTERVAL_MS) {
    return;
  }
  lastIrPoll = now;
  sensorData.irState = digitalRead(IR_PIN);
  sendIrToPi(false);
}

void sendIrToPi(bool force) {
  if (!force && sensorData.irState == lastSentIrState) {
    return;
  }
  lastSentIrState = sensorData.irState;

  char buf[16];
  snprintf(buf, sizeof(buf), "IR:%d", sensorData.irState);
  PI_SERIAL.println(buf);
}

void checkAlerts() {
  unsigned long now = millis();

  bool fireRisk = false;
  bool gasRisk = false;
  for (int i = 0; i < 3; i++) {
    if (sensorData.temp[i] > TEMP_FIRE_THRESHOLD || sensorData.mq2[i] > MQ2_SMOKE_THRESHOLD) {
      fireRisk = true;
    }
    if (sensorData.mq135[i] > MQ135_GAS_THRESHOLD) {
      gasRisk = true;
    }
  }

  if (fireRisk && now - lastFireAlert >= ALERT_INTERVAL_MS) {
    lastFireAlert = now;
    PI_SERIAL.println("ALERT:FIRE");
  }

  if (gasRisk && now - lastGasAlert >= ALERT_INTERVAL_MS) {
    lastGasAlert = now;
    PI_SERIAL.println("ALERT:GAS");
  }
}

void sendSensorDataToPi() {
  char buf[320];
  snprintf(
    buf,
    sizeof(buf),
    "SENSOR:%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%d,%d,%d,%d,%d,%d,%d,%d,%d,%.2f,%d",
    sensorData.temp[0], sensorData.hum[0],
    sensorData.temp[1], sensorData.hum[1],
    sensorData.temp[2], sensorData.hum[2],
    sensorData.mq2[0], sensorData.mq2[1], sensorData.mq2[2],
    sensorData.mq135[0], sensorData.mq135[1], sensorData.mq135[2],
    sensorData.level[0], sensorData.level[1], sensorData.level[2],
    sensorData.vbat,
    sensorData.irState
  );
  PI_SERIAL.println(buf);
}

void sendLevelsToPi() {
  sensorData.level[0] = calcFillPercent(readUltrasonicCm(US1_TRIG_PIN, US1_ECHO_PIN));
  sensorData.level[1] = calcFillPercent(readUltrasonicCm(US2_TRIG_PIN, US2_ECHO_PIN));
  sensorData.level[2] = calcFillPercent(readUltrasonicCm(US3_TRIG_PIN, US3_ECHO_PIN));

  char buf[64];
  snprintf(buf, sizeof(buf), "LEVELS:%d,%d,%d", sensorData.level[0], sensorData.level[1], sensorData.level[2]);
  PI_SERIAL.println(buf);
}

void sendBatteryToPi() {
  sensorData.vbat = readBatteryVoltage();

  char buf[32];
  snprintf(buf, sizeof(buf), "BATTERY:%.2f", sensorData.vbat);
  PI_SERIAL.println(buf);
}

void handlePiCommands() {
  while (PI_SERIAL.available()) {
    String cmd = PI_SERIAL.readStringUntil('\n');
    cmd.trim();
    if (cmd.length() == 0) {
      continue;
    }

    if (cmd == "CMD:READ_SENSORS") {
      readAllSensors();
      checkAlerts();
      sendSensorDataToPi();
    } else if (cmd == "CMD:READ_LEVELS") {
      sendLevelsToPi();
    } else if (cmd == "CMD:READ_BATTERY") {
      sendBatteryToPi();
    } else if (cmd == "CMD:READ_IR") {
      sensorData.irState = digitalRead(IR_PIN);
      sendIrToPi(true);
    }
  }
}
