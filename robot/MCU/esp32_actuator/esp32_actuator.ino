/*
 * ESP32 #2 - Actuator and Navigation Node
 *
 * Pi commands:
 *   CMD:CLASSIFY:<0|1|2>  -> SG2 selects bin, SG1 drops trash by 45 deg
 *   CMD:SERVO_OPEN        -> SG3 open position
 *   CMD:SERVO_CLOSE       -> SG3 close position
 *   CMD:MOVE_START        -> run PID line following
 *   CMD:MOVE_HOME         -> run PID line following, report ARRIVED as home
 *   CMD:MOVE_STOP         -> stop motors
 *   CMD:STATUS            -> send status + one telemetry snapshot now
 *   CMD:LED:<RED|GREEN|YELLOW|OFF>
 *
 * ESP32 -> Pi:
 *   STATUS:<...>
 *   ACT:<state>,<moving>,<bin>,<line_pos>,<active>,<raw1>..<raw5>,<str1>..<str5>
 */

#include <Arduino.h>
#include <ESP32Servo.h>

// The Raspberry Pi connects to this board through the ESP32 devkit Type-C port,
// so commands and telemetry must use the USB/UART0 serial bridge.
#define PI_SERIAL Serial

// Servo pins from code_test/esp32_servo_sg_test.
static const int SG1_DROP_PIN = 33;    // catch/drop tray
static const int SG2_SELECT_PIN = 26;  // horizontal bin selector
static const int SG3_AUX_PIN = 25;     // lid/aux mechanism

// L298N pins, same mapping as the working line test.
static const int LEFT_FORWARD_PIN = 23;   // IN_M1
static const int LEFT_BACKWARD_PIN = 22;  // IN_M2
static const int RIGHT_FORWARD_PIN = 19;  // IN_M3
static const int RIGHT_BACKWARD_PIN = 18; // IN_M4
static const bool LEFT_REVERSED = false;
static const bool RIGHT_REVERSED = false;

// 5-channel line sensor pins.
static const int LINE_PINS[5] = {36, 39, 34, 35, 32};
static const int LINE_WEIGHTS[5] = {-2000, -1000, 0, 1000, 2000};

static const int LED_RED = 4;
static const int LED_GREEN = 2;
static const int LED_YELLOW = 27;

static const uint32_t PI_BAUD = 115200;

static const int PWM_FREQ = 5000;
static const int PWM_RESOLUTION = 8;
static const int PWM_MAX = 255;

static const int SERVO_MIN_US = 500;
static const int SERVO_MAX_US = 2400;

// Tune on the real mechanism.
static const int DROP_HOME_ANGLE = 0;
static const int DROP_RELEASE_ANGLE = 45;
static const int AUX_CLOSED_ANGLE = 0;
static const int AUX_OPEN_ANGLE = 90;
static const int BIN_ANGLES[3] = {30, 90, 150};  // three 60-degree sectors over 180 degrees
static const unsigned long SELECT_SETTLE_MS = 700;
static const unsigned long DROP_HOLD_MS = 700;
static const unsigned long DROP_RETURN_MS = 400;
static const unsigned long TURN_AROUND_MS = 900;
static const int TURN_AROUND_SPEED = 130;

int blackCal[5] = {0, 0, 0, 0, 0};
int whiteCal[5] = {4095, 4095, 4095, 4095, 4095};
int detectThreshold = 200;

int baseSpeed = 130;
int minRunSpeed = 55;
int maxSpeed = 210;
int bridgeSpeed = 90;
int searchSpeed = 120;
float kp = 0.070f;
float ki = 0.000f;
float kd = 0.018f;

unsigned long bridgeLineGapMs = 120;
unsigned long searchLineGapMs = 900;
unsigned long endpointHoldMs = 250;
enum SystemState {
  STATE_IDLE,
  STATE_SORTING,
  STATE_MOVING,
  STATE_RETURNING_HOME,
  STATE_LINE_LOST
};

struct LineRead {
  int raw[5];
  int strength[5];
  int active[5];
  int activeCount;
  long position;
};

Servo servoDrop;
Servo servoSelect;
Servo servoAux;

SystemState currentState = STATE_IDLE;
int currentBin = 0;
bool moving = false;
bool lineLostReported = false;

unsigned long lastPidUs = 0;
unsigned long lastLineSeenMs = 0;
unsigned long endpointSeenSinceMs = 0;
float integral = 0.0f;
float lastError = 0.0f;
float lastSeenError = 0.0f;
int lastSearchDir = 1;
int lastLeftSpeed = 0;
int lastRightSpeed = 0;

LineRead lastLine;

void setupMotorPin(int pin) {
  pinMode(pin, OUTPUT);
  ledcAttach(pin, PWM_FREQ, PWM_RESOLUTION);
  ledcWrite(pin, 0);
}

void setup() {
  PI_SERIAL.begin(PI_BAUD);
  PI_SERIAL.setTimeout(20);
  delay(100);

  servoDrop.setPeriodHertz(50);
  servoSelect.setPeriodHertz(50);
  servoAux.setPeriodHertz(50);
  servoDrop.attach(SG1_DROP_PIN, SERVO_MIN_US, SERVO_MAX_US);
  servoSelect.attach(SG2_SELECT_PIN, SERVO_MIN_US, SERVO_MAX_US);
  servoAux.attach(SG3_AUX_PIN, SERVO_MIN_US, SERVO_MAX_US);
  servoDrop.write(DROP_HOME_ANGLE);
  servoSelect.write(BIN_ANGLES[currentBin]);
  servoAux.write(AUX_CLOSED_ANGLE);

  analogReadResolution(12);
  for (int i = 0; i < 5; i++) {
    pinMode(LINE_PINS[i], INPUT);
    analogSetPinAttenuation(LINE_PINS[i], ADC_11db);
    lastLine.raw[i] = 4095;
    lastLine.strength[i] = 0;
    lastLine.active[i] = 0;
  }
  lastLine.activeCount = 0;
  lastLine.position = 0;

  setupMotorPin(LEFT_FORWARD_PIN);
  setupMotorPin(LEFT_BACKWARD_PIN);
  setupMotorPin(RIGHT_FORWARD_PIN);
  setupMotorPin(RIGHT_BACKWARD_PIN);
  stopMotors();

  pinMode(LED_RED, OUTPUT);
  pinMode(LED_GREEN, OUTPUT);
  pinMode(LED_YELLOW, OUTPUT);
  allLedsOff();

  PI_SERIAL.println("[ESP32 Actuator] Ready");
  PI_SERIAL.println("STATUS:IDLE");
}

void loop() {
  handlePiCommands();

  lastLine = readLineSensors();
  if (currentState == STATE_MOVING || currentState == STATE_RETURNING_HOME) {
    followLinePid(lastLine);
  }
}

void handlePiCommands() {
  while (PI_SERIAL.available()) {
    String cmd = PI_SERIAL.readStringUntil('\n');
    cmd.trim();
    if (cmd.length() == 0) {
      continue;
    }

    PI_SERIAL.print("STATUS:RX:");
    PI_SERIAL.println(cmd);

    if (cmd == "CMD:SERVO_OPEN") {
      servoAux.write(AUX_OPEN_ANGLE);
      PI_SERIAL.println("STATUS:SERVO_OPENED");
    } else if (cmd == "CMD:SERVO_CLOSE") {
      servoAux.write(AUX_CLOSED_ANGLE);
      PI_SERIAL.println("STATUS:SERVO_CLOSED");
    } else if (cmd.startsWith("CMD:CLASSIFY:")) {
      int targetBin = constrain(cmd.substring(13).toInt(), 0, 2);
      sortToBin(targetBin);
    } else if (cmd == "CMD:MOVE_START") {
      startLineFollow(false);
    } else if (cmd == "CMD:MOVE_HOME") {
      startLineFollow(true);
    } else if (cmd == "CMD:MOVE_STOP") {
      stopMovement("STATUS:IDLE");
    } else if (cmd == "CMD:STATUS") {
      sendStatus();
      sendTelemetry();
    } else if (cmd == "CMD:LED:RED") {
      allLedsOff();
      digitalWrite(LED_RED, HIGH);
    } else if (cmd == "CMD:LED:GREEN") {
      allLedsOff();
      digitalWrite(LED_GREEN, HIGH);
    } else if (cmd == "CMD:LED:YELLOW") {
      allLedsOff();
      digitalWrite(LED_YELLOW, HIGH);
    } else if (cmd == "CMD:LED:OFF") {
      allLedsOff();
    }
  }
}

void sortToBin(int targetBin) {
  stopMotors();
  moving = false;
  currentState = STATE_SORTING;
  currentBin = targetBin;
  lineLostReported = false;
  allLedsOff();
  digitalWrite(LED_YELLOW, HIGH);

  char status[32];
  snprintf(status, sizeof(status), "STATUS:SORTING:%d", targetBin);
  PI_SERIAL.println(status);

  servoSelect.write(BIN_ANGLES[targetBin]);
  delay(SELECT_SETTLE_MS);

  servoDrop.write(DROP_RELEASE_ANGLE);
  delay(DROP_HOLD_MS);
  servoDrop.write(DROP_HOME_ANGLE);
  delay(DROP_RETURN_MS);

  allLedsOff();
  digitalWrite(LED_GREEN, HIGH);
  currentState = STATE_IDLE;
  PI_SERIAL.println("STATUS:SORT_DONE");
  sendTelemetry();
}

void startLineFollow(bool returningHome) {
  if (returningHome) {
    setMotorSpeeds(TURN_AROUND_SPEED, -TURN_AROUND_SPEED);
    delay(TURN_AROUND_MS);
    stopMotors();
  }

  currentState = returningHome ? STATE_RETURNING_HOME : STATE_MOVING;
  moving = true;
  lineLostReported = false;
  endpointSeenSinceMs = 0;
  integral = 0.0f;
  lastError = 0.0f;
  lastLineSeenMs = millis();
  lastPidUs = micros();
  allLedsOff();
  digitalWrite(LED_YELLOW, HIGH);
  PI_SERIAL.println("STATUS:MOVING");
}

void stopMovement(const char *statusLine) {
  stopMotors();
  moving = false;
  currentState = STATE_IDLE;
  endpointSeenSinceMs = 0;
  allLedsOff();
  PI_SERIAL.println(statusLine);
  sendTelemetry();
}

LineRead readLineSensors() {
  LineRead line;
  line.activeCount = 0;
  long weightedSum = 0;
  long strengthSum = 0;

  for (int i = 0; i < 5; i++) {
    int value = analogRead(LINE_PINS[i]);
    int strength = normalizedLineStrength(i, value);
    line.raw[i] = value;
    line.strength[i] = strength;
    line.active[i] = strength >= detectThreshold;

    if (line.active[i]) {
      line.activeCount++;
      weightedSum += (long)LINE_WEIGHTS[i] * strength;
      strengthSum += strength;
    }
  }

  line.position = (strengthSum > 0) ? (weightedSum / strengthSum) : 0;
  return line;
}

int normalizedLineStrength(int sensorIndex, int rawValue) {
  int span = whiteCal[sensorIndex] - blackCal[sensorIndex];
  if (span < 50) {
    span = 50;
  }
  long strength = ((long)whiteCal[sensorIndex] - rawValue) * 1000L / span;
  if (strength < 0) strength = 0;
  if (strength > 1000) strength = 1000;
  return (int)strength;
}

void followLinePid(const LineRead &line) {
  unsigned long nowMs = millis();

  if (line.activeCount >= 5) {
    if (endpointSeenSinceMs == 0) {
      endpointSeenSinceMs = nowMs;
    }
    if (nowMs - endpointSeenSinceMs >= endpointHoldMs) {
      if (currentState == STATE_RETURNING_HOME) {
        stopMovement("STATUS:ARRIVED_HOME");
      } else {
        stopMovement("STATUS:ARRIVED_DUMP");
      }
      return;
    }
  } else {
    endpointSeenSinceMs = 0;
  }

  unsigned long nowUs = micros();
  float dt = (lastPidUs == 0) ? 0.01f : (nowUs - lastPidUs) / 1000000.0f;
  lastPidUs = nowUs;
  if (dt <= 0.0f || dt > 0.2f) {
    dt = 0.01f;
  }

  if (line.activeCount == 0) {
    recoverLostLine(nowMs);
    return;
  }

  lineLostReported = false;
  lastLineSeenMs = nowMs;
  float error = (float)line.position;
  lastSeenError = error;
  if (error > 150.0f) {
    lastSearchDir = 1;
  } else if (error < -150.0f) {
    lastSearchDir = -1;
  }

  integral += error * dt;
  integral = constrain(integral, -5000.0f, 5000.0f);
  float derivative = (error - lastError) / dt;
  lastError = error;

  float correction = kp * error + ki * integral + kd * derivative;
  int leftSpeed = clampRunSpeed((int)(baseSpeed + correction));
  int rightSpeed = clampRunSpeed((int)(baseSpeed - correction));
  setMotorSpeeds(leftSpeed, rightSpeed);
  lastLeftSpeed = leftSpeed;
  lastRightSpeed = rightSpeed;
}

void recoverLostLine(unsigned long nowMs) {
  unsigned long lostMs = (lastLineSeenMs == 0) ? searchLineGapMs + 1 : nowMs - lastLineSeenMs;
  integral = 0.0f;

  if (lostMs <= bridgeLineGapMs) {
    int leftSpeed = clampRecoverySpeed(lastLeftSpeed, bridgeSpeed);
    int rightSpeed = clampRecoverySpeed(lastRightSpeed, bridgeSpeed);
    if (leftSpeed == 0 && rightSpeed == 0) {
      leftSpeed = bridgeSpeed;
      rightSpeed = bridgeSpeed;
    }
    setMotorSpeeds(leftSpeed, rightSpeed);
    return;
  }

  if (lostMs <= searchLineGapMs) {
    int dir = lastSearchDir;
    if (lastSeenError > 100.0f || lastSeenError < -100.0f) {
      dir = (lastSeenError > 0.0f) ? 1 : -1;
    }
    if (dir > 0) {
      setMotorSpeeds(searchSpeed, -searchSpeed);
    } else {
      setMotorSpeeds(-searchSpeed, searchSpeed);
    }
    return;
  }

  stopMotors();
  moving = false;
  currentState = STATE_LINE_LOST;
  if (!lineLostReported) {
    PI_SERIAL.println("STATUS:LINE_LOST");
    lineLostReported = true;
    sendTelemetry();
  }
}

int clampRecoverySpeed(int speed, int fallbackAbsSpeed) {
  if (speed == 0) {
    return 0;
  }
  int sign = speed > 0 ? 1 : -1;
  int magnitude = constrain(abs(speed), minRunSpeed, fallbackAbsSpeed);
  return sign * magnitude;
}

int clampRunSpeed(int speed) {
  if (speed == 0) {
    return 0;
  }
  if (speed > 0) {
    return constrain(speed, minRunSpeed, maxSpeed);
  }
  return constrain(speed, -maxSpeed, -minRunSpeed);
}

void setMotorSpeeds(int leftSpeed, int rightSpeed) {
  if (LEFT_REVERSED) {
    leftSpeed = -leftSpeed;
  }
  if (RIGHT_REVERSED) {
    rightSpeed = -rightSpeed;
  }
  driveMotor(LEFT_FORWARD_PIN, LEFT_BACKWARD_PIN, leftSpeed);
  driveMotor(RIGHT_FORWARD_PIN, RIGHT_BACKWARD_PIN, rightSpeed);
}

void driveMotor(int forwardPin, int backwardPin, int speed) {
  speed = constrain(speed, -PWM_MAX, PWM_MAX);
  if (speed > 0) {
    ledcWrite(forwardPin, speed);
    ledcWrite(backwardPin, 0);
  } else if (speed < 0) {
    ledcWrite(forwardPin, 0);
    ledcWrite(backwardPin, -speed);
  } else {
    ledcWrite(forwardPin, 0);
    ledcWrite(backwardPin, 0);
  }
}

void stopMotors() {
  ledcWrite(LEFT_FORWARD_PIN, 0);
  ledcWrite(LEFT_BACKWARD_PIN, 0);
  ledcWrite(RIGHT_FORWARD_PIN, 0);
  ledcWrite(RIGHT_BACKWARD_PIN, 0);
  lastLeftSpeed = 0;
  lastRightSpeed = 0;
}

void allLedsOff() {
  digitalWrite(LED_RED, LOW);
  digitalWrite(LED_GREEN, LOW);
  digitalWrite(LED_YELLOW, LOW);
}

const char *stateName() {
  switch (currentState) {
    case STATE_IDLE: return "IDLE";
    case STATE_SORTING: return "SORTING";
    case STATE_MOVING: return "MOVING";
    case STATE_RETURNING_HOME: return "RETURNING_HOME";
    case STATE_LINE_LOST: return "LINE_LOST";
  }
  return "UNKNOWN";
}

void sendStatus() {
  switch (currentState) {
    case STATE_IDLE: PI_SERIAL.println("STATUS:IDLE"); break;
    case STATE_SORTING: PI_SERIAL.println("STATUS:SORTING"); break;
    case STATE_MOVING:
    case STATE_RETURNING_HOME: PI_SERIAL.println("STATUS:MOVING"); break;
    case STATE_LINE_LOST: PI_SERIAL.println("STATUS:LINE_LOST"); break;
  }
}

void sendTelemetry() {
  PI_SERIAL.print("ACT:");
  PI_SERIAL.print(stateName());
  PI_SERIAL.print(",");
  PI_SERIAL.print(moving ? 1 : 0);
  PI_SERIAL.print(",");
  PI_SERIAL.print(currentBin);
  PI_SERIAL.print(",");
  PI_SERIAL.print(lastLine.position);
  PI_SERIAL.print(",");
  PI_SERIAL.print(lastLine.activeCount);
  for (int i = 0; i < 5; i++) {
    PI_SERIAL.print(",");
    PI_SERIAL.print(lastLine.raw[i]);
  }
  for (int i = 0; i < 5; i++) {
    PI_SERIAL.print(",");
    PI_SERIAL.print(lastLine.strength[i]);
  }
  PI_SERIAL.println();
}
