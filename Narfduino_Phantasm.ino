#include "NBC.h"
#include <U8x8lib.h>
#include <Wire.h>

// --- Pin Definitions ---
#define PIN_TRIGGER 6
#define PIN_SELECT_1 2
#define PIN_SELECT_2 3
#define PIN_ENCODER_CLK 4
#define PIN_ENCODER_DT 11
#define PIN_ENCODER_SW 12

// --- Configuration ---
#define MIN_BATTERY_VOLTAGE (3.0 * 4)
#define MAX_WAIT_TIME 1000
#define SOLENOID_PULSE_TIME 50
#define SOLENOID_RETRACT_TIME 40
#define BURST_COUNT 3
#define MOTOR_RPM_MIN 3000
#define MOTOR_RPM_MAX 8000
#define MOTOR_RPM_DEFAULT 5000
#define ENCODER_RPM_STEP 250

// --- Fire Mode Enum ---
enum FireMode {
  FIRE_MODE_SAFETY,
  FIRE_MODE_SINGLE,
  FIRE_MODE_BURST,
  FIRE_MODE_FULL_AUTO
};

// --- OLED Display (I2C, text-only for low memory) ---
U8X8_SSD1306_128X64_NONAME_HW_I2C display( U8X8_PIN_NONE );

// --- State ---
int triggerState = LOW;
int lastTriggerState = HIGH;
unsigned long motorRPM = MOTOR_RPM_DEFAULT;
unsigned long displayedRPM = 0;
FireMode displayedMode = FIRE_MODE_SAFETY;
int lastEncoderCLK = HIGH;

// --- Pure Logic: Determine fire mode from 4-position rotary switch state ---
// Position 1: HIGH/HIGH = Safety, 2: LOW/HIGH = Single,
// 3: HIGH/LOW = Burst, 4: LOW/LOW = Full Auto
FireMode getFireMode( bool select1Low, bool select2Low )
{
  if( !select1Low && select2Low )
    return FIRE_MODE_SINGLE;
  if( select1Low && !select2Low )
    return FIRE_MODE_BURST;
  if( select1Low && select2Low )
    return FIRE_MODE_FULL_AUTO;
  return FIRE_MODE_SAFETY;
}

// --- Pure Logic: Clamp RPM within valid bounds ---
unsigned long clampRPM( long rpm, unsigned long minRPM, unsigned long maxRPM )
{
  if( rpm < (long)minRPM ) return minRPM;
  if( (unsigned long)rpm > maxRPM ) return maxRPM;
  return (unsigned long)rpm;
}

// --- Pure Logic: Calculate new RPM from encoder rotation ---
unsigned long calculateEncoderRPM( unsigned long currentRPM, int direction, unsigned int stepSize, unsigned long minRPM, unsigned long maxRPM )
{
  long newRPM = (long)currentRPM + ( direction * (int)stepSize );
  return clampRPM( newRPM, minRPM, maxRPM );
}

// --- Pure Logic: Get display label for fire mode ---
const char* getFireModeLabel( FireMode mode )
{
  switch( mode )
  {
    case FIRE_MODE_SINGLE:    return "SINGLE";
    case FIRE_MODE_BURST:     return "BURST";
    case FIRE_MODE_FULL_AUTO: return "FULL AUTO";
    case FIRE_MODE_SAFETY:
    default:                  return "SAFETY";
  }
}

// --- Pure Logic: Check if RPM has changed ---
bool hasRPMChanged( unsigned long oldRPM, unsigned long newRPM )
{
  return oldRPM != newRPM;
}

// --- Solenoid Helpers (DRY: single cycle extracted) ---
void fireSolenoidCycle()
{
  NBCSolenoidOn();
  NBCWait( SOLENOID_PULSE_TIME );
  NBCSolenoidOff();
  NBCWait( SOLENOID_RETRACT_TIME );
}

// --- Fire Modes (Single Responsibility) ---

void singleShot()
{
  triggerState = digitalRead( PIN_TRIGGER );
  if( triggerState != lastTriggerState )
  {
    if( triggerState == LOW )
    {
      fireSolenoidCycle();
    }
    lastTriggerState = triggerState;
  }
}

void burstFire()
{
  triggerState = digitalRead( PIN_TRIGGER );
  if( triggerState != lastTriggerState )
  {
    if( triggerState == LOW )
    {
      for( int i = 0; i < BURST_COUNT; i++ )
      {
        fireSolenoidCycle();
        NBCProcessFlywheelSpeed();
      }
    }
    lastTriggerState = triggerState;
  }
}

void fullAuto()
{
  if( digitalRead( PIN_TRIGGER ) == LOW )
  {
    fireSolenoidCycle();
  }
}

// --- Fire Mode Dispatcher ---
void selectFire()
{
  FireMode mode = getFireMode(
    digitalRead( PIN_SELECT_1 ) == LOW,
    digitalRead( PIN_SELECT_2 ) == LOW
  );

  switch( mode )
  {
    case FIRE_MODE_SINGLE:
      singleShot();
      break;
    case FIRE_MODE_BURST:
      burstFire();
      break;
    case FIRE_MODE_FULL_AUTO:
      fullAuto();
      break;
    case FIRE_MODE_SAFETY:
    default:
      break;
  }
}

// --- Encoder Polling (non-interrupt, avoids PCINT conflict with tachometers) ---
int pollEncoderDirection()
{
  int currentCLK = digitalRead( PIN_ENCODER_CLK );

  if( currentCLK != lastEncoderCLK && currentCLK == LOW )
  {
    lastEncoderCLK = currentCLK;
    if( digitalRead( PIN_ENCODER_DT ) != currentCLK )
      return 1;   // Clockwise
    else
      return -1;  // Anti-clockwise
  }

  lastEncoderCLK = currentCLK;
  return 0;
}

// --- Display Update (only redraws changed values to minimise I2C traffic) ---
void updateDisplay( FireMode mode, unsigned long rpm )
{
  if( mode != displayedMode )
  {
    display.clearLine( 0 );
    display.setCursor( 0, 0 );
    display.print( getFireModeLabel( mode ) );
    displayedMode = mode;
  }

  if( hasRPMChanged( displayedRPM, rpm ) )
  {
    display.clearLine( 2 );
    display.setCursor( 0, 2 );
    display.print( "RPM: " );
    display.print( rpm );
    displayedRPM = rpm;
  }
}

// --- Setup ---
void setup()
{
  pinMode( PIN_TRIGGER, INPUT_PULLUP );
  pinMode( PIN_SELECT_1, INPUT_PULLUP );
  pinMode( PIN_SELECT_2, INPUT_PULLUP );
  pinMode( PIN_ENCODER_CLK, INPUT_PULLUP );
  pinMode( PIN_ENCODER_DT, INPUT_PULLUP );
  pinMode( PIN_ENCODER_SW, INPUT_PULLUP );

  lastEncoderCLK = digitalRead( PIN_ENCODER_CLK );

  display.begin();
  display.setFont( u8x8_font_chroma48medium8_r );
  display.setCursor( 0, 0 );
  display.print( "PHANTASM" );
  display.setCursor( 0, 2 );
  display.print( "Initialising..." );

  delay( 1000 );

  NBCInit();

  delay( 1000 );

  FlyshotSetNewMotorSpeed( motorRPM );
  delay( 500 );
  FlyshotSetMotorDirectionForward( FLYSHOT_ESC_A );
  delay( 500 );
  FlyshotSetMotorDirectionForward( FLYSHOT_ESC_B );
  delay( 500 );
  FlyshotBeep1( FLYSHOT_ESC_BOTH );
  delay( 1000 );

  CalibrateFlywheels();

  display.clear();
  displayedMode = FIRE_MODE_SAFETY;
  displayedRPM = 0;
}

// --- Main Loop ---
void loop()
{
  if( NBCGetVoltage() < MIN_BATTERY_VOLTAGE )
    return;

  // Poll encoder for RPM adjustment (only during idle, not mid-firing)
  int encoderDir = pollEncoderDirection();
  if( encoderDir != 0 )
  {
    unsigned long newRPM = calculateEncoderRPM( motorRPM, encoderDir, ENCODER_RPM_STEP, MOTOR_RPM_MIN, MOTOR_RPM_MAX );
    if( hasRPMChanged( motorRPM, newRPM ) )
    {
      motorRPM = newRPM;
      FlyshotSetNewMotorSpeed( motorRPM );
    }
  }

  FireMode mode = getFireMode(
    digitalRead( PIN_SELECT_1 ) == LOW,
    digitalRead( PIN_SELECT_2 ) == LOW
  );

  updateDisplay( mode, motorRPM );

  if( mode == FIRE_MODE_SAFETY )
  {
    NBCProcessFlywheelSpeed();
    FlyshotStopMotors();
    return;
  }

  while( digitalRead( PIN_TRIGGER ) == LOW )
  {
    if( !FlyshotStartMotorsAndWait( MAX_WAIT_TIME ) )
    {
      FlyshotStopMotorsAndWait( MAX_WAIT_TIME );
      FlyshotBeep2( FLYSHOT_ESC_BOTH );
      NBCWait( 5000 );
      return;
    }

    selectFire();
    NBCProcessFlywheelSpeed();
  }

  NBCProcessFlywheelSpeed();
  FlyshotStopMotors();
}
