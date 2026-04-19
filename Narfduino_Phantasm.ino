#include "NBC.h"
#include <U8x8lib.h>
#include <Wire.h>

// --- Pin Definitions ---
#define PIN_TRIGGER 6
#define PIN_PRE_REV A1
#define PIN_SELECT_1 2
#define PIN_SELECT_2 3
#define PIN_ENCODER_CLK 13
#define PIN_ENCODER_DT 11
#define PIN_ENCODER_SW 12
#define PIN_MP5_SLAP A2

// --- Configuration ---
#define MIN_BATTERY_VOLTAGE (3.0 * 4)
#define MAX_WAIT_TIME 1000
#define SOLENOID_PULSE_TIME 50
#define SOLENOID_RETRACT_TIME 40
#define BURST_COUNT_DEFAULT 3
#define BURST_COUNT_MIN 2
#define BURST_COUNT_MAX 10
#define ENCODER_BURST_STEP 1
#define MOTOR_RPM_MIN 3000
#define MOTOR_RPM_MAX 8000
#define MOTOR_RPM_DEFAULT 5000
#define ENCODER_RPM_STEP 250
#define PRE_REV_RPM_DEFAULT 3000
#define PRE_REV_RPM_MIN 2000
#define PRE_REV_RPM_MAX 5000
#define ENCODER_PRE_REV_STEP 250

// --- Fire Mode Enum ---
enum FireMode {
  FIRE_MODE_SINGLE,
  FIRE_MODE_BURST,
  FIRE_MODE_FULL_AUTO
};

// --- Encoder Mode Enum ---
enum EncoderMode {
  ENCODER_MODE_RPM,
  ENCODER_MODE_BURST,
  ENCODER_MODE_PREREV
};

// --- OLED Display (I2C, text-only for low memory) ---
U8X8_SSD1306_128X64_NONAME_HW_I2C display( U8X8_PIN_NONE );

// --- State ---
int triggerState = LOW;
int lastTriggerState = HIGH;
unsigned long motorRPM = MOTOR_RPM_DEFAULT;
int burstCount = BURST_COUNT_DEFAULT;
unsigned long preRevRPM = PRE_REV_RPM_DEFAULT;
bool idling = false;
EncoderMode encoderMode = ENCODER_MODE_RPM;
unsigned long displayedRPM = 0;
int displayedBurstCount = 0;
FireMode displayedMode = FIRE_MODE_SINGLE;
EncoderMode displayedEncoderMode = ENCODER_MODE_RPM;
bool displayedSafe = false;
bool displayedPreRev = false;
unsigned long displayedPreRevRPM = 0;
float displayedVoltage = -1.0;
uint8_t lastEncoderState = 0b11;
int pendingEncoderDir = 0;
int lastButtonState = HIGH;
unsigned long lastButtonDebounceTime = 0;
#define BUTTON_DEBOUNCE_MS 200

// --- Pure Logic: Determine fire mode from 3-position slide switch state ---
// Position 1: LOW/HIGH = Single, 2: HIGH/LOW = Burst, 3: LOW/LOW = Full Auto
// HIGH/HIGH cannot occur with a properly wired slide switch; defaults to SINGLE.
FireMode getFireMode( bool select1Low, bool select2Low )
{
  if( !select1Low && select2Low )
    return FIRE_MODE_BURST;
  if( select1Low && select2Low )
    return FIRE_MODE_FULL_AUTO;
  return FIRE_MODE_SINGLE;
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
    case FIRE_MODE_BURST:     return "BURST";
    case FIRE_MODE_FULL_AUTO: return "FULL AUTO";
    case FIRE_MODE_SINGLE:
    default:                  return "SINGLE";
  }
}

// --- Pure Logic: Get display label, overridden to "SAFE" when bolt is open ---
const char* getDisplayModeLabel( FireMode mode, bool safe )
{
  if( safe )
    return "SAFE";
  return getFireModeLabel( mode );
}

// --- Pure Logic: Check if RPM has changed ---
bool hasRPMChanged( unsigned long oldRPM, unsigned long newRPM )
{
  return oldRPM != newRPM;
}

// --- Pure Logic: Toggle encoder mode (RPM -> Burst -> Pre-Rev -> RPM) ---
EncoderMode toggleEncoderMode( EncoderMode currentMode )
{
  if( currentMode == ENCODER_MODE_RPM )
    return ENCODER_MODE_BURST;
  if( currentMode == ENCODER_MODE_BURST )
    return ENCODER_MODE_PREREV;
  return ENCODER_MODE_RPM;
}

// --- Pure Logic: Clamp burst count within valid bounds ---
int clampBurstCount( int count, int minCount, int maxCount )
{
  if( count < minCount ) return minCount;
  if( count > maxCount ) return maxCount;
  return count;
}

// --- Pure Logic: Calculate new burst count from encoder rotation ---
int calculateEncoderBurst( int currentCount, int direction, int stepSize, int minCount, int maxCount )
{
  int newCount = currentCount + ( direction * stepSize );
  return clampBurstCount( newCount, minCount, maxCount );
}

// --- Pure Logic: Calculate new pre-rev RPM from encoder rotation ---
unsigned long calculateEncoderPreRevRPM( unsigned long currentRPM, int direction, unsigned int stepSize, unsigned long minRPM, unsigned long maxRPM )
{
  long newRPM = (long)currentRPM + ( direction * (int)stepSize );
  return clampRPM( newRPM, minRPM, maxRPM );
}

// --- Pure Logic: Get display label for encoder mode ---
const char* getEncoderModeLabel( EncoderMode mode )
{
  switch( mode )
  {
    case ENCODER_MODE_BURST:  return "BURST";
    case ENCODER_MODE_PREREV: return "PREREV";
    case ENCODER_MODE_RPM:
    default:                  return "RPM";
  }
}

// --- Pure Logic: Determine pre-rev state from NC switch reading ---
// NC switch wiring: closed at rest pulls pin LOW, opened pulls pin HIGH
// via INPUT_PULLUP. Pre-rev is active when the switch is open (pin HIGH).
bool isPreRevActive( bool pinHigh )
{
  return pinHigh;
}

// --- Pure Logic: Determine MP5 slap safety state from NC switch reading ---
// NC switch wiring: closed at rest (bolt locked) pulls pin LOW via GND.
// When bolt is open the switch opens and the pullup pulls pin HIGH = unsafe.
bool isMp5SlapSafe( bool pinHigh )
{
  return !pinHigh;
}

// --- Pure Logic: Format voltage for display ---
void formatVoltageDisplay( float voltage, char* buf, size_t bufSize )
{
  int tenths = (int)( voltage * 10 + 0.5 );
  int whole = tenths / 10;
  int frac = tenths % 10;
  if( whole < 10 )
    snprintf( buf, bufSize, "Bat: %d.%dV", whole, frac );
  else
    snprintf( buf, bufSize, "Bat:%d.%dV", whole, frac );
}

// --- Pure Logic: Check if voltage display needs updating ---
bool hasVoltageChanged( float oldVoltage, float newVoltage )
{
  int oldTenths = (int)( oldVoltage * 10 + 0.5 );
  int newTenths = (int)( newVoltage * 10 + 0.5 );
  return oldTenths != newTenths;
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
      for( int i = 0; i < burstCount; i++ )
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
    case FIRE_MODE_BURST:
      burstFire();
      break;
    case FIRE_MODE_FULL_AUTO:
      fullAuto();
      break;
    case FIRE_MODE_SINGLE:
    default:
      singleShot();
      break;
  }
}

// --- Encoder Polling (non-interrupt, avoids PCINT conflict with tachometers) ---
// KY-040 detent is CLK=HIGH, DT=HIGH (state 0b11). One click traverses:
//   CW:  11 -> 01 -> 00 -> 10 -> 11  (CLK falls first)
//   CCW: 11 -> 10 -> 00 -> 01 -> 11  (DT falls first)
// Direction is captured from the first state seen after leaving detent so the
// reading remains correct even when the main loop is too slow to catch every
// quadrature transition. The click is committed on return to detent.
int pollEncoderDirection()
{
  uint8_t currentState = ( digitalRead( PIN_ENCODER_CLK ) << 1 ) | digitalRead( PIN_ENCODER_DT );

  if( currentState == lastEncoderState )
    return 0;

  if( lastEncoderState == 0b11 )
  {
    if( currentState == 0b01 )
      pendingEncoderDir = 1;   // Clockwise
    else if( currentState == 0b10 )
      pendingEncoderDir = -1;  // Anti-clockwise
  }

  int reportedDir = 0;
  if( currentState == 0b11 && pendingEncoderDir != 0 )
  {
    reportedDir = pendingEncoderDir;
    pendingEncoderDir = 0;
  }

  lastEncoderState = currentState;
  return reportedDir;
}

// --- Encoder Button Polling (with debounce) ---
bool pollEncoderButton()
{
  int reading = digitalRead( PIN_ENCODER_SW );

  if( reading == LOW && lastButtonState == HIGH )
  {
    if( millis() - lastButtonDebounceTime > BUTTON_DEBOUNCE_MS )
    {
      lastButtonDebounceTime = millis();
      lastButtonState = reading;
      return true;
    }
  }

  lastButtonState = reading;
  return false;
}

// --- Display Update (only redraws changed values to minimise I2C traffic) ---
void updateDisplay( FireMode mode, unsigned long rpm, int burst, EncoderMode encMode, bool preRev, unsigned long preRevRPMVal, bool safe, float voltage )
{
  if( mode != displayedMode || safe != displayedSafe )
  {
    display.clearLine( 0 );
    display.setCursor( 0, 0 );
    display.print( getDisplayModeLabel( mode, safe ) );
    displayedMode = mode;
    displayedSafe = safe;
  }

  bool rpmChanged = hasRPMChanged( displayedRPM, rpm );
  bool encModeChanged = ( encMode != displayedEncoderMode );

  if( rpmChanged || encModeChanged )
  {
    display.clearLine( 2 );
    display.setCursor( 0, 2 );
    display.print( encMode == ENCODER_MODE_RPM ? ">" : " " );
    display.print( "RPM: " );
    display.print( rpm );
    displayedRPM = rpm;
  }

  if( burst != displayedBurstCount || encModeChanged )
  {
    display.clearLine( 4 );
    display.setCursor( 0, 4 );
    display.print( encMode == ENCODER_MODE_BURST ? ">" : " " );
    display.print( "Burst: " );
    display.print( burst );
    displayedBurstCount = burst;
  }

  if( encModeChanged )
  {
    displayedEncoderMode = encMode;
  }

  bool preRevRPMChanged = hasRPMChanged( displayedPreRevRPM, preRevRPMVal );

  if( preRev != displayedPreRev || preRevRPMChanged || encModeChanged )
  {
    display.clearLine( 6 );
    display.setCursor( 0, 6 );
    display.print( encMode == ENCODER_MODE_PREREV ? ">" : " " );
    display.print( "PRev:" );
    if( preRev )
    {
      display.print( preRevRPMVal );
    }
    else
    {
      display.print( "OFF" );
    }
    displayedPreRev = preRev;
    displayedPreRevRPM = preRevRPMVal;
  }

  if( hasVoltageChanged( displayedVoltage, voltage ) )
  {
    char voltageBuf[17];
    formatVoltageDisplay( voltage, voltageBuf, sizeof( voltageBuf ) );
    display.clearLine( 7 );
    display.setCursor( 0, 7 );
    display.print( voltageBuf );
    displayedVoltage = voltage;
  }
}

// --- Setup ---
void setup()
{
  pinMode( PIN_TRIGGER, INPUT_PULLUP );
  pinMode( PIN_PRE_REV, INPUT_PULLUP );
  pinMode( PIN_MP5_SLAP, INPUT_PULLUP );
  pinMode( PIN_SELECT_1, INPUT_PULLUP );
  pinMode( PIN_SELECT_2, INPUT_PULLUP );
  pinMode( PIN_ENCODER_CLK, INPUT_PULLUP );
  pinMode( PIN_ENCODER_DT, INPUT_PULLUP );
  pinMode( PIN_ENCODER_SW, INPUT_PULLUP );

  lastEncoderState = ( digitalRead( PIN_ENCODER_CLK ) << 1 ) | digitalRead( PIN_ENCODER_DT );
  pendingEncoderDir = 0;
  lastButtonState = digitalRead( PIN_ENCODER_SW );

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
  displayedMode = FIRE_MODE_SINGLE;
  displayedSafe = false;
  displayedRPM = 0;
  displayedBurstCount = 0;
  displayedEncoderMode = ENCODER_MODE_RPM;
  displayedPreRev = false;
  displayedPreRevRPM = 0;
  displayedVoltage = -1.0;

  // Ensure the debounce period has already "elapsed" so the very first button
  // press in loop() registers immediately, regardless of how quickly setup() ran.
  lastButtonDebounceTime = millis() - ( BUTTON_DEBOUNCE_MS + 1 );
}

// --- Main Loop ---
void loop()
{
  float currentVoltage = NBCGetVoltage();
  if( currentVoltage < MIN_BATTERY_VOLTAGE )
    return;

  // Poll encoder button for mode toggle
  if( pollEncoderButton() )
  {
    encoderMode = toggleEncoderMode( encoderMode );
  }

  // Poll encoder rotation for parameter adjustment
  int encoderDir = pollEncoderDirection();
  if( encoderDir != 0 )
  {
    if( encoderMode == ENCODER_MODE_RPM )
    {
      unsigned long newRPM = calculateEncoderRPM( motorRPM, encoderDir, ENCODER_RPM_STEP, MOTOR_RPM_MIN, MOTOR_RPM_MAX );
      if( hasRPMChanged( motorRPM, newRPM ) )
      {
        motorRPM = newRPM;
        // Only update motor speed if not idling at pre-rev RPM
        if( !idling )
        {
          FlyshotSetNewMotorSpeed( motorRPM );
        }
      }
    }
    else if( encoderMode == ENCODER_MODE_BURST )
    {
      burstCount = calculateEncoderBurst( burstCount, encoderDir, ENCODER_BURST_STEP, BURST_COUNT_MIN, BURST_COUNT_MAX );
    }
    else if( encoderMode == ENCODER_MODE_PREREV )
    {
      unsigned long newPreRevRPM = calculateEncoderPreRevRPM( preRevRPM, encoderDir, ENCODER_PRE_REV_STEP, PRE_REV_RPM_MIN, PRE_REV_RPM_MAX );
      if( hasRPMChanged( preRevRPM, newPreRevRPM ) )
      {
        preRevRPM = newPreRevRPM;
        // Update the idling speed immediately if currently idling
        if( idling )
        {
          FlyshotSetNewMotorSpeed( preRevRPM );
        }
      }
    }
  }

  FireMode mode = getFireMode(
    digitalRead( PIN_SELECT_1 ) == LOW,
    digitalRead( PIN_SELECT_2 ) == LOW
  );

  bool preRevActive = isPreRevActive( digitalRead( PIN_PRE_REV ) == HIGH );
  bool mp5SlapSafe = isMp5SlapSafe( digitalRead( PIN_MP5_SLAP ) == HIGH );

  updateDisplay( mode, motorRPM, burstCount, encoderMode, preRevActive, preRevRPM, !mp5SlapSafe, currentVoltage );

  if( !mp5SlapSafe )
  {
    NBCProcessFlywheelSpeed();
    FlyshotStopMotors();
    idling = false;
    return;
  }

  while( digitalRead( PIN_TRIGGER ) == LOW )
  {
    // Re-check MP5 slap safety each iteration so the safety gate
    // takes effect immediately if the bolt opens mid-hold
    if( !isMp5SlapSafe( digitalRead( PIN_MP5_SLAP ) == HIGH ) )
    {
      NBCProcessFlywheelSpeed();
      FlyshotStopMotors();
      idling = false;
      return;
    }

    // Restore full target RPM when transitioning from idle to firing
    if( preRevActive && idling )
    {
      idling = false;
      FlyshotSetNewMotorSpeed( motorRPM );
    }

    if( !FlyshotStartMotorsAndWait( MAX_WAIT_TIME ) )
    {
      FlyshotStopMotorsAndWait( MAX_WAIT_TIME );
      FlyshotBeep2( FLYSHOT_ESC_BOTH );
      NBCWait( 5000 );
      idling = false;
      return;
    }

    selectFire();
    NBCProcessFlywheelSpeed();
  }

  NBCProcessFlywheelSpeed();

  // Re-read pre-rev switch so toggling during firing takes effect immediately on release
  preRevActive = isPreRevActive( digitalRead( PIN_PRE_REV ) == HIGH );

  if( preRevActive )
  {
    // Drop governor to idle RPM, keep flywheels spinning
    if( !idling )
    {
      FlyshotSetNewMotorSpeed( preRevRPM );
      idling = true;
    }
    FlyshotStartMotors();
  }
  else
  {
    FlyshotStopMotors();
    idling = false;
  }
}
