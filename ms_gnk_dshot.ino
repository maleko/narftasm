// Firmware for MS-GnK using Bidirectional DShot protocol to spin the motors
// RPM is calculated by reading eRPM of each ESC, divided by motor poles/2
// Uses an RPM threshold to decide when to fire the solenoid, this is calculated by Voltage * Motor KV, and a reduction in RPM set by the user below

//User defined settings below

// Motor/ESC Settings
#define MOTORS 4 // Number of motors in blaster
#define MOTOR_POLES 14 // Motor poles, can be found in motor datasheet (12N14P = 14 poles)
#define MOTOR_KV 2950  // Known motor KV, used to calculate rpm threshold
const int voltage = 22; // Nominal lipo voltage used, must be a whole number (3s = 11V, 4s = 15V, 6s = 22V)
const int rpmReduction = 15000; // How much RPM is deducted from the theoretical max rpm calculated by Voltage * Motor KV, this sets the rpm threshold that the solenoid will fire at

// Motor speed control
const int motorMin = 600;   // Motor Minimum speed
const int motorMax = 2000;  // Motor Maximum speed
const int preRev = 75;     // Motor Pre-rev speed

// Solenoid controls
int solenoidOn = 35;       // Solenoid  On Delay, default 40ms
int solenoidOff = 23;      // Solenoid  Off Delay, default 50ms

// Everything else

// Libraries
#include <PIO_DShot.h>

// Switches
#define REV D1
#define TRIGGER D2
#define POT A3
#define SELECT_1 D4
#define SELECT_2 D5

//Trigger and burst states
int triggerState = LOW;
int lastTriggerState = HIGH;
int triggerReading;
int fireDelay;
int triggerDelay;
unsigned long debounceTime = 0;  // Last time the output pin was toggled
unsigned long debounce = 200UL;  // Debounce time
unsigned long previousMillis = 0;

// Solenoid
#define MOSFET D0

// ESC controls
BidirDShotX1 *ESC1;
BidirDShotX1 *ESC2;
BidirDShotX1 *ESC3;
BidirDShotX1 *ESC4;

// ESC values
int escThrottle;
int escSpeed;
int escLow = 0;
int escRevdown;
uint32_t ERPM1 = 0;
uint32_t ERPM2 = 0;
uint32_t ERPM3 = 0;
uint32_t ERPM4 = 0;
uint32_t rpm1 = 0;
uint32_t rpm2 = 0;
uint32_t rpm3 = 0;
uint32_t rpm4 = 0;

void setup() {
  pinMode(MOSFET, OUTPUT);
  pinMode(REV, INPUT_PULLUP);
  pinMode(TRIGGER, INPUT_PULLUP);
  pinMode(SELECT_1, INPUT_PULLUP);
  pinMode(SELECT_2, INPUT_PULLUP);
  delay(2000);
  ESC1 = new BidirDShotX1(D7, 300);
  ESC2 = new BidirDShotX1(D8, 300);
  ESC3 = new BidirDShotX1(D9, 300);
  ESC4 = new BidirDShotX1(D10, 300);
  Serial.begin(115200);
}
// Semi auto
void semiAuto() {
  triggerState = digitalRead(TRIGGER);
  if (triggerState != lastTriggerState) {
    if ((triggerState == LOW)) {
      digitalWrite(MOSFET, HIGH);
      delay(solenoidOn);
      digitalWrite(MOSFET, LOW);
    } else {
      digitalWrite(MOSFET, LOW);
    }
    delay(20);
    lastTriggerState = triggerState;
  }
}
// Rev flywheels
void revUp() {
  while (digitalRead(REV) == LOW) {  // Rev trigger pressed
    delayMicroseconds(200);
    revSpeed();  // Find throttle value for motors
    ESC1->sendThrottle(escSpeed);
    ESC2->sendThrottle(escSpeed);
    ESC3->sendThrottle(escSpeed);
    ESC4->sendThrottle(escSpeed);
    escOutput();                     // RPM output from motors, 
    if (digitalRead(REV) == HIGH) {  // Rev trigger released
      revDown();
    }
  }
}
// Power down flywheels
void revDown() {
  digitalWrite(MOSFET, LOW);
  for (escRevdown = escSpeed; escRevdown >= escLow; escRevdown -= 25) {  // Gradually rev down motors
    ESC1->sendThrottle(escRevdown);
    ESC2->sendThrottle(escRevdown);
    ESC3->sendThrottle(escRevdown);
    ESC4->sendThrottle(escRevdown);
    if (digitalRead(REV) == LOW) {  // Rev trigger pressed
      revUp();
    }
    delay(20);
  }
}
// Rev speed control
void revSpeed() {
  escThrottle = analogRead(POT);
  escSpeed = map(escThrottle, 0, 1023, motorMin, motorMax);
}
// ESC RPM Output and calculations
void escOutput() {
  uint32_t avgRpm = 0;
  uint32_t rpmThreshold = 0;
  uint32_t rpmFire = 0;
  ESC1->getTelemetryErpm(&rpm1);
  ESC2->getTelemetryErpm(&rpm2);
  ESC3->getTelemetryErpm(&rpm3);
  ESC4->getTelemetryErpm(&rpm4);
  rpm1 /= MOTOR_POLES / 2; // eRPM divided by motor poles/2
  rpm2 /= MOTOR_POLES / 2;
  rpm3 /= MOTOR_POLES / 2;
  rpm4 /= MOTOR_POLES / 2;
  avgRpm = (rpm1 + rpm2 + rpm3 + rpm4) / MOTORS; // Average RPM of all motors used in blaster
  rpmThreshold = (MOTOR_KV * voltage) - rpmReduction; // Max RPM threshold, with user defined reduction
  rpmFire = map(escSpeed, 0, motorMax, 0, rpmThreshold); // Maps RPM threshold to the potentiometer
  // Serial.print(avgRpm);
  // Serial.print("\t");
  // Serial.println(rpmFire);
  if (avgRpm >= rpmFire) {
    selectFire();
  }
}
// Check select fire switch
void selectFire() {
  if (digitalRead(SELECT_1) == LOW && digitalRead(SELECT_2) == HIGH) {  // Semi Auto
    semiAuto();
  } else if (digitalRead(SELECT_1) == HIGH && digitalRead(SELECT_2) == LOW) {  // Full Auto
    binaryFire();
  } else if (digitalRead(SELECT_1) == LOW && digitalRead(SELECT_2) == LOW) {  // Full Auto
    fullAuto();
  }
}
// Binary fire
void binaryFire() {
  triggerState = digitalRead(TRIGGER);
  if (triggerState != lastTriggerState) {
    if ((triggerState == LOW)) {
      digitalWrite(MOSFET, HIGH);
      delay(solenoidOn);
      digitalWrite(MOSFET, LOW);
      delay(solenoidOff);
    } else {
      digitalWrite(MOSFET, HIGH);
      delay(solenoidOn);
      digitalWrite(MOSFET, LOW);
      delay(solenoidOff);
    }
    delay(20);
    lastTriggerState = triggerState;
  }
}
// Full auto
void fullAuto() {
  if (digitalRead(TRIGGER) == LOW) {
    digitalWrite(MOSFET, HIGH);
    delay(solenoidOn);
    digitalWrite(MOSFET, LOW);
    delay(solenoidOff);
    if (digitalRead(TRIGGER) == HIGH) {
      digitalWrite(MOSFET, LOW);
    }
  }
}
// Pre-rev mode, toggle trigger switch
void idleMode() {
  triggerReading = digitalRead(TRIGGER);
  if (triggerReading == LOW && lastTriggerState == HIGH && millis() - debounceTime > debounce) {  // Trigger pressed after debounce time
    if (escLow == preRev) {
      escLow = 0;
    } else {
      escLow = preRev;
    }
    debounceTime = millis();
  }
  lastTriggerState = triggerReading;
}
void loop() {
  delayMicroseconds(200);
  if (digitalRead(SELECT_1) == HIGH && digitalRead(SELECT_2) == HIGH) {  // Safety On
    idleMode();
  } else {  // Safety Off
    triggerDelay = fireDelay;
    revUp();
  }
  ESC1->sendThrottle(escLow);
  ESC2->sendThrottle(escLow);
  ESC3->sendThrottle(escLow);
  ESC4->sendThrottle(escLow);
  digitalWrite(MOSFET, LOW);
}