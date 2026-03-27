#include "NBC.h"

// --- Pin Definitions ---
#define PIN_TRIGGER 6
#define PIN_SELECT_1 2
#define PIN_SELECT_2 3

// --- Configuration ---
#define MIN_BATTERY_VOLTAGE (3.0 * 4)
#define MAX_WAIT_TIME 1000
#define SOLENOID_PULSE_TIME 50
#define SOLENOID_RETRACT_TIME 40
#define BURST_COUNT 3

// --- Fire Mode Enum ---
enum FireMode {
  FIRE_MODE_SAFETY,
  FIRE_MODE_SINGLE,
  FIRE_MODE_BURST,
  FIRE_MODE_FULL_AUTO
};

// --- Trigger State (edge detection) ---
int triggerState = LOW;
int lastTriggerState = HIGH;

// --- Pure Logic: Determine fire mode from select switch state ---
FireMode getFireMode( bool select1Low, bool select2Low )
{
  if( select1Low && !select2Low )
    return FIRE_MODE_SINGLE;
  if( !select1Low && select2Low )
    return FIRE_MODE_BURST;
  if( select1Low && select2Low )
    return FIRE_MODE_FULL_AUTO;
  return FIRE_MODE_SAFETY;
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

// --- Setup ---
void setup()
{
  pinMode( PIN_TRIGGER, INPUT_PULLUP );
  pinMode( PIN_SELECT_1, INPUT_PULLUP );
  pinMode( PIN_SELECT_2, INPUT_PULLUP );

  delay( 1000 );

  NBCInit();

  delay( 1000 );

  FlyshotSetNewMotorSpeed( 5000 );
  delay( 500 );
  FlyshotSetMotorDirectionForward( FLYSHOT_ESC_A );
  delay( 500 );
  FlyshotSetMotorDirectionForward( FLYSHOT_ESC_B );
  delay( 500 );
  FlyshotBeep1( FLYSHOT_ESC_BOTH );
  delay( 1000 );

  CalibrateFlywheels();
}

// --- Main Loop ---
void loop()
{
  if( NBCGetVoltage() < MIN_BATTERY_VOLTAGE )
    return;

  // Safety: both select pins HIGH — do nothing
  FireMode mode = getFireMode(
    digitalRead( PIN_SELECT_1 ) == LOW,
    digitalRead( PIN_SELECT_2 ) == LOW
  );

  if( mode == FIRE_MODE_SAFETY )
  {
    NBCProcessFlywheelSpeed();
    FlyshotStopMotors();
    return;
  }

  // Rev flywheels while trigger is held, then fire per selected mode
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
