/*
 * ESP32 line PID test sketch
 *
 * Hardware mapping:
 *   Line sensors, left -> right: 36, 39, 34, 35, 32
 *   Left motor L298N: 23 (IN1), 22 (IN2)
 *   Right motor L298N: 19 (IN3), 21 (IN4)
 *
 * Serial commands:
 *   r -> start from the start marker and run forward to the end marker
 *   z -> continue forward on the circular line to the home marker
 *
 * The robot is expected to begin on a 5-sensor black marker. The first fully
 * covered marker is treated as the start zone, and the next fully covered
 * marker is treated as the stop zone.
 */

#include <Arduino.h>

#define PI_SERIAL Serial

static const int LEFT_FORWARD_PIN = 23;
static const int LEFT_BACKWARD_PIN = 22;
static const int RIGHT_FORWARD_PIN = 19;
static const int RIGHT_BACKWARD_PIN = 21;
static const bool LEFT_REVERSED = false;
static const bool RIGHT_REVERSED = false;

static const int LINE_PINS[5] = {36, 39, 34, 35, 32};
static const int LINE_WEIGHTS[5] = {-2000, -1000, 0, 1000, 2000};

static const uint32_t BAUD = 115200;
static const int PWM_FREQ = 5000;
static const int PWM_RESOLUTION = 8;
static const int PWM_MAX = 255;

static const int START_CLEAR_SPEED = 150;
static const int BASE_SPEED = 170;
static const int MIN_RUN_SPEED = 55;
static const int MAX_SPEED = 185;
static const int TURN_SLOW_SPEED = 55;

static const int ENDPOINT_MIN_ACTIVE = 3;
static const unsigned long START_IGNORE_MS = 1000;
static const unsigned long LOST_BRIDGE_MS = 120;
static const unsigned long TELEMETRY_MS = 250;

static int blackCal[5] = {0, 0, 0, 0, 0};
static int whiteCal[5] = {4095, 4095, 4095, 4095, 4095};
static int detectThreshold = 200;

static float kp = 0.060f;
static float ki = 0.000f;
static float kd = 0.120f;

enum RunState {
  IDLE,
  FORWARD_TEST,
  RETURN_FORWARD_TEST,
  LINE_LOST
};

struct LineRead {
  int raw[5];
  int strength[5];
  int active[5];
  int activeCount;
  long position;
};

RunState state = IDLE;
bool moving = false;
bool endpointArmed = true;
bool lineLostReported = false;

unsigned long lineLostSinceMs = 0;
unsigned long lastTelemetryMs = 0;
unsigned long testStartMs = 0;
long lastSeenPosition = 0;
int lastSearchDir = 1;
int lastLeftMagnitude = 105;
int lastRightMagnitude = 105;

float pidIntegral = 0.0f;
float pidLastError = 0.0f;

LineRead lastLine;

void sendTelemetry();

void setupMotorPin(int pin) {
  pinMode(pin, OUTPUT);
  ledcAttach(pin, PWM_FREQ, PWM_RESOLUTION);
  ledcWrite(pin, 0);
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
    line.active[i] = (strength >= detectThreshold);

    if (line.active[i]) {
      line.activeCount++;
      weightedSum += (long)LINE_WEIGHTS[i] * strength;
      strengthSum += strength;
    }
  }

  line.position = (strengthSum > 0) ? (weightedSum / strengthSum) : 0;
  return line;
}

int clampMagnitude(int speed) {
  return constrain(speed, MIN_RUN_SPEED, MAX_SPEED);
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

void stopMotors() {
  ledcWrite(LEFT_FORWARD_PIN, 0);
  ledcWrite(LEFT_BACKWARD_PIN, 0);
  ledcWrite(RIGHT_FORWARD_PIN, 0);
  ledcWrite(RIGHT_BACKWARD_PIN, 0);
}

void driveStraightSigned(int speedMagnitude) {
  int magnitude = clampMagnitude(speedMagnitude);
  setMotorSpeeds(magnitude, magnitude);
}

void drivePidSpeeds(int leftMagnitude, int rightMagnitude) {
  leftMagnitude = clampMagnitude(leftMagnitude);
  rightMagnitude = clampMagnitude(rightMagnitude);
  lastLeftMagnitude = leftMagnitude;
  lastRightMagnitude = rightMagnitude;
  setMotorSpeeds(leftMagnitude, rightMagnitude);
}

void stopAndReport(const char *statusLine) {
  stopMotors();
  moving = false;
  state = IDLE;
  endpointArmed = true;
  pidIntegral = 0.0f;
  pidLastError = 0.0f;
  lineLostSinceMs = 0;
  PI_SERIAL.println(statusLine);
  sendTelemetry();
}

void sendTelemetry() {
  if (PI_SERIAL.availableForWrite() < 96) {
    return;
  }

  PI_SERIAL.print("ACT:");
  PI_SERIAL.print((state == IDLE) ? "IDLE" : (state == FORWARD_TEST) ? "FORWARD" : (state == RETURN_FORWARD_TEST) ? "RETURN_FORWARD" : "LINE_LOST");
  PI_SERIAL.print(",");
  PI_SERIAL.print(moving ? 1 : 0);
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

void startTest(bool backward) {
  stopMotors();
  moving = true;
  state = backward ? RETURN_FORWARD_TEST : FORWARD_TEST;
  endpointArmed = false;
  lineLostReported = false;
  lineLostSinceMs = 0;
  lastTelemetryMs = 0;
  testStartMs = millis();
  pidIntegral = 0.0f;
  pidLastError = 0.0f;
  lastSearchDir = 1;
  PI_SERIAL.println(backward ? "STATUS:RUN_HOME" : "STATUS:RUN_FORWARD");
  sendTelemetry();
}

bool hasContiguousActiveBlock(const LineRead &line) {
  int first = -1;
  int last = -1;
  for (int i = 0; i < 5; i++) {
    if (line.active[i]) {
      if (first < 0) {
        first = i;
      }
      last = i;
    }
  }

  if (first < 0) {
    return false;
  }

  for (int i = first; i <= last; i++) {
    if (!line.active[i]) {
      return false;
    }
  }
  return true;
}

bool isEndpointMarkerCandidate(const LineRead &line) {
  unsigned long elapsedMs = millis() - testStartMs;
  if (elapsedMs < START_IGNORE_MS) {
    return false;
  }

  if (line.activeCount < ENDPOINT_MIN_ACTIVE) {
    return false;
  }

  if (!hasContiguousActiveBlock(line)) {
    return false;
  }

  return true;
}

bool clearStartMarker(const LineRead &line) {
  unsigned long elapsedMs = millis() - testStartMs;
  if (elapsedMs < START_IGNORE_MS && line.activeCount >= ENDPOINT_MIN_ACTIVE) {
    driveStraightSigned(START_CLEAR_SPEED);
    return true;
  }

  return false;
}

bool stopAtEndpoint(const LineRead &line) {
  if (!isEndpointMarkerCandidate(line)) {
    return false;
  }

  if (state == RETURN_FORWARD_TEST) {
    stopAndReport("STATUS:ARRIVED_START");
  } else {
    stopAndReport("STATUS:ARRIVED_END");
  }
  return true;
}

void followVisibleLine(const LineRead &line) {
  lineLostReported = false;
  lineLostSinceMs = 0;
  lastSeenPosition = line.position;

  if (line.position < -150) {
    lastSearchDir = -1;
  } else if (line.position > 150) {
    lastSearchDir = 1;
  }

  float error = (float)line.position;
  pidIntegral += error;
  pidIntegral = constrain(pidIntegral, -4000.0f, 4000.0f);
  float derivative = error - pidLastError;
  pidLastError = error;

  float output = (kp * error) + (ki * pidIntegral) + (kd * derivative);
  int correction = (int)output;

  int leftMagnitude = BASE_SPEED - correction;
  int rightMagnitude = BASE_SPEED + correction;

  if (line.position < -500) {
    leftMagnitude = TURN_SLOW_SPEED;
    rightMagnitude = BASE_SPEED;
  } else if (line.position > 500) {
    leftMagnitude = BASE_SPEED;
    rightMagnitude = TURN_SLOW_SPEED;
  }

  drivePidSpeeds(leftMagnitude, rightMagnitude);
}

void recoverLostLine() {
  unsigned long nowMs = millis();
  if (lineLostSinceMs == 0) {
    lineLostSinceMs = nowMs;
  }

  unsigned long lostMs = nowMs - lineLostSinceMs;
  if (lostMs <= LOST_BRIDGE_MS) {
    drivePidSpeeds(lastLeftMagnitude, lastRightMagnitude);
    return;
  }

  stopMotors();
  moving = false;
  state = LINE_LOST;
  endpointArmed = true;
  lineLostSinceMs = 0;
  if (!lineLostReported) {
    PI_SERIAL.println("STATUS:LINE_LOST");
    lineLostReported = true;
  }
}

void followLinePid(const LineRead &line) {
  if (clearStartMarker(line)) {
    return;
  }

  if (stopAtEndpoint(line)) {
    return;
  }

  if (line.activeCount == 0) {
    recoverLostLine();
    return;
  }

  followVisibleLine(line);
}

void handleCommands() {
  while (PI_SERIAL.available()) {
    String cmd = PI_SERIAL.readStringUntil('\n');
    cmd.trim();
    if (cmd.length() == 0) {
      continue;
    }

    if (cmd == "r" || cmd == "R") {
      startTest(false);
    } else if (cmd == "z" || cmd == "Z") {
      startTest(true);
    }
  }
}

void setup() {
  PI_SERIAL.begin(BAUD);
  PI_SERIAL.setTimeout(20);
  delay(100);

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

  PI_SERIAL.println("[ESP32 Line PID Test] Ready");
  PI_SERIAL.println("STATUS:IDLE");
}

void loop() {
  handleCommands();

  lastLine = readLineSensors();
  if (moving && (state == FORWARD_TEST || state == RETURN_FORWARD_TEST)) {
    followLinePid(lastLine);

    unsigned long nowMs = millis();
    if (nowMs - lastTelemetryMs >= TELEMETRY_MS) {
      lastTelemetryMs = nowMs;
      sendTelemetry();
    }
  }
}
