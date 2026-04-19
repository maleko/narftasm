// Suppress NBC's default PCINT0/PCINT2 ISRs so this sketch can supply its own
// (PCINT0 needs to drive both the tachometer read and the rotary encoder tick).
#define NBC_OVERRIDE_PCINT
#include "NBC.h"
#include <U8x8lib.h>
#include <Wire.h>
#include <RotaryEncoder.h>

// --- Pin Definitions ---
#define PIN_TRIGGER 6
#define PIN_PRE_REV A1
#define PIN_SELECT_1 2
#define PIN_SELECT_2 3
#define PIN_ENCODER_CLK A3
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

// --- Rotary Encoder (mathertel/RotaryEncoder, ticked from PCINT ISRs) ---
// KY-040 detents at both pins HIGH, so LatchMode::FOUR3. Pin order
// (DT, CLK) is chosen so clockwise rotation yields positive deltas, i.e.
// pollEncoderDirection() returns +1 for CW and -1 for CCW. If hardware
// reads inverted, swap the first two constructor args.
// CLK is on A3 (PC3) and DT is on D11 (PB3), so each pin sits on a
// different PCINT vector (see the ISRs below).
RotaryEncoder encoder( PIN_ENCODER_DT, PIN_ENCODER_CLK, RotaryEncoder::LatchMode::FOUR3 );

// --- PCINT ISR overrides (see NBC_OVERRIDE_PCINT at top of file) ---
// Encoder DT (D11/PB3/PCINT3) shares the PCINT0 vector with NBC's tach1
// input (PB0/PCINT0). Encoder CLK (A3/PC3/PCINT11) sits on the PCINT1
// vector which nothing else in the sketch uses. Calling encoder.tick()
// from both handlers guarantees every quadrature edge is observed, which
// LatchMode::FOUR3 requires to advance the reported position even when
// loop() stalls on the I2C display refresh or the trigger hold loop.
// ReadTach1() / ReadTach2() are kept so tachometer RPM continues to work
// unchanged.
ISR( PCINT0_vect )
{
  ReadTach1();
  encoder.tick();
}
ISR( PCINT1_vect )
{
  encoder.tick();
}
ISR( PCINT2_vect )
{
  ReadTach2();
}

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
int lastButtonState = HIGH;
unsigned long lastButtonDebounceTime = 0;
#define BUTTON_DEBOUNCE_MS 200


// --- Startup Splash Bitmap ---
const unsigned char splash[] PROGMEM = {
  0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0x7F,0x7F,0x7F,0x7F,0x7F,0xBF,0xBF,0xBF,0xBF,0xBF,0x3F,0x3F,0x3F,0x3F,0xBF,0xBF,0x3F,0x3F,0xBF,0xBF,0xBF,0x3F,0x5F,0x5F,0x5F,0x5F,0x5F,0x5F,0x5F,0x5F,0x5F,0x5F,0x5F,0x3F,0x3F,0x3F,0xBF,0xBF,0x3F,0x7F,0x7F,0x7F,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0x07,0xF3,0x19,0x0D,0x04,0x02,0x02,0x02,0x02,0x02,0x02,0x00,0x05,0x05,0x05,0x05,0x05,0x05,0x05,0x05,0x05,0x00,0x02,0x02,0x02,0x02,0x02,0x02,0x01,0x01,0x01,0x01,0x00,0x80,0x40,0x20,0x10,0x00,0x00,0x00,0x00,0x00,0x80,0x80,0x00,0x80,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x00,0x02,0x02,0x05,0x01,0x01,0x05,0x05,0x05,0x03,0x0B,0x0B,0x03,0x17,0x17,0x07,0x2F,0x2F,0x4F,0x5F,0x5F,0x1F,0xBF,0xBF,0xBF,0xBF,0xBF,0xBF,0x3F,0x3F,0x7F,0x7F,0x7F,0x7F,0x7F,0x7F,0x7F,0x7F,0x7F,0x7F,0x7F,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFE,0xF8,0xF3,0xF6,0xEC,0xC8,0xD0,0xD0,0xA0,0xA0,0xA0,0xA0,0xA0,0x40,0x40,0x40,0x40,0x40,0xE0,0xA0,0xA0,0xA0,0xA0,0xA0,0xB0,0xD0,0xD0,0xD0,0xE8,0xE8,0xCC,0x9E,0x3F,0x61,0xD0,0x80,0x00,0x00,0x00,0x00,0x00,0x07,0x27,0x07,0x07,0x03,0x00,0x00,0x00,0x00,0x00,0x30,0x00,0x00,0x20,0x20,0x00,0x40,0x40,0x60,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x1C,0x3E,0x3C,0x1E,0x00,0x00,0x00,0x00,0x00,0x00,0x03,0x0E,0xF8,0xE0,0x80,0x80,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x02,0x02,0x04,0x19,0xF3,0x07,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0x7F,0x3F,0x9F,0xDF,0x6F,0x2F,0x2F,0x16,0x16,0x15,0x15,0x2D,0xEA,0x72,0x1A,0x0E,0x06,0x04,0x06,0x02,0x02,0x01,0x01,0x23,0x1E,0x0C,0x08,0x08,0x08,0x08,0x08,0x08,0x3C,0x42,0x02,0x02,0x04,0x04,0x08,0x18,0x18,0x39,0xF8,0x88,0x28,0xE8,0xE8,0xE8,0xE4,0xF7,0xF6,0xFB,0xF9,0xFC,0xFE,0xFD,0xFD,0xFA,0xFA,0xF2,0xF4,0xF4,0xF4,0xE8,0xE8,0xE8,0xE8,0xE8,0xD0,0xD0,0xD0,0xD0,0xD0,0xD0,0xD0,0xD0,0xD0,0xD0,0xD0,0xD0,0xD8,0xE8,0xE8,0xEC,0xF6,0xF3,0xF8,0xFE,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xE0,0xCF,0x99,0xA0,0x60,0x40,0x44,0x8E,0x99,0x90,0x90,0x9F,0x9F,0xE0,0xC0,0x00,0x00,0x00,0x00,0x80,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x80,0x80,0x00,0x00,0x00,0x40,0xF7,0x3E,0xC0,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFE,0xFE,0xFE,0xFE,0xFE,0xFE,0xFC,0xF9,0xFA,0xFA,0xF8,0xFA,0xFA,0xFB,0xFD,0xFD,0xFD,0xFD,0xFD,0xFD,0xFC,0xFC,0xFC,0xFC,0xFC,0xFD,0xFD,0xFD,0xFD,0xFD,0xFD,0xFA,0xFA,0xFA,0xFA,0xFB,0xFD,0xFC,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF
};

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

// --- Encoder Polling ---
// encoder.tick() is driven by the PCINT0 ISR above, so this function only
// consumes the decoded direction. Reads are wrapped in noInterrupts() to
// take a consistent snapshot of the library's internal counters while the
// ISR may fire at any time.
// Returns +1 for one clockwise detent, -1 for counter-clockwise, 0 otherwise.
int pollEncoderDirection()
{
  noInterrupts();
  RotaryEncoder::Direction dir = encoder.getDirection();
  interrupts();

  if( dir == RotaryEncoder::Direction::CLOCKWISE )
    return 1;
  if( dir == RotaryEncoder::Direction::COUNTERCLOCKWISE )
    return -1;
  return 0;
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
  pinMode( PIN_ENCODER_SW, INPUT_PULLUP );

  // Encoder CLK/DT pins are configured as INPUT_PULLUP by the RotaryEncoder
  // constructor; no explicit pinMode needed here.
  lastButtonState = digitalRead( PIN_ENCODER_SW );

  display.begin();
  display.setFont( u8x8_font_chroma48medium8_r );
  display.setCursor( 0, 0 );
  display.print( "PHANTASM" );
  display.setCursor( 0, 2 );
  display.print( "Initialising..." );

  delay( 1000 );

  NBCInit();

  // Unmask pin-change interrupts for the encoder:
  //   DT  = D11 / PB3 / PCINT3 -> PCINT0_vect (shared with NBC tach1)
  //   CLK = A3  / PC3 / PCINT11 -> PCINT1_vect (otherwise unused)
  // NBCInit() has already configured PCMSK0 / PCIE0 for the tachometer,
  // so we only add PCINT3 there. PCINT1 needs both its mask bit and its
  // group-enable bit in PCICR set here.
  PCMSK0 |= ( 1 << PCINT3 );
  PCMSK1 |= ( 1 << PCINT11 );
  PCICR  |= ( 1 << PCIE1 );

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
