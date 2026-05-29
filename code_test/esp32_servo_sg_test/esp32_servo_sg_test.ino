// ESP32 3x SG Servo test (Serial command: servoIndex angle)
//
// Default pin map (based on typical net labels in the project schematic):
//   SG1 -> GPIO33
//   SG2 -> GPIO26
//   SG3 -> GPIO25
// If your PCB/wiring differs, change SERVO_PINS below.
//
// Commands (send with newline):
//   help
//   1 90        -> servo 1 to 90 degrees
//   2 45        -> servo 2 to 45 degrees
//   3 120       -> servo 3 to 120 degrees
//   all 90      -> all servos to 90 degrees
//   u1 1500     -> servo 1 pulse width (microseconds)
//   status      -> print pins + last angles
//
// Notes:
// - Power servos from a proper 5V supply. Tie ESP32 GND to servo GND.
// - Do NOT power servos from ESP32 3V3.

#include <Arduino.h>
#include <ESP32Servo.h>

static const int SERVO_COUNT = 3;
static const int SERVO_PINS[SERVO_COUNT] = {
  33, // SG1
  26, // SG2
  25  // SG3
};

static const int SERVO_MIN_US = 500;
static const int SERVO_MAX_US = 2400;

// Continuous-rotation (360°) servo support:
// - Angle commands become a speed command:
//     90 ~ stop, <90 rotate one way, >90 rotate the other way.
// - Neutral differs per servo. Tune with: c<idx> <center_us> (e.g. c3 1490)
// - Preferred control: v<idx> <percent> where percent is -100..100.
static const bool SERVO_CONTINUOUS[SERVO_COUNT] = {true, true, true};

// Neutral pulse width (us) for stop (continuous) or mid-position (standard).
static int servoCenterUs[SERVO_COUNT] = {1500, 1500, 1500};

// Range (us) from center to max speed (continuous). Typical: 300..500.
static int servoRangeUs[SERVO_COUNT] = {400, 400, 400};

// Center angle used when you send a negative value as an offset.
// Example: "3 -30" -> target angle = SERVO_CENTER_ANGLE[2] + (-30) = 60.
static const int SERVO_CENTER_ANGLE[SERVO_COUNT] = {90, 90, 90};

Servo servos[SERVO_COUNT];
// For continuous servos, 90 ~= stop. Start all at 90 to avoid sudden spin on boot.
int lastAngle[SERVO_COUNT] = {90, 90, 90};

static void printHelp();
static void printStatus();
static bool parseTwoTokens(const String &s, String &a, String &b);
static bool isInteger(const String &s);
static void setServoAngle(int index0, int angle);
static void setServoMicros(int index0, int us);
static void setServoSpeedPercent(int index0, int percent);

void setup() {
  Serial.begin(115200);
  delay(100);

  for (int i = 0; i < SERVO_COUNT; i++) {
    servos[i].setPeriodHertz(50);
    // attach(pin, min, max) sets the pulse width range
    servos[i].attach(SERVO_PINS[i], SERVO_MIN_US, SERVO_MAX_US);
    servos[i].write(lastAngle[i]);
  }

  Serial.println("ESP32 SG Servo Test Ready");
  printStatus();
  printHelp();
}

void loop() {
  if (!Serial.available()) {
    delay(5);
    return;
  }

  String cmd = Serial.readStringUntil('\n');
  cmd.trim();
  cmd.toLowerCase();
  if (cmd.length() == 0) return;

  if (cmd == "help" || cmd == "h" || cmd == "?") {
    printHelp();
    return;
  }

  if (cmd == "status" || cmd == "st") {
    printStatus();
    return;
  }

  if (cmd.startsWith("all")) {
    String a, b;
    if (!parseTwoTokens(cmd, a, b) || a != "all" || !isInteger(b)) {
      Serial.println("ERR: use 'all <angle>'");
      return;
    }
    int angle = constrain(b.toInt(), 0, 180);
    for (int i = 0; i < SERVO_COUNT; i++) {
      setServoAngle(i, angle);
    }
    Serial.printf("OK: all -> %d\n", angle);
    return;
  }

  // Center calibration for continuous servos: c<idx> <center_us>
  // Example: c3 1490
  if (cmd.startsWith("c")) {
    String a, b;
    if (!parseTwoTokens(cmd, a, b) || a.length() < 2) {
      Serial.println("ERR: use 'c<1..3> <center_us>' e.g. c3 1490");
      return;
    }
    String idxStr = a.substring(1);
    if (!isInteger(idxStr) || !isInteger(b)) {
      Serial.println("ERR: invalid c command");
      return;
    }
    int idx1 = idxStr.toInt();
    if (idx1 < 1 || idx1 > SERVO_COUNT) {
      Serial.println("ERR: servo index must be 1..3");
      return;
    }
    int center = constrain(b.toInt(), SERVO_MIN_US, SERVO_MAX_US);
    servoCenterUs[idx1 - 1] = center;
    // Apply immediately (stop / center)
    setServoMicros(idx1 - 1, center);
    Serial.printf("OK: c%d -> %dus\n", idx1, center);
    return;
  }

  // Speed percent for continuous servos: v<idx> <percent>
  // Example: v3 -25 (slow one way), v3 0 (stop), v3 80 (fast other way)
  if (cmd.startsWith("v")) {
    String a, b;
    if (!parseTwoTokens(cmd, a, b) || a.length() < 2) {
      Serial.println("ERR: use 'v<1..3> <percent>' e.g. v3 -25");
      return;
    }
    String idxStr = a.substring(1);
    if (!isInteger(idxStr) || !isInteger(b)) {
      Serial.println("ERR: invalid v command");
      return;
    }
    int idx1 = idxStr.toInt();
    if (idx1 < 1 || idx1 > SERVO_COUNT) {
      Serial.println("ERR: servo index must be 1..3");
      return;
    }
    int percent = constrain(b.toInt(), -100, 100);
    setServoSpeedPercent(idx1 - 1, percent);
    Serial.printf("OK: v%d -> %d%%\n", idx1, percent);
    return;
  }

  // Pulse width command: u<idx> <micros>
  if (cmd.startsWith("u")) {
    String a, b;
    if (!parseTwoTokens(cmd, a, b) || a.length() < 2) {
      Serial.println("ERR: use 'u<1..3> <micros>' e.g. u1 1500");
      return;
    }
    String idxStr = a.substring(1);
    if (!isInteger(idxStr) || !isInteger(b)) {
      Serial.println("ERR: invalid u command");
      return;
    }
    int idx1 = idxStr.toInt();
    if (idx1 < 1 || idx1 > SERVO_COUNT) {
      Serial.println("ERR: servo index must be 1..3");
      return;
    }
    int us = b.toInt();
    setServoMicros(idx1 - 1, us);
    Serial.printf("OK: u%d -> %dus\n", idx1, constrain(us, SERVO_MIN_US, SERVO_MAX_US));
    return;
  }

  // Angle command (flexible separators):
  //   "1 90" | "1:90" | "1-90" | "sg1 90" | "s1 90"
  String a, b;
  if (!parseTwoTokens(cmd, a, b)) {
    // Try single token separators like 1:90 or 1-90
    int sepPos = cmd.indexOf(':');
    if (sepPos < 0) sepPos = cmd.indexOf('-');
    if (sepPos < 0) {
      Serial.println("ERR: use '<1..3> <angle>' or 'help'");
      return;
    }
    a = cmd.substring(0, sepPos);
    b = cmd.substring(sepPos + 1);
    a.trim();
    b.trim();
  }

  // Normalize a token like sg1 / s1 -> 1
  if (a.startsWith("sg")) a = a.substring(2);
  if (a.startsWith("s")) a = a.substring(1);

  if (!isInteger(a) || !isInteger(b)) {
    Serial.println("ERR: use '<1..3> <angle>'");
    return;
  }

  int idx1 = a.toInt();
  int angleOrOffset = b.toInt();
  if (idx1 < 1 || idx1 > SERVO_COUNT) {
    Serial.println("ERR: servo index must be 1..3");
    return;
  }

  // Angle semantics:
  // - Non-negative: absolute degrees 0..180
  // - Negative: offset from center (SERVO_CENTER_ANGLE)
  int targetAngle = 0;
  if (angleOrOffset < 0) {
    targetAngle = SERVO_CENTER_ANGLE[idx1 - 1] + angleOrOffset;
  } else {
    targetAngle = angleOrOffset;
  }
  targetAngle = constrain(targetAngle, 0, 180);
  setServoAngle(idx1 - 1, targetAngle);
  Serial.printf("OK: %d -> %d\n", idx1, targetAngle);
}

static void setServoAngle(int index0, int angle) {
  angle = constrain(angle, 0, 180);
  lastAngle[index0] = angle;
  if (SERVO_CONTINUOUS[index0]) {
    // Map 0..180 around center -> microseconds (more predictable for 360 servos)
    int center = servoCenterUs[index0];
    int range = servoRangeUs[index0];
    int us = map(angle, 0, 180, center - range, center + range);
    setServoMicros(index0, us);
  } else {
    servos[index0].write(angle);
  }
}

static void setServoMicros(int index0, int us) {
  us = constrain(us, SERVO_MIN_US, SERVO_MAX_US);
  servos[index0].writeMicroseconds(us);
}

static void setServoSpeedPercent(int index0, int percent) {
  percent = constrain(percent, -100, 100);
  int center = servoCenterUs[index0];
  int range = servoRangeUs[index0];
  int us = center + (percent * range) / 100;
  setServoMicros(index0, us);
}

static bool parseTwoTokens(const String &s, String &a, String &b) {
  int sp = s.indexOf(' ');
  if (sp < 0) return false;
  a = s.substring(0, sp);
  b = s.substring(sp + 1);
  a.trim();
  b.trim();
  return a.length() > 0 && b.length() > 0;
}

static bool isInteger(const String &s) {
  if (s.length() == 0) return false;
  int i = 0;
  if (s[0] == '+' || s[0] == '-') i = 1;
  if (i >= s.length()) return false;
  for (; i < s.length(); i++) {
    if (s[i] < '0' || s[i] > '9') return false;
  }
  return true;
}

static void printStatus() {
  Serial.println("Pins:");
  for (int i = 0; i < SERVO_COUNT; i++) {
    Serial.printf(
      "  SG%d -> GPIO%d (cont=%d last=%d centerDeg=%d centerUs=%d rangeUs=%d)\n",
      i + 1,
      SERVO_PINS[i],
      SERVO_CONTINUOUS[i] ? 1 : 0,
      lastAngle[i],
      SERVO_CENTER_ANGLE[i],
      servoCenterUs[i],
      servoRangeUs[i]
    );
  }
  Serial.printf("Pulse range: %dus..%dus\n", SERVO_MIN_US, SERVO_MAX_US);
}

static void printHelp() {
  Serial.println("Commands:");
  Serial.println("  help");
  Serial.println("  status");
  Serial.println("  1 90        (servo 1 to 90 deg)");
  Serial.println("  1 -30       (offset from center: center-30)");
  Serial.println("  2:45        (servo 2 to 45 deg)");
  Serial.println("  sg3 120     (servo 3 to 120 deg)");
  Serial.println("  all 90      (all servos to 90 deg)");
  Serial.println("  u1 1500     (servo 1 pulse width in us)");
  Serial.println("  c3 1490     (set neutral/center us for servo 3; stops it)");
  Serial.println("  v3 -25      (continuous servo speed %: -100..100, 0=stop)");
  Serial.println("Note (360 servo): 90 ~= stop; 10/170 ~= fast.");
}
