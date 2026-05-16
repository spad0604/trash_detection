/*
 * ============================================================
 *  ESP32 #2 — Actuator & Navigation Node
 *  Project : Smart Trash Bin — Edge AI & IoT Cloud
 * ============================================================
 *  Responsibilities:
 *    • Control 3x Servo motors (lid open/close)
 *    • Control Stepper Motor via A4988 (sorting turntable)
 *    • Control 2x DC motors via L298N (autonomous movement)
 *    • Read line-follower sensors (navigation)
 *    • Control 3x status LEDs
 *    • Receive classification commands from Raspberry Pi (UART)
 *
 *  UART Protocol  (ESP32 #2 ↔ Pi):
 *    Pi→ESP:  "CMD:SERVO_OPEN\n"         — open intake lid
 *    Pi→ESP:  "CMD:SERVO_CLOSE\n"        — close intake lid
 *    Pi→ESP:  "CMD:CLASSIFY:0\n"         — sort to bin 0 (Plastic/Can)
 *    Pi→ESP:  "CMD:CLASSIFY:1\n"         — sort to bin 1 (Organic)
 *    Pi→ESP:  "CMD:CLASSIFY:2\n"         — sort to bin 2 (Other)
 *    Pi→ESP:  "CMD:MOVE_START\n"         — begin line-following
 *    Pi→ESP:  "CMD:MOVE_STOP\n"          — stop movement
 *    Pi→ESP:  "CMD:MOVE_HOME\n"          — return to base
 *    Pi→ESP:  "CMD:LED:RED\n"            — turn on red LED
 *    Pi→ESP:  "CMD:LED:GREEN\n"          — turn on green LED
 *    Pi→ESP:  "CMD:LED:YELLOW\n"         — turn on yellow LED
 *    Pi→ESP:  "CMD:LED:OFF\n"            — turn off all LEDs
 *    Pi→ESP:  "CMD:STATUS\n"             — request current status
 *    ESP→Pi:  "STATUS:SERVO_OPENED\n"
 *    ESP→Pi:  "STATUS:SERVO_CLOSED\n"
 *    ESP→Pi:  "STATUS:SORTING:0\n"       — sorting started for bin 0
 *    ESP→Pi:  "STATUS:SORT_DONE\n"       — sorting complete
 *    ESP→Pi:  "STATUS:MOVING\n"          — robot is moving
 *    ESP→Pi:  "STATUS:ARRIVED\n"         — reached destination
 *    ESP→Pi:  "STATUS:LINE_LOST\n"       — line not detected
 *    ESP→Pi:  "STATUS:IDLE\n"            — system idle
 * ============================================================
 */

#include <ESP32Servo.h>
#include <AccelStepper.h>

// ════════════════════════════════════════════════════════════
//  Pin Definitions  —  ESP32 #2 (Actuator Node)
// ════════════════════════════════════════════════════════════

// A4988 Stepper Driver  (sorting turntable)
#define STEP_PIN   22
#define DIR_PIN    19
#define EN_PIN     23

// L298N Motor Driver  (2x DC motors for movement)
#define MOTOR_IN1  12   // Left motor forward
#define MOTOR_IN2  14   // Left motor backward
#define MOTOR_IN3  26   // Right motor forward
#define MOTOR_IN4  27   // Right motor backward
#define MOTOR_ENA  13   // Left motor PWM speed
#define MOTOR_ENB  15   // Right motor PWM speed

// Servo motors
#define SERVO1_PIN  5   // Main intake lid
#define SERVO2_PIN 18   // Secondary mechanism
#define SERVO3_PIN 21   // Auxiliary mechanism

// Line-follower sensors
#define LINE_LEFT   4
#define LINE_RIGHT  2
#define LINE_CENTER 34  // Analog-capable pin

// Status LEDs
#define LED_RED    25
#define LED_GREEN  32
#define LED_YELLOW 33

// UART to Raspberry Pi  (Serial2)
#define UART_RX2   16
#define UART_TX2   17

// ════════════════════════════════════════════════════════════
//  Stepper configuration
// ════════════════════════════════════════════════════════════
#define STEPS_PER_REV      200     // 1.8° stepper = 200 steps/rev
#define MICROSTEP          16      // A4988 microstepping
#define STEPS_FULL_REV     (STEPS_PER_REV * MICROSTEP)  // 3200

// Each bin sits at 120° apart on the turntable
#define STEPS_PER_BIN      (STEPS_FULL_REV / 3)         // ~1067 steps

// Motor speed defaults
#define MOTOR_SPEED        180     // PWM 0-255
#define MOTOR_TURN_SPEED   140

// Servo angles
#define LID_OPEN_ANGLE     120
#define LID_CLOSED_ANGLE    10

// ════════════════════════════════════════════════════════════
//  Objects
// ════════════════════════════════════════════════════════════
AccelStepper stepper(AccelStepper::DRIVER, STEP_PIN, DIR_PIN);
Servo servoLid;
Servo servo2;
Servo servo3;

// ════════════════════════════════════════════════════════════
//  State machine
// ════════════════════════════════════════════════════════════
enum SystemState {
  STATE_IDLE,
  STATE_LID_OPENING,
  STATE_LID_OPEN,
  STATE_LID_CLOSING,
  STATE_SORTING,
  STATE_MOVING,
  STATE_RETURNING_HOME
};

SystemState currentState = STATE_IDLE;
int  currentBinPosition  = 0;   // 0, 1, 2 — which bin the turntable faces
bool isMoving            = false;
unsigned long lidOpenTime = 0;
#define LID_AUTO_CLOSE_MS 8000   // auto close after 8 seconds

// ════════════════════════════════════════════════════════════
//  PWM channels for L298N (using LEDC)
// ════════════════════════════════════════════════════════════
#define PWM_FREQ     5000
#define PWM_RES      8
#define PWM_CH_ENA   0
#define PWM_CH_ENB   1

// ════════════════════════════════════════════════════════════
//  SETUP
// ════════════════════════════════════════════════════════════
void setup() {
  Serial.begin(115200);
  Serial2.begin(9600, SERIAL_8N1, UART_RX2, UART_TX2);

  // ── Stepper ───────────────────────────────────────────────
  pinMode(EN_PIN, OUTPUT);
  digitalWrite(EN_PIN, HIGH);  // disable stepper initially (active LOW)
  stepper.setMaxSpeed(1000);
  stepper.setAcceleration(500);
  stepper.setCurrentPosition(0);

  // ── L298N DC Motors ───────────────────────────────────────
  pinMode(MOTOR_IN1, OUTPUT);
  pinMode(MOTOR_IN2, OUTPUT);
  pinMode(MOTOR_IN3, OUTPUT);
  pinMode(MOTOR_IN4, OUTPUT);

  ledcAttach(MOTOR_ENA, PWM_FREQ, PWM_RES);
  ledcAttach(MOTOR_ENB, PWM_FREQ, PWM_RES);
  stopMotors();

  // ── Servos ────────────────────────────────────────────────
  servoLid.attach(SERVO1_PIN);
  servo2.attach(SERVO2_PIN);
  servo3.attach(SERVO3_PIN);
  servoLid.write(LID_CLOSED_ANGLE);

  // ── Line sensors ──────────────────────────────────────────
  pinMode(LINE_LEFT,  INPUT);
  pinMode(LINE_RIGHT, INPUT);
  // LINE_CENTER is analog — no pinMode needed

  // ── LEDs ──────────────────────────────────────────────────
  pinMode(LED_RED,    OUTPUT);
  pinMode(LED_GREEN,  OUTPUT);
  pinMode(LED_YELLOW, OUTPUT);
  allLedsOff();

  Serial.println("[ESP32 #2] Actuator Node — Ready");
  Serial2.println("STATUS:IDLE");
}

// ════════════════════════════════════════════════════════════
//  LOOP
// ════════════════════════════════════════════════════════════
void loop() {
  // ── Handle commands from Pi ───────────────────────────────
  handlePiCommands();

  // ── Run stepper (non-blocking) ────────────────────────────
  stepper.run();

  // Check if stepper finished its move
  if (currentState == STATE_SORTING && stepper.distanceToGo() == 0) {
    onSortingComplete();
  }

  // ── Auto-close lid ────────────────────────────────────────
  if (currentState == STATE_LID_OPEN &&
      millis() - lidOpenTime >= LID_AUTO_CLOSE_MS) {
    closeLid();
  }

  // ── Line-following logic ──────────────────────────────────
  if (currentState == STATE_MOVING || currentState == STATE_RETURNING_HOME) {
    followLine();
  }
}

// ════════════════════════════════════════════════════════════
//  Handle incoming UART commands from Pi
// ════════════════════════════════════════════════════════════
void handlePiCommands() {
  if (!Serial2.available()) return;

  String cmd = Serial2.readStringUntil('\n');
  cmd.trim();
  Serial.printf("[CMD] Received: %s\n", cmd.c_str());

  // ── Servo commands ────────────────────────────────────────
  if (cmd == "CMD:SERVO_OPEN") {
    openLid();
  }
  else if (cmd == "CMD:SERVO_CLOSE") {
    closeLid();
  }
  // ── Classification / sorting ──────────────────────────────
  else if (cmd.startsWith("CMD:CLASSIFY:")) {
    int binTarget = cmd.substring(13).toInt();  // 0, 1, 2
    startSorting(binTarget);
  }
  // ── Movement commands ─────────────────────────────────────
  else if (cmd == "CMD:MOVE_START") {
    currentState = STATE_MOVING;
    isMoving = true;
    Serial2.println("STATUS:MOVING");
    Serial.println("[Motor] Line-following started");
  }
  else if (cmd == "CMD:MOVE_STOP") {
    stopMotors();
    currentState = STATE_IDLE;
    isMoving = false;
    Serial2.println("STATUS:IDLE");
    Serial.println("[Motor] Stopped");
  }
  else if (cmd == "CMD:MOVE_HOME") {
    currentState = STATE_RETURNING_HOME;
    isMoving = true;
    Serial2.println("STATUS:MOVING");
    Serial.println("[Motor] Returning home via line");
  }
  // ── LED commands ──────────────────────────────────────────
  else if (cmd == "CMD:LED:RED") {
    allLedsOff();
    digitalWrite(LED_RED, HIGH);
  }
  else if (cmd == "CMD:LED:GREEN") {
    allLedsOff();
    digitalWrite(LED_GREEN, HIGH);
  }
  else if (cmd == "CMD:LED:YELLOW") {
    allLedsOff();
    digitalWrite(LED_YELLOW, HIGH);
  }
  else if (cmd == "CMD:LED:OFF") {
    allLedsOff();
  }
  // ── Status request ────────────────────────────────────────
  else if (cmd == "CMD:STATUS") {
    sendStatus();
  }
  else {
    Serial.printf("[CMD] Unknown: %s\n", cmd.c_str());
  }
}

// ════════════════════════════════════════════════════════════
//  Lid control
// ════════════════════════════════════════════════════════════
void openLid() {
  servoLid.write(LID_OPEN_ANGLE);
  currentState = STATE_LID_OPEN;
  lidOpenTime  = millis();
  Serial2.println("STATUS:SERVO_OPENED");
  Serial.println("[Servo] Lid OPENED");
}

void closeLid() {
  servoLid.write(LID_CLOSED_ANGLE);
  currentState = STATE_IDLE;
  Serial2.println("STATUS:SERVO_CLOSED");
  Serial.println("[Servo] Lid CLOSED");
}

// ════════════════════════════════════════════════════════════
//  Sorting — rotate turntable to target bin
// ════════════════════════════════════════════════════════════
void startSorting(int targetBin) {
  targetBin = constrain(targetBin, 0, 2);
  Serial.printf("[Sort] Target bin: %d (current: %d)\n", targetBin, currentBinPosition);

  // Enable stepper
  digitalWrite(EN_PIN, LOW);

  // Calculate steps to reach target
  int deltaSteps = (targetBin - currentBinPosition) * STEPS_PER_BIN;
  stepper.move(deltaSteps);

  currentState = STATE_SORTING;
  currentBinPosition = targetBin;

  // Turn on yellow LED while sorting
  allLedsOff();
  digitalWrite(LED_YELLOW, HIGH);

  char buf[32];
  snprintf(buf, sizeof(buf), "STATUS:SORTING:%d", targetBin);
  Serial2.println(buf);
}

void onSortingComplete() {
  Serial.println("[Sort] Sorting complete — returning turntable home");

  // Wait a moment for trash to fall
  delay(500);

  // Return turntable to home (position 0)
  stepper.moveTo(0);

  // Wait for return (blocking, but turntable return is fast)
  while (stepper.distanceToGo() != 0) {
    stepper.run();
  }

  currentBinPosition = 0;
  digitalWrite(EN_PIN, HIGH);  // disable stepper to save power

  allLedsOff();
  digitalWrite(LED_GREEN, HIGH);

  currentState = STATE_IDLE;
  Serial2.println("STATUS:SORT_DONE");
  Serial.println("[Sort] Done — idle");
}

// ════════════════════════════════════════════════════════════
//  DC Motor helpers (L298N)
// ════════════════════════════════════════════════════════════
void moveForward(int speed) {
  digitalWrite(MOTOR_IN1, HIGH);
  digitalWrite(MOTOR_IN2, LOW);
  digitalWrite(MOTOR_IN3, HIGH);
  digitalWrite(MOTOR_IN4, LOW);
  ledcWrite(MOTOR_ENA, speed);
  ledcWrite(MOTOR_ENB, speed);
}

void turnLeft(int speed) {
  digitalWrite(MOTOR_IN1, LOW);
  digitalWrite(MOTOR_IN2, HIGH);
  digitalWrite(MOTOR_IN3, HIGH);
  digitalWrite(MOTOR_IN4, LOW);
  ledcWrite(MOTOR_ENA, speed);
  ledcWrite(MOTOR_ENB, speed);
}

void turnRight(int speed) {
  digitalWrite(MOTOR_IN1, HIGH);
  digitalWrite(MOTOR_IN2, LOW);
  digitalWrite(MOTOR_IN3, LOW);
  digitalWrite(MOTOR_IN4, HIGH);
  ledcWrite(MOTOR_ENA, speed);
  ledcWrite(MOTOR_ENB, speed);
}

void stopMotors() {
  digitalWrite(MOTOR_IN1, LOW);
  digitalWrite(MOTOR_IN2, LOW);
  digitalWrite(MOTOR_IN3, LOW);
  digitalWrite(MOTOR_IN4, LOW);
  ledcWrite(MOTOR_ENA, 0);
  ledcWrite(MOTOR_ENB, 0);
}

// ════════════════════════════════════════════════════════════
//  Line-following logic
// ════════════════════════════════════════════════════════════
void followLine() {
  bool left  = digitalRead(LINE_LEFT)  == HIGH;   // HIGH = line detected
  bool right = digitalRead(LINE_RIGHT) == HIGH;
  int  center = analogRead(LINE_CENTER);
  bool centerDetected = (center > 2000);  // threshold for analog sensor

  if (centerDetected && !left && !right) {
    // On track — go straight
    moveForward(MOTOR_SPEED);
  }
  else if (left && !right) {
    // Drifting right — correct left
    turnLeft(MOTOR_TURN_SPEED);
  }
  else if (!left && right) {
    // Drifting left — correct right
    turnRight(MOTOR_TURN_SPEED);
  }
  else if (!left && !right && !centerDetected) {
    // Lost line — stop and report
    stopMotors();
    Serial2.println("STATUS:LINE_LOST");
    Serial.println("[Line] Line LOST — stopped");

    // Stay in current state so Pi can decide
  }
  else if (left && right && centerDetected) {
    // Intersection / endpoint — stop
    stopMotors();
    if (currentState == STATE_RETURNING_HOME) {
      currentState = STATE_IDLE;
      isMoving = false;
      Serial2.println("STATUS:ARRIVED");
      Serial.println("[Line] Arrived at home");
    } else {
      currentState = STATE_IDLE;
      isMoving = false;
      Serial2.println("STATUS:ARRIVED");
      Serial.println("[Line] Arrived at destination");
    }
  }
}

// ════════════════════════════════════════════════════════════
//  LED helpers
// ════════════════════════════════════════════════════════════
void allLedsOff() {
  digitalWrite(LED_RED,    LOW);
  digitalWrite(LED_GREEN,  LOW);
  digitalWrite(LED_YELLOW, LOW);
}

// ════════════════════════════════════════════════════════════
//  Send current status to Pi
// ════════════════════════════════════════════════════════════
void sendStatus() {
  switch (currentState) {
    case STATE_IDLE:            Serial2.println("STATUS:IDLE");          break;
    case STATE_LID_OPENING:
    case STATE_LID_OPEN:        Serial2.println("STATUS:SERVO_OPENED"); break;
    case STATE_LID_CLOSING:     Serial2.println("STATUS:SERVO_CLOSED"); break;
    case STATE_SORTING:         Serial2.println("STATUS:SORTING");      break;
    case STATE_MOVING:
    case STATE_RETURNING_HOME:  Serial2.println("STATUS:MOVING");       break;
  }
}
