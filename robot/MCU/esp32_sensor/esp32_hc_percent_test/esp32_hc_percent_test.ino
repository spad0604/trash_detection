/*
 * ESP32 HC-SR04 fill-percent test
 *
 * Bin height: 18 cm
 *
 * Pins copied from robot/MCU/esp32_sensor/esp32_sensor.ino:
 *   HC1: TRIG 13, ECHO 14
 *   HC2: TRIG 27, ECHO 26
 *   HC3: TRIG 15, ECHO 2
 *
 * Serial output:
 *   HC:dist1_cm,pct1,dist2_cm,pct2,dist3_cm,pct3
 *   LEVELS:pct1,pct2,pct3
 */

#include <Arduino.h>

static const int US1_TRIG_PIN = 13;
static const int US1_ECHO_PIN = 14;
static const int US2_TRIG_PIN = 27;
static const int US2_ECHO_PIN = 26;
static const int US3_TRIG_PIN = 15;
static const int US3_ECHO_PIN = 2;

static const float BIN_HEIGHT_CM = 18.0f;
static const uint32_t SERIAL_BAUD = 115200;
static const unsigned long READ_INTERVAL_MS = 500;
static const unsigned long ECHO_TIMEOUT_US = 30000UL;

unsigned long lastReadMs = 0;

void setupUltrasonicPin(int trigPin, int echoPin) {
  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);
  digitalWrite(trigPin, LOW);
}

float readUltrasonicCm(int trigPin, int echoPin) {
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);

  unsigned long duration = pulseIn(echoPin, HIGH, ECHO_TIMEOUT_US);
  if (duration == 0) {
    return BIN_HEIGHT_CM;
  }

  return duration * 0.0343f / 2.0f;
}

int calcFillPercent(float distanceCm) {
  float fill = ((BIN_HEIGHT_CM - distanceCm) / BIN_HEIGHT_CM) * 100.0f;
  if (fill < 0) fill = 0;
  if (fill > 100) fill = 100;
  return (int)(fill + 0.5f);
}

void printReadings() {
  float d1 = readUltrasonicCm(US1_TRIG_PIN, US1_ECHO_PIN);
  delay(40);
  float d2 = readUltrasonicCm(US2_TRIG_PIN, US2_ECHO_PIN);
  delay(40);
  float d3 = readUltrasonicCm(US3_TRIG_PIN, US3_ECHO_PIN);

  int p1 = calcFillPercent(d1);
  int p2 = calcFillPercent(d2);
  int p3 = calcFillPercent(d3);

  Serial.print("HC:");
  Serial.print(d1, 1);
  Serial.print(",");
  Serial.print(p1);
  Serial.print(",");
  Serial.print(d2, 1);
  Serial.print(",");
  Serial.print(p2);
  Serial.print(",");
  Serial.print(d3, 1);
  Serial.print(",");
  Serial.println(p3);

  Serial.print("LEVELS:");
  Serial.print(p1);
  Serial.print(",");
  Serial.print(p2);
  Serial.print(",");
  Serial.println(p3);
}

void setup() {
  Serial.begin(SERIAL_BAUD);
  delay(300);

  setupUltrasonicPin(US1_TRIG_PIN, US1_ECHO_PIN);
  setupUltrasonicPin(US2_TRIG_PIN, US2_ECHO_PIN);
  setupUltrasonicPin(US3_TRIG_PIN, US3_ECHO_PIN);

  Serial.println("[ESP32 HC Percent Test] Ready");
  Serial.println("BIN_HEIGHT_CM=18.0");
}

void loop() {
  unsigned long nowMs = millis();
  if (nowMs - lastReadMs >= READ_INTERVAL_MS) {
    lastReadMs = nowMs;
    printReadings();
  }
}
