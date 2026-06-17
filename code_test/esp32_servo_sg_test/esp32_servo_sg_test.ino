#include <Arduino.h>

static const int SG1_PIN = 26;

// Mở rộng dải xung để servo có thể quay gần 180 độ hơn
// Nếu servo kêu gằn/gồng ở 2 đầu thì giảm lại, ví dụ 600-2400
static const int SERVO_MIN_US = 500;
static const int SERVO_MAX_US = 2500;

static const int SERVO_PERIOD_US = 20000;

static const int SG1_HOME_ANGLE = 0;
static const int SG1_TARGET_ANGLE = 180;

static const int STEP_DEGREES = 3;
static const int STEP_HOLD_PULSES = 1;
static const unsigned long END_HOLD_MS = 300;

int angleToPulseUs(int angle) {
  angle = constrain(angle, 0, 180);
  return map(angle, 0, 180, SERVO_MIN_US, SERVO_MAX_US);
}

void writeServoPulse(int angle) {
  int pulseUs = angleToPulseUs(angle);

  digitalWrite(SG1_PIN, HIGH);
  delayMicroseconds(pulseUs);

  digitalWrite(SG1_PIN, LOW);
  delayMicroseconds(SERVO_PERIOD_US - pulseUs);
}

void holdServoAngle(int angle, int pulses) {
  for (int i = 0; i < pulses; i++) {
    writeServoPulse(angle);
  }
}

void sweepServoFast(int fromAngle, int toAngle) {
  int step = (toAngle >= fromAngle) ? STEP_DEGREES : -STEP_DEGREES;

  if (step > 0) {
    for (int angle = fromAngle; angle <= toAngle; angle += step) {
      holdServoAngle(angle, STEP_HOLD_PULSES);
    }
  } else {
    for (int angle = fromAngle; angle >= toAngle; angle += step) {
      holdServoAngle(angle, STEP_HOLD_PULSES);
    }
  }

  holdServoAngle(toAngle, 5);
}

void setup() {
  Serial.begin(115200);
  delay(100);

  pinMode(SG1_PIN, OUTPUT);
  digitalWrite(SG1_PIN, LOW);

  Serial.println("ESP32 SG1 Servo 180 Degree Test");
  Serial.println("Manual pulse only, no Servo library");

  holdServoAngle(0, 50);
}

void loop() {
  Serial.println("SG1 -> 180 deg");
  sweepServoFast(SG1_HOME_ANGLE, SG1_TARGET_ANGLE);
  delay(END_HOLD_MS);

  Serial.println("Hold 180 deg");
  holdServoAngle(180, 50);
  delay(END_HOLD_MS);

  Serial.println("SG1 -> 0 deg");
  sweepServoFast(SG1_TARGET_ANGLE, SG1_HOME_ANGLE);
  delay(END_HOLD_MS);

  Serial.println("Hold 0 deg");
  holdServoAngle(0, 50);
  delay(END_HOLD_MS);
}