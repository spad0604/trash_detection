// ESP32 SG1 + SG2 + SG3 serial servo test without Servo library.
//
// SG1: horizontal selector servo -> GPIO26
// SG2: drop servo                -> GPIO33
// SG3: auxiliary/lid servo        -> GPIO25
//
// Serial commands:
//   1:30 -> SG1 to 30 deg
//   2:30 -> SG2 to 30 deg
//   3:30 -> SG3 to 30 deg
//   h or ? -> print help
//
// Manual 50 Hz servo pulses only. No Servo/ESP32Servo library.

#include <Arduino.h>

static const int SG1_PIN = 26;
static const int SG2_PIN = 33;
static const int SG3_PIN = 25;

static const int SERVO_MIN_US = 500;
static const int SERVO_MAX_US = 2500;
static const int SERVO_PERIOD_US = 20000;

static const int SG1_HOME_ANGLE = 0;
static const int SG2_HOME_ANGLE = 45;
static const int SG3_HOME_ANGLE = 0;

static const int STEP_DEGREES = 5;
static const int STEP_HOLD_PULSES = 1;
static const int COMMAND_HOLD_PULSES = 30;

int sg1Angle = SG1_HOME_ANGLE;
int sg2Angle = SG2_HOME_ANGLE;
int sg3Angle = SG3_HOME_ANGLE;

int angleToPulseUs(int angle) {
  angle = constrain(angle, 0, 180);
  return map(angle, 0, 180, SERVO_MIN_US, SERVO_MAX_US);
}

void writeServoFrame(int angle1, int angle2, int angle3) {
  int pulse1Us = angleToPulseUs(angle1);
  int pulse2Us = angleToPulseUs(angle2);
  int pulse3Us = angleToPulseUs(angle3);

  digitalWrite(SG1_PIN, HIGH);
  delayMicroseconds(pulse1Us);
  digitalWrite(SG1_PIN, LOW);

  digitalWrite(SG2_PIN, HIGH);
  delayMicroseconds(pulse2Us);
  digitalWrite(SG2_PIN, LOW);

  digitalWrite(SG3_PIN, HIGH);
  delayMicroseconds(pulse3Us);
  digitalWrite(SG3_PIN, LOW);

  delayMicroseconds(SERVO_PERIOD_US - pulse1Us - pulse2Us - pulse3Us);
}

void holdServos(int pulses) {
  for (int i = 0; i < pulses; i++) {
    writeServoFrame(sg1Angle, sg2Angle, sg3Angle);
  }
}

void moveSg1To(int targetAngle) {
  targetAngle = constrain(targetAngle, 0, 180);
  int step = (targetAngle >= sg1Angle) ? STEP_DEGREES : -STEP_DEGREES;

  for (int angle = sg1Angle; angle != targetAngle; angle += step) {
    sg1Angle = angle;
    holdServos(STEP_HOLD_PULSES);
  }

  sg1Angle = targetAngle;
  holdServos(COMMAND_HOLD_PULSES);
}

void moveSg2To(int targetAngle) {
  targetAngle = constrain(targetAngle, 0, 180);
  int step = (targetAngle >= sg2Angle) ? STEP_DEGREES : -STEP_DEGREES;

  for (int angle = sg2Angle; angle != targetAngle; angle += step) {
    sg2Angle = angle;
    holdServos(STEP_HOLD_PULSES);
  }

  sg2Angle = targetAngle;
  holdServos(COMMAND_HOLD_PULSES);
}

void moveSg3To(int targetAngle) {
  targetAngle = constrain(targetAngle, 0, 180);
  int step = (targetAngle >= sg3Angle) ? STEP_DEGREES : -STEP_DEGREES;

  for (int angle = sg3Angle; angle != targetAngle; angle += step) {
    sg3Angle = angle;
    holdServos(STEP_HOLD_PULSES);
  }

  sg3Angle = targetAngle;
  holdServos(COMMAND_HOLD_PULSES);
}

void moveServoTo(int servoIndex, int targetAngle) {
  targetAngle = constrain(targetAngle, 0, 180);
  switch (servoIndex) {
    case 1:
      Serial.printf("SG1: -> %d deg\n", targetAngle);
      moveSg1To(targetAngle);
      break;
    case 2:
      Serial.printf("SG2: -> %d deg\n", targetAngle);
      moveSg2To(targetAngle);
      break;
    case 3:
      Serial.printf("SG3: -> %d deg\n", targetAngle);
      moveSg3To(targetAngle);
      break;
    default:
      Serial.println("ERR: servo must be 1, 2, or 3");
      break;
  }
}

void printHelp() {
  Serial.println("Commands:");
  Serial.println("  1:30  -> SG1 -> 30 deg");
  Serial.println("  2:30  -> SG2 -> 30 deg");
  Serial.println("  3:180 -> SG3 -> 180 deg");
  Serial.println("  h/? -> help");
}

void handleSerial() {
  if (!Serial.available()) {
    return;
  }

  String cmd = Serial.readStringUntil('\n');
  cmd.trim();
  if (cmd.length() == 0) {
    return;
  }

  if (cmd == "h" || cmd == "H" || cmd == "?") {
    printHelp();
  } else {
    int colonIndex = cmd.indexOf(':');
    if (colonIndex <= 0 || colonIndex >= cmd.length() - 1) {
      Serial.println("ERR: use servo:angle, e.g. 1:30, 2:30, 3:180");
    } else {
      int servoIndex = cmd.substring(0, colonIndex).toInt();
      int targetAngle = cmd.substring(colonIndex + 1).toInt();
      moveServoTo(servoIndex, targetAngle);
    }
  }

  Serial.printf("NOW: SG1=%d deg, SG2=%d deg, SG3=%d deg\n", sg1Angle, sg2Angle, sg3Angle);
}

void setup() {
  Serial.begin(115200);
  delay(100);

  pinMode(SG1_PIN, OUTPUT);
  pinMode(SG2_PIN, OUTPUT);
  pinMode(SG3_PIN, OUTPUT);
  digitalWrite(SG1_PIN, LOW);
  digitalWrite(SG2_PIN, LOW);
  digitalWrite(SG3_PIN, LOW);

  Serial.println("ESP32 SG1 + SG2 + SG3 Serial Servo Test Ready");
  Serial.printf("SG1 horizontal -> GPIO%d\n", SG1_PIN);
  Serial.printf("SG2 drop       -> GPIO%d\n", SG2_PIN);
  Serial.printf("SG3 aux/lid    -> GPIO%d\n", SG3_PIN);
  Serial.println("Manual pulse only, no Servo library");
  printHelp();

  holdServos(80);
  Serial.printf("HOME: SG1=%d deg, SG2=%d deg, SG3=%d deg\n", sg1Angle, sg2Angle, sg3Angle);
}

void loop() {
  handleSerial();
  holdServos(1);
}
