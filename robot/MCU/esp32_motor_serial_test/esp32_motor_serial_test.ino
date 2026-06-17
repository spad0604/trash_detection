/*
 * ESP32 2-wheel motor serial test
 *
 * Serial commands:
 *   w -> forward, both wheels
 *   s -> backward, both wheels
 *   a -> turn left
 *   d -> turn right
 *   z -> stop both wheels
 *
 * L298N mapping:
 *   Left motor:  GPIO23 forward, GPIO22 backward
 *   Right motor: GPIO19 forward, GPIO21 backward
 */

#include <Arduino.h>

static const int LEFT_FORWARD_PIN = 23;
static const int LEFT_BACKWARD_PIN = 22;
static const int RIGHT_FORWARD_PIN = 19;
static const int RIGHT_BACKWARD_PIN = 21;

static const bool LEFT_REVERSED = false;
static const bool RIGHT_REVERSED = false;

static const uint32_t BAUD = 115200;
static const int PWM_FREQ = 5000;
static const int PWM_RESOLUTION = 8;
static const int PWM_MAX = 255;
static const int TEST_SPEED = 180;
static const int TURN_SPEED = 170;

void setupMotorPin(int pin);
void stopMotors();
void setMotorSpeeds(int leftSpeed, int rightSpeed);
void driveMotor(int forwardPin, int backwardPin, int speed);
void printHelp();

void setup() {
  Serial.begin(BAUD);
  Serial.setTimeout(20);
  delay(100);

  setupMotorPin(LEFT_FORWARD_PIN);
  setupMotorPin(LEFT_BACKWARD_PIN);
  setupMotorPin(RIGHT_FORWARD_PIN);
  setupMotorPin(RIGHT_BACKWARD_PIN);
  stopMotors();

  Serial.println("[ESP32 Motor Serial Test] Ready");
  printHelp();
}

void loop() {
  if (!Serial.available()) {
    delay(5);
    return;
  }

  char cmd = Serial.read();
  while (Serial.available()) {
    char extra = Serial.peek();
    if (extra == '\n' || extra == '\r' || extra == ' ') {
      Serial.read();
    } else {
      break;
    }
  }

  switch (cmd) {
    case 'w':
    case 'W':
      setMotorSpeeds(TEST_SPEED, TEST_SPEED);
      Serial.println("OK: forward");
      break;

    case 's':
    case 'S':
      setMotorSpeeds(-TEST_SPEED, -TEST_SPEED);
      Serial.println("OK: backward");
      break;

    case 'a':
    case 'A':
      setMotorSpeeds(-TURN_SPEED, TURN_SPEED);
      Serial.println("OK: left");
      break;

    case 'd':
    case 'D':
      setMotorSpeeds(TURN_SPEED, -TURN_SPEED);
      Serial.println("OK: right");
      break;

    case 'z':
    case 'Z':
      stopMotors();
      Serial.println("OK: stop");
      break;

    case 'h':
    case 'H':
    case '?':
      printHelp();
      break;

    default:
      Serial.println("ERR: use w/a/s/d/z");
      break;
  }
}

void setupMotorPin(int pin) {
  pinMode(pin, OUTPUT);
  ledcAttach(pin, PWM_FREQ, PWM_RESOLUTION);
  ledcWrite(pin, 0);
}

void stopMotors() {
  ledcWrite(LEFT_FORWARD_PIN, 0);
  ledcWrite(LEFT_BACKWARD_PIN, 0);
  ledcWrite(RIGHT_FORWARD_PIN, 0);
  ledcWrite(RIGHT_BACKWARD_PIN, 0);
}

void setMotorSpeeds(int leftSpeed, int rightSpeed) {
  leftSpeed = constrain(leftSpeed, -PWM_MAX, PWM_MAX);
  rightSpeed = constrain(rightSpeed, -PWM_MAX, PWM_MAX);

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

void printHelp() {
  Serial.println("Commands:");
  Serial.println("  w -> forward");
  Serial.println("  s -> backward");
  Serial.println("  a -> left");
  Serial.println("  d -> right");
  Serial.println("  z -> stop");
}
