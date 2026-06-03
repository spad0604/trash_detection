/*
 * ESP32 - Actuator Servo Config / Tuning Sketch
 *
 * Purpose:
 *  - Tune angles for:
 *      SG1: drop tray (positional servo)
 *      SG2: bin selector (positional servo)
 *  - Test/control SG3 as a continuous (360) servo for the lid mechanism.
 *
 * Wiring/pins match robot/MCU/esp32_actuator/esp32_actuator.ino
 *
 * Serial protocol (USB / UART0):
 *   HELP
 *   STATUS
 *
 *   CMD:CLASSIFY:<0|1|2>   -> OPEN lid, select bin, drop, (optional CLOSE)
 *   CMD:OPEN               -> run lid "open" command for OPEN_MS, then STOP
 *   CMD:CLOSE              -> run lid "close" command for CLOSE_MS, then STOP
 *
 *   CMD:SET:DROP_HOME:<deg>
 *   CMD:SET:DROP_RELEASE:<deg>
 *   CMD:SET:BIN0:<deg>
 *   CMD:SET:BIN1:<deg>
 *   CMD:SET:BIN2:<deg>
 *
 *   CMD:SET:SELECT_SETTLE_MS:<ms>
 *   CMD:SET:DROP_HOLD_MS:<ms>
 *   CMD:SET:DROP_RETURN_MS:<ms>
 *
 * Continuous servo (SG3) raw control:
 *   CMD:LID:RAW:<0..180>        -> write raw value immediately
 *   CMD:LID:STOP:<0..180>       -> value that stops the continuous servo (often ~90)
 *   CMD:LID:OPEN_CMD:<0..180>   -> value used to open (direction/speed)
 *   CMD:LID:CLOSE_CMD:<0..180>  -> value used to close
 *   CMD:LID:OPEN_MS:<ms>
 *   CMD:LID:CLOSE_MS:<ms>
 *
 * Notes:
 *  - For continuous servos: write(90) usually stops, <90 / >90 changes direction.
 *  - If your lid servo is actually positional (not continuous), use CMD:LID:RAW:<angle>
 */

#include <Arduino.h>
#include <ESP32Servo.h>

#define PI_SERIAL Serial

// Servo pins (same as actuator sketch)
static const int SG1_DROP_PIN = 33;
static const int SG2_SELECT_PIN = 26;
static const int SG3_LID_PIN = 25;

static const uint32_t PI_BAUD = 115200;

static const int SERVO_MIN_US = 500;
static const int SERVO_MAX_US = 2400;

// Defaults copied from actuator sketch (you will tune these)
int dropHomeAngle = 0;
int dropReleaseAngle = 45;
int binAngles[3] = {0, 100, 270};

// SG2 (bin selector) advanced tuning
// - Many servo libraries map write(angle) to 0..180 only.
// - If your selector servo is actually a 270-degree positional servo, try the microseconds mode.
bool selectUseMicroseconds = false;
int binUs[3] = {1000, 1500, 2000};
int selectMinUs = SERVO_MIN_US;
int selectMaxUs = SERVO_MAX_US;

unsigned long selectSettleMs = 700;
unsigned long dropHoldMs = 700;
unsigned long dropReturnMs = 400;

// Continuous servo defaults (common values; tune with commands)
int lidStop = 90;
int lidOpenCmd = 120;
int lidCloseCmd = 60;
unsigned long lidOpenMs = 900;
unsigned long lidCloseMs = 900;

// If you want classify to close lid automatically after dropping
bool autoCloseAfterClassify = true;

Servo servoDrop;
Servo servoSelect;
Servo servoLid;

static int clampAngle(int v) {
  return constrain(v, 0, 180);
}

static int clampUs(int us) {
  // Keep within a generally safe range, then within configured attach limits.
  us = constrain(us, 500, 3000);
  int lo = min(selectMinUs, selectMaxUs);
  int hi = max(selectMinUs, selectMaxUs);
  return constrain(us, lo, hi);
}

static unsigned long clampMs(long v) {
  if (v < 0) return 0;
  if (v > 60000) return 60000;
  return (unsigned long)v;
}

static void printHelp() {
  PI_SERIAL.println("=== ESP32 Servo Config ===");
  PI_SERIAL.println("HELP");
  PI_SERIAL.println("STATUS");
  PI_SERIAL.println();
  PI_SERIAL.println("CMD:CLASSIFY:<0|1|2>");
  PI_SERIAL.println("CMD:AUTO_CLOSE:<0|1>");
  PI_SERIAL.println("CMD:OPEN");
  PI_SERIAL.println("CMD:CLOSE");
  PI_SERIAL.println();
  PI_SERIAL.println("CMD:SET:DROP_HOME:<deg>");
  PI_SERIAL.println("CMD:SET:DROP_RELEASE:<deg>");
  PI_SERIAL.println("CMD:SET:BIN0:<deg>");
  PI_SERIAL.println("CMD:SET:BIN1:<deg>");
  PI_SERIAL.println("CMD:SET:BIN2:<deg>");
  PI_SERIAL.println("CMD:SELECT:MODE:<DEG|US>");
  PI_SERIAL.println("CMD:SET:BIN0_US:<us>");
  PI_SERIAL.println("CMD:SET:BIN1_US:<us>");
  PI_SERIAL.println("CMD:SET:BIN2_US:<us>");
  PI_SERIAL.println("CMD:SEL:US:<us>  (direct selector microseconds test)");
  PI_SERIAL.println("CMD:SET:SELECT_MIN_US:<us>");
  PI_SERIAL.println("CMD:SET:SELECT_MAX_US:<us>");
  PI_SERIAL.println();
  PI_SERIAL.println("CMD:SET:SELECT_SETTLE_MS:<ms>");
  PI_SERIAL.println("CMD:SET:DROP_HOLD_MS:<ms>");
  PI_SERIAL.println("CMD:SET:DROP_RETURN_MS:<ms>");
  PI_SERIAL.println();
  PI_SERIAL.println("CMD:LID:RAW:<0..180>");
  PI_SERIAL.println("CMD:LID:STOP:<0..180>");
  PI_SERIAL.println("CMD:LID:OPEN_CMD:<0..180>");
  PI_SERIAL.println("CMD:LID:CLOSE_CMD:<0..180>");
  PI_SERIAL.println("CMD:LID:OPEN_MS:<ms>");
  PI_SERIAL.println("CMD:LID:CLOSE_MS:<ms>");
  PI_SERIAL.println("CMD:LID:SWING  (demo: 180 for 500ms, stop, 45 for 500ms, stop)");
  PI_SERIAL.println("CMD:LID:SWING:<fwd>,<rev>,<run_ms>,<pause_ms>");
  PI_SERIAL.println();
  PI_SERIAL.println("Tip: For continuous servo, find STOP first (often 90). Then tune OPEN_CMD/CLOSE_CMD and durations.");
}

static void printStatus() {
  PI_SERIAL.println("STATUS:CONFIG");

  PI_SERIAL.print("DROP_HOME=");
  PI_SERIAL.print(dropHomeAngle);
  PI_SERIAL.print(" DROP_RELEASE=");
  PI_SERIAL.println(dropReleaseAngle);

  PI_SERIAL.print("BIN_ANGLES=");
  PI_SERIAL.print(binAngles[0]);
  PI_SERIAL.print(",");
  PI_SERIAL.print(binAngles[1]);
  PI_SERIAL.print(",");
  PI_SERIAL.println(binAngles[2]);

  PI_SERIAL.print("SELECT_MODE=");
  PI_SERIAL.print(selectUseMicroseconds ? "US" : "DEG");
  PI_SERIAL.print(" SELECT_LIMITS_US=");
  PI_SERIAL.print(selectMinUs);
  PI_SERIAL.print("..");
  PI_SERIAL.println(selectMaxUs);

  PI_SERIAL.print("BIN_US=");
  PI_SERIAL.print(binUs[0]);
  PI_SERIAL.print(",");
  PI_SERIAL.print(binUs[1]);
  PI_SERIAL.print(",");
  PI_SERIAL.println(binUs[2]);

  PI_SERIAL.print("SELECT_SETTLE_MS=");
  PI_SERIAL.print(selectSettleMs);
  PI_SERIAL.print(" DROP_HOLD_MS=");
  PI_SERIAL.print(dropHoldMs);
  PI_SERIAL.print(" DROP_RETURN_MS=");
  PI_SERIAL.println(dropReturnMs);

  PI_SERIAL.print("LID_STOP=");
  PI_SERIAL.print(lidStop);
  PI_SERIAL.print(" LID_OPEN_CMD=");
  PI_SERIAL.print(lidOpenCmd);
  PI_SERIAL.print(" LID_CLOSE_CMD=");
  PI_SERIAL.print(lidCloseCmd);
  PI_SERIAL.print(" LID_OPEN_MS=");
  PI_SERIAL.print(lidOpenMs);
  PI_SERIAL.print(" LID_CLOSE_MS=");
  PI_SERIAL.println(lidCloseMs);

  PI_SERIAL.print("AUTO_CLOSE_AFTER_CLASSIFY=");
  PI_SERIAL.println(autoCloseAfterClassify ? 1 : 0);
}

static void lidWrite(int raw) {
  raw = clampAngle(raw);
  servoLid.write(raw);

  PI_SERIAL.print("STATUS:LID_RAW:");
  PI_SERIAL.println(raw);
}

static void lidStopNow() {
  lidWrite(lidStop);
}

static void openLidOnce() {
  PI_SERIAL.println("STATUS:LID_OPENING");
  lidWrite(lidOpenCmd);
  delay(lidOpenMs);
  lidStopNow();
  PI_SERIAL.println("STATUS:LID_OPEN_DONE");
}

static void closeLidOnce() {
  PI_SERIAL.println("STATUS:LID_CLOSING");
  lidWrite(lidCloseCmd);
  delay(lidCloseMs);
  lidStopNow();
  PI_SERIAL.println("STATUS:LID_CLOSE_DONE");
}

static void selectBin(int bin) {
  bin = constrain(bin, 0, 2);
  PI_SERIAL.print("STATUS:SELECT_BIN:");
  PI_SERIAL.print(bin);
  PI_SERIAL.print(":MODE=");
  PI_SERIAL.print(selectUseMicroseconds ? "US" : "DEG");

  if (selectUseMicroseconds) {
    int requestedUs = binUs[bin];
    int actualUs = clampUs(requestedUs);
    PI_SERIAL.print(":REQ_US=");
    PI_SERIAL.print(requestedUs);
    PI_SERIAL.print(":ACT_US=");
    PI_SERIAL.println(actualUs);
    servoSelect.writeMicroseconds(actualUs);
  } else {
    int requestedDeg = binAngles[bin];
    int actualDeg = clampAngle(requestedDeg);
    PI_SERIAL.print(":REQ=");
    PI_SERIAL.print(requestedDeg);
    PI_SERIAL.print(":ACT=");
    PI_SERIAL.println(actualDeg);
    servoSelect.write(actualDeg);
  }

  delay(selectSettleMs);
}

static void dropTrashOnce() {
  PI_SERIAL.println("STATUS:DROP");
  servoDrop.write(clampAngle(dropReleaseAngle));
  delay(dropHoldMs);
  servoDrop.write(clampAngle(dropHomeAngle));
  delay(dropReturnMs);
  PI_SERIAL.println("STATUS:DROP_DONE");
}

static long parseValueAfterColon(const String &cmd) {
  int idx = cmd.lastIndexOf(':');
  if (idx < 0 || idx + 1 >= cmd.length()) {
    return 0;
  }
  return cmd.substring(idx + 1).toInt();
}

static bool parse4CsvInts(const String &csv, int &a, int &b, long &c, long &d) {
  int i1 = csv.indexOf(',');
  if (i1 < 0) return false;
  int i2 = csv.indexOf(',', i1 + 1);
  if (i2 < 0) return false;
  int i3 = csv.indexOf(',', i2 + 1);
  if (i3 < 0) return false;

  a = csv.substring(0, i1).toInt();
  b = csv.substring(i1 + 1, i2).toInt();
  c = csv.substring(i2 + 1, i3).toInt();
  d = csv.substring(i3 + 1).toInt();
  return true;
}

static void handleCommand(const String &cmd) {
  if (cmd == "HELP") {
    printHelp();
    return;
  }

  if (cmd == "STATUS") {
    printStatus();
    return;
  }

  if (cmd == "CMD:OPEN") {
    openLidOnce();
    return;
  }

  if (cmd == "CMD:CLOSE") {
    closeLidOnce();
    return;
  }

  if (cmd.startsWith("CMD:AUTO_CLOSE:")) {
    int v = cmd.substring(15).toInt();
    autoCloseAfterClassify = (v != 0);
    PI_SERIAL.print("STATUS:SET:AUTO_CLOSE:");
    PI_SERIAL.println(autoCloseAfterClassify ? 1 : 0);
    return;
  }

  if (cmd.startsWith("CMD:CLASSIFY:")) {
    int bin = constrain(cmd.substring(13).toInt(), 0, 2);
    PI_SERIAL.print("STATUS:CLASSIFY:");
    PI_SERIAL.println(bin);

    openLidOnce();
    selectBin(bin);
    dropTrashOnce();
    if (autoCloseAfterClassify) {
      closeLidOnce();
    }

    PI_SERIAL.println("STATUS:CLASSIFY_DONE");
    return;
  }

  // Angle tuning
  if (cmd.startsWith("CMD:SET:DROP_HOME:")) {
    dropHomeAngle = clampAngle((int)parseValueAfterColon(cmd));
    PI_SERIAL.print("STATUS:SET:DROP_HOME:");
    PI_SERIAL.println(dropHomeAngle);
    servoDrop.write(dropHomeAngle);
    return;
  }

  if (cmd.startsWith("CMD:SET:DROP_RELEASE:")) {
    dropReleaseAngle = clampAngle((int)parseValueAfterColon(cmd));
    PI_SERIAL.print("STATUS:SET:DROP_RELEASE:");
    PI_SERIAL.println(dropReleaseAngle);
    servoDrop.write(dropReleaseAngle);
    return;
  }

  if (cmd.startsWith("CMD:SET:BIN0:")) {
    binAngles[0] = clampAngle((int)parseValueAfterColon(cmd));
    PI_SERIAL.print("STATUS:SET:BIN0:");
    PI_SERIAL.println(binAngles[0]);
    servoSelect.write(binAngles[0]);
    return;
  }

  if (cmd.startsWith("CMD:SET:BIN1:")) {
    binAngles[1] = clampAngle((int)parseValueAfterColon(cmd));
    PI_SERIAL.print("STATUS:SET:BIN1:");
    PI_SERIAL.println(binAngles[1]);
    servoSelect.write(binAngles[1]);
    return;
  }

  if (cmd.startsWith("CMD:SET:BIN2:")) {
    binAngles[2] = clampAngle((int)parseValueAfterColon(cmd));
    PI_SERIAL.print("STATUS:SET:BIN2:");
    PI_SERIAL.println(binAngles[2]);
    servoSelect.write(binAngles[2]);
    return;
  }

  // Selector mode + microseconds tuning
  if (cmd == "CMD:SELECT:MODE:DEG") {
    selectUseMicroseconds = false;
    PI_SERIAL.println("STATUS:SET:SELECT_MODE:DEG");
    return;
  }

  if (cmd == "CMD:SELECT:MODE:US") {
    selectUseMicroseconds = true;
    PI_SERIAL.println("STATUS:SET:SELECT_MODE:US");
    return;
  }

  if (cmd.startsWith("CMD:SET:BIN0_US:")) {
    binUs[0] = clampUs((int)parseValueAfterColon(cmd));
    PI_SERIAL.print("STATUS:SET:BIN0_US:");
    PI_SERIAL.println(binUs[0]);
    servoSelect.writeMicroseconds(binUs[0]);
    return;
  }

  if (cmd.startsWith("CMD:SET:BIN1_US:")) {
    binUs[1] = clampUs((int)parseValueAfterColon(cmd));
    PI_SERIAL.print("STATUS:SET:BIN1_US:");
    PI_SERIAL.println(binUs[1]);
    servoSelect.writeMicroseconds(binUs[1]);
    return;
  }

  if (cmd.startsWith("CMD:SET:BIN2_US:")) {
    binUs[2] = clampUs((int)parseValueAfterColon(cmd));
    PI_SERIAL.print("STATUS:SET:BIN2_US:");
    PI_SERIAL.println(binUs[2]);
    servoSelect.writeMicroseconds(binUs[2]);
    return;
  }

  if (cmd.startsWith("CMD:SEL:US:")) {
    int us = clampUs((int)parseValueAfterColon(cmd));
    PI_SERIAL.print("STATUS:SEL_US:");
    PI_SERIAL.println(us);
    servoSelect.writeMicroseconds(us);
    return;
  }

  if (cmd.startsWith("CMD:SET:SELECT_MIN_US:")) {
    selectMinUs = constrain((int)parseValueAfterColon(cmd), 500, 3000);
    if (selectMinUs > selectMaxUs) {
      int tmp = selectMaxUs;
      selectMaxUs = selectMinUs;
      selectMinUs = tmp;
    }
    PI_SERIAL.print("STATUS:SET:SELECT_MIN_US:");
    PI_SERIAL.println(selectMinUs);
    servoSelect.detach();
    servoSelect.attach(SG2_SELECT_PIN, selectMinUs, selectMaxUs);
    return;
  }

  if (cmd.startsWith("CMD:SET:SELECT_MAX_US:")) {
    selectMaxUs = constrain((int)parseValueAfterColon(cmd), 500, 3000);
    if (selectMinUs > selectMaxUs) {
      int tmp = selectMaxUs;
      selectMaxUs = selectMinUs;
      selectMinUs = tmp;
    }
    PI_SERIAL.print("STATUS:SET:SELECT_MAX_US:");
    PI_SERIAL.println(selectMaxUs);
    servoSelect.detach();
    servoSelect.attach(SG2_SELECT_PIN, selectMinUs, selectMaxUs);
    return;
  }

  // Timing tuning
  if (cmd.startsWith("CMD:SET:SELECT_SETTLE_MS:")) {
    selectSettleMs = clampMs(parseValueAfterColon(cmd));
    PI_SERIAL.print("STATUS:SET:SELECT_SETTLE_MS:");
    PI_SERIAL.println(selectSettleMs);
    return;
  }

  if (cmd.startsWith("CMD:SET:DROP_HOLD_MS:")) {
    dropHoldMs = clampMs(parseValueAfterColon(cmd));
    PI_SERIAL.print("STATUS:SET:DROP_HOLD_MS:");
    PI_SERIAL.println(dropHoldMs);
    return;
  }

  if (cmd.startsWith("CMD:SET:DROP_RETURN_MS:")) {
    dropReturnMs = clampMs(parseValueAfterColon(cmd));
    PI_SERIAL.print("STATUS:SET:DROP_RETURN_MS:");
    PI_SERIAL.println(dropReturnMs);
    return;
  }

  // Lid continuous servo control
  if (cmd.startsWith("CMD:LID:RAW:")) {
    lidWrite((int)parseValueAfterColon(cmd));
    return;
  }

  if (cmd.startsWith("CMD:LID:STOP:")) {
    lidStop = clampAngle((int)parseValueAfterColon(cmd));
    PI_SERIAL.print("STATUS:SET:LID_STOP:");
    PI_SERIAL.println(lidStop);
    lidStopNow();
    return;
  }

  if (cmd.startsWith("CMD:LID:OPEN_CMD:")) {
    lidOpenCmd = clampAngle((int)parseValueAfterColon(cmd));
    PI_SERIAL.print("STATUS:SET:LID_OPEN_CMD:");
    PI_SERIAL.println(lidOpenCmd);
    lidWrite(lidOpenCmd);
    return;
  }

  if (cmd.startsWith("CMD:LID:CLOSE_CMD:")) {
    lidCloseCmd = clampAngle((int)parseValueAfterColon(cmd));
    PI_SERIAL.print("STATUS:SET:LID_CLOSE_CMD:");
    PI_SERIAL.println(lidCloseCmd);
    lidWrite(lidCloseCmd);
    return;
  }

  if (cmd.startsWith("CMD:LID:OPEN_MS:")) {
    lidOpenMs = clampMs(parseValueAfterColon(cmd));
    PI_SERIAL.print("STATUS:SET:LID_OPEN_MS:");
    PI_SERIAL.println(lidOpenMs);
    return;
  }

  if (cmd.startsWith("CMD:LID:CLOSE_MS:")) {
    lidCloseMs = clampMs(parseValueAfterColon(cmd));
    PI_SERIAL.print("STATUS:SET:LID_CLOSE_MS:");
    PI_SERIAL.println(lidCloseMs);
    return;
  }

  // Convenience timed swing test for continuous lid servo
  // Demo: fwd=180 for 500ms, stop, rev=45 for 500ms, stop
  if (cmd == "CMD:LID:SWING") {
    int fwd = 180;
    int rev = 45;
    unsigned long runMs = 500;
    unsigned long pauseMs = 200;

    PI_SERIAL.println("STATUS:LID_SWING_START");
    lidWrite(fwd);
    delay(runMs);
    lidStopNow();
    delay(pauseMs);
    lidWrite(rev);
    delay(runMs);
    lidStopNow();
    PI_SERIAL.println("STATUS:LID_SWING_DONE");
    return;
  }

  if (cmd.startsWith("CMD:LID:SWING:")) {
    String args = cmd.substring(String("CMD:LID:SWING:").length());
    int fwd = 0;
    int rev = 0;
    long runMsLong = 0;
    long pauseMsLong = 0;
    if (!parse4CsvInts(args, fwd, rev, runMsLong, pauseMsLong)) {
      PI_SERIAL.println("STATUS:ERR:LID_SWING_ARGS");
      return;
    }

    unsigned long runMs = clampMs(runMsLong);
    unsigned long pauseMs = clampMs(pauseMsLong);

    PI_SERIAL.print("STATUS:LID_SWING_START:FWD=");
    PI_SERIAL.print(clampAngle(fwd));
    PI_SERIAL.print(":REV=");
    PI_SERIAL.print(clampAngle(rev));
    PI_SERIAL.print(":RUN_MS=");
    PI_SERIAL.print(runMs);
    PI_SERIAL.print(":PAUSE_MS=");
    PI_SERIAL.println(pauseMs);

    lidWrite(fwd);
    delay(runMs);
    lidStopNow();
    delay(pauseMs);
    lidWrite(rev);
    delay(runMs);
    lidStopNow();
    PI_SERIAL.println("STATUS:LID_SWING_DONE");
    return;
  }

  PI_SERIAL.print("STATUS:ERR:UNKNOWN_CMD:");
  PI_SERIAL.println(cmd);
}

void setup() {
  PI_SERIAL.begin(PI_BAUD);
  PI_SERIAL.setTimeout(20);
  delay(200);

  servoDrop.setPeriodHertz(50);
  servoSelect.setPeriodHertz(50);
  servoLid.setPeriodHertz(50);

  servoDrop.attach(SG1_DROP_PIN, SERVO_MIN_US, SERVO_MAX_US);
  servoSelect.attach(SG2_SELECT_PIN, selectMinUs, selectMaxUs);
  servoLid.attach(SG3_LID_PIN, SERVO_MIN_US, SERVO_MAX_US);

  servoDrop.write(clampAngle(dropHomeAngle));
  servoSelect.write(clampAngle(binAngles[0]));
  lidStopNow();

  PI_SERIAL.println("STATUS:READY");
  printHelp();
  printStatus();
}

void loop() {
  while (PI_SERIAL.available()) {
    String cmd = PI_SERIAL.readStringUntil('\n');
    cmd.trim();
    if (cmd.length() == 0) {
      continue;
    }

    PI_SERIAL.print("STATUS:RX:");
    PI_SERIAL.println(cmd);
    handleCommand(cmd);
  }
}
