#include <DHT.h>

// ESP32 #1 sensor test sketch
// Prints each sensor group on its own line.

// DHT22 pins
static const int DHT1_PIN = 5;
static const int DHT2_PIN = 18;
static const int DHT3_PIN = 19;

// IR sensor pin
static const int IR_PIN = 4;

// MQ-2 analog pins
static const int MQ2_1_PIN = 32;
static const int MQ2_2_PIN = 33;
static const int MQ2_3_PIN = 25;

// MQ-135 analog pins
static const int MQ135_1_PIN = 36; // VP
static const int MQ135_2_PIN = 39; // VN
static const int MQ135_3_PIN = 34;

// Ultrasonic sensors: trig/echo pairs
static const int US1_TRIG_PIN = 13;
static const int US1_ECHO_PIN = 14;
static const int US2_TRIG_PIN = 27;
static const int US2_ECHO_PIN = 26;
static const int US3_TRIG_PIN = 15;
static const int US3_ECHO_PIN = 2;

// Battery divider input
static const int VBAT_PIN = 35;

static const float VBAT_R1 = 10000.0f;
static const float VBAT_R2 = 3000.0f;

static const unsigned long PRINT_INTERVAL_MS = 2000;

DHT dht1(DHT1_PIN, DHT22);
DHT dht2(DHT2_PIN, DHT22);
DHT dht3(DHT3_PIN, DHT22);

unsigned long lastPrint = 0;

float readUltrasonicCm(int trigPin, int echoPin) {
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);

  unsigned long duration = pulseIn(echoPin, HIGH, 30000UL);
  if (duration == 0) {
    return -1.0f;
  }

  return duration * 0.0343f / 2.0f;
}

float readBatteryVoltage() {
  int mv = analogReadMilliVolts(VBAT_PIN);
  return (mv / 1000.0f) * ((VBAT_R1 + VBAT_R2) / VBAT_R2);
}

void setupUltrasonicPin(int trigPin, int echoPin) {
  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);
  digitalWrite(trigPin, LOW);
}

void setup() {
  Serial.begin(115200);
  delay(300);

  dht1.begin();
  dht2.begin();
  dht3.begin();

  pinMode(IR_PIN, INPUT);

  pinMode(MQ2_1_PIN, INPUT);
  pinMode(MQ2_2_PIN, INPUT);
  pinMode(MQ2_3_PIN, INPUT);
  pinMode(MQ135_1_PIN, INPUT);
  pinMode(MQ135_2_PIN, INPUT);
  pinMode(MQ135_3_PIN, INPUT);
  pinMode(VBAT_PIN, INPUT);

  analogReadResolution(12);
  analogSetPinAttenuation(MQ2_1_PIN, ADC_11db);
  analogSetPinAttenuation(MQ2_2_PIN, ADC_11db);
  analogSetPinAttenuation(MQ2_3_PIN, ADC_11db);
  analogSetPinAttenuation(MQ135_1_PIN, ADC_11db);
  analogSetPinAttenuation(MQ135_2_PIN, ADC_11db);
  analogSetPinAttenuation(MQ135_3_PIN, ADC_11db);
  analogSetPinAttenuation(VBAT_PIN, ADC_11db);

  setupUltrasonicPin(US1_TRIG_PIN, US1_ECHO_PIN);
  setupUltrasonicPin(US2_TRIG_PIN, US2_ECHO_PIN);
  setupUltrasonicPin(US3_TRIG_PIN, US3_ECHO_PIN);

  Serial.println("ESP32 sensor test ready");
}

void printDhtLine(const char *label, DHT &sensor) {
  float temperature = sensor.readTemperature();
  float humidity = sensor.readHumidity();

  Serial.print(label);
  Serial.print(",t=");
  if (isnan(temperature)) {
    Serial.print("nan");
  } else {
    Serial.print(temperature, 1);
  }
  Serial.print(",h=");
  if (isnan(humidity)) {
    Serial.print("nan");
  } else {
    Serial.print(humidity, 1);
  }
  Serial.println();
}

void printAnalogLine(const char *label, int pin) {
  int raw = analogRead(pin);
  int mv = analogReadMilliVolts(pin);
  Serial.print(label);
  Serial.print(",raw=");
  Serial.print(raw);
  Serial.print(",mv=");
  Serial.println(mv);
}

void printDigitalLine(const char *label, int pin) {
  int state = digitalRead(pin);
  Serial.print(label);
  Serial.print(",state=");
  Serial.println(state);
}

void printUltrasonicLine(const char *label, int trigPin, int echoPin) {
  float cm = readUltrasonicCm(trigPin, echoPin);
  Serial.print(label);
  Serial.print(",cm=");
  if (cm < 0) {
    Serial.print("timeout");
  } else {
    Serial.print(cm, 1);
  }
  Serial.println();
}

void loop() {
  unsigned long now = millis();
  if (now - lastPrint < PRINT_INTERVAL_MS) {
    return;
  }
  lastPrint = now;

  printDhtLine("DHT1", dht1);
  printDhtLine("DHT2", dht2);
  printDhtLine("DHT3", dht3);

  printDigitalLine("IR", IR_PIN);

  printAnalogLine("MQ2_1", MQ2_1_PIN);
  printAnalogLine("MQ2_2", MQ2_2_PIN);
  printAnalogLine("MQ2_3", MQ2_3_PIN);

  printAnalogLine("MQ135_1", MQ135_1_PIN);
  printAnalogLine("MQ135_2", MQ135_2_PIN);
  printAnalogLine("MQ135_3", MQ135_3_PIN);

  printUltrasonicLine("HC_SR04_1", US1_TRIG_PIN, US1_ECHO_PIN);
  printUltrasonicLine("HC_SR04_2", US2_TRIG_PIN, US2_ECHO_PIN);
  printUltrasonicLine("HC_SR04_3", US3_TRIG_PIN, US3_ECHO_PIN);

  Serial.print("VBAT,voltage=");
  Serial.println(readBatteryVoltage(), 2);
  Serial.println("---");
}