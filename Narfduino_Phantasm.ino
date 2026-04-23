// Suppress NBC's default PCINT0/PCINT2 ISRs so this sketch can supply its own
// (PCINT0 needs to drive both the tachometer read and the rotary encoder tick).
#define NBC_OVERRIDE_PCINT
#include "NBC.h"
#include <U8x8lib.h>
#include <Wire.h>
#include <RotaryEncoder.h>

// --- Pin Definitions ---
#define PIN_TRIGGER 6
#define PIN_PRE_REV 13
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

// --- Display State Enum ---
enum DisplayState {
  DISPLAY_VIEW,
  DISPLAY_MENU,
  DISPLAY_EDIT,
  DISPLAY_ABOUT
};

// --- Menu Item Enum ---
enum MenuItem {
  MENU_ITEM_RPM,
  MENU_ITEM_BURST,
  MENU_ITEM_PREREV,
  MENU_ITEM_ABOUT,
  MENU_ITEM_BACK,
  MENU_ITEM_COUNT
};

#define DISPLAY_MENU_TIMEOUT_MS 10000UL

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
DisplayState displayState = DISPLAY_VIEW;
DisplayState displayedState = DISPLAY_VIEW;
MenuItem menuSelection = MENU_ITEM_RPM;
MenuItem displayedMenuSelection = MENU_ITEM_RPM;
uint32_t lastActivityMs = 0;
unsigned long displayedRPM = 0;
int displayedBurstCount = 0;
FireMode displayedMode = FIRE_MODE_SINGLE;
bool displayedSafe = false;
unsigned long displayedPreRevRPM = 0;
float displayedVoltage = -1.0;
int lastButtonState = HIGH;
unsigned long lastButtonDebounceTime = 0;
#define BUTTON_DEBOUNCE_MS 200

// Fire-mode selector debounce: transient HIGH/HIGH during 2P3T slider transit
// otherwise dispatches to SINGLE for a few ms; require the raw reading to be
// stable for this window before committing.
#define FIRE_MODE_DEBOUNCE_MS 40UL
FireMode committedFireMode = FIRE_MODE_SINGLE;
FireMode pendingFireMode = FIRE_MODE_SINGLE;
uint32_t pendingFireModeSinceMs = 0;


// --- Startup Splash Bitmap ---
const unsigned char splash[] PROGMEM = {
  0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0x7F,0x7F,0x7F,0x7F,0x7F,0xBF,0xBF,0xBF,0xBF,0xBF,0x3F,0x3F,0x3F,0x3F,0xBF,0xBF,0x3F,0x3F,0xBF,0xBF,0xBF,0x3F,0x5F,0x5F,0x5F,0x5F,0x5F,0x5F,0x5F,0x5F,0x5F,0x5F,0x5F,0x3F,0x3F,0x3F,0xBF,0xBF,0x3F,0x7F,0x7F,0x7F,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0x07,0xF3,0x19,0x0D,0x04,0x02,0x02,0x02,0x02,0x02,0x02,0x00,0x05,0x05,0x05,0x05,0x05,0x05,0x05,0x05,0x05,0x00,0x02,0x02,0x02,0x02,0x02,0x02,0x01,0x01,0x01,0x01,0x00,0x80,0x40,0x20,0x10,0x00,0x00,0x00,0x00,0x00,0x80,0x80,0x00,0x80,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x00,0x02,0x02,0x05,0x01,0x01,0x05,0x05,0x05,0x03,0x0B,0x0B,0x03,0x17,0x17,0x07,0x2F,0x2F,0x4F,0x5F,0x5F,0x1F,0xBF,0xBF,0xBF,0xBF,0xBF,0xBF,0x3F,0x3F,0x7F,0x7F,0x7F,0x7F,0x7F,0x7F,0x7F,0x7F,0x7F,0x7F,0x7F,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFE,0xF8,0xF3,0xF6,0xEC,0xC8,0xD0,0xD0,0xA0,0xA0,0xA0,0xA0,0xA0,0x40,0x40,0x40,0x40,0x40,0xE0,0xA0,0xA0,0xA0,0xA0,0xA0,0xB0,0xD0,0xD0,0xD0,0xE8,0xE8,0xCC,0x9E,0x3F,0x61,0xD0,0x80,0x00,0x00,0x00,0x00,0x00,0x07,0x27,0x07,0x07,0x03,0x00,0x00,0x00,0x00,0x00,0x30,0x00,0x00,0x20,0x20,0x00,0x40,0x40,0x60,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x1C,0x3E,0x3C,0x1E,0x00,0x00,0x00,0x00,0x00,0x00,0x03,0x0E,0xF8,0xE0,0x80,0x80,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x02,0x02,0x04,0x19,0xF3,0x07,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0x7F,0x3F,0x9F,0xDF,0x6F,0x2F,0x2F,0x16,0x16,0x15,0x15,0x2D,0xEA,0x72,0x1A,0x0E,0x06,0x04,0x06,0x02,0x02,0x01,0x01,0x23,0x1E,0x0C,0x08,0x08,0x08,0x08,0x08,0x08,0x3C,0x42,0x02,0x02,0x04,0x04,0x08,0x18,0x18,0x39,0xF8,0x88,0x28,0xE8,0xE8,0xE8,0xE4,0xF7,0xF6,0xFB,0xF9,0xFC,0xFE,0xFD,0xFD,0xFA,0xFA,0xF2,0xF4,0xF4,0xF4,0xE8,0xE8,0xE8,0xE8,0xE8,0xD0,0xD0,0xD0,0xD0,0xD0,0xD0,0xD0,0xD0,0xD0,0xD0,0xD0,0xD0,0xD8,0xE8,0xE8,0xEC,0xF6,0xF3,0xF8,0xFE,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xE0,0xCF,0x99,0xA0,0x60,0x40,0x44,0x8E,0x99,0x90,0x90,0x9F,0x9F,0xE0,0xC0,0x00,0x00,0x00,0x00,0x80,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x80,0x80,0x00,0x00,0x00,0x40,0xF7,0x3E,0xC0,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFE,0xFE,0xFE,0xFE,0xFE,0xFE,0xFC,0xF9,0xFA,0xFA,0xF8,0xFA,0xFA,0xFB,0xFD,0xFD,0xFD,0xFD,0xFD,0xFD,0xFC,0xFC,0xFC,0xFC,0xFC,0xFD,0xFD,0xFD,0xFD,0xFD,0xFD,0xFA,0xFA,0xFA,0xFA,0xFB,0xFD,0xFC,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF
};

// --- Pure Logic: Determine fire mode from 3-position slide switch state ---
// ON-OFF-ON 2P3T wiring (centre position has no contact on either pole):
//   Pos 1 (left):   D2 LOW,  D3 HIGH -> Single
//   Pos 2 (centre): D2 HIGH, D3 HIGH -> Burst
//   Pos 3 (right):  D2 LOW,  D3 LOW  -> Full Auto
// HIGH/LOW is unreachable with this wiring; defaults defensively to SINGLE.
FireMode getFireMode( bool select1Low, bool select2Low )
{
  if( select1Low && select2Low )
    return FIRE_MODE_FULL_AUTO;
  if( !select1Low && !select2Low )
    return FIRE_MODE_BURST;
  return FIRE_MODE_SINGLE;
}

// --- Pure Logic: Debounce the fire-mode reading ---
// Holds committed mode until a different raw reading has been stable for
// debounceMs. Also resets the candidate whenever the raw reading flips, so
// bouncy transit through multiple positions never promotes a transient.
FireMode debounceFireMode( FireMode committed, FireMode rawReading,
  FireMode* pendingCandidate, uint32_t* pendingSinceMs,
  uint32_t nowMs, uint32_t debounceMs )
{
  if( rawReading == committed )
  {
    *pendingCandidate = committed;
    *pendingSinceMs = nowMs;
    return committed;
  }
  if( rawReading != *pendingCandidate )
  {
    *pendingCandidate = rawReading;
    *pendingSinceMs = nowMs;
    return committed;
  }
  if( ( nowMs - *pendingSinceMs ) >= debounceMs )
    return rawReading;
  return committed;
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

// --- Pure Logic: Determine pre-rev state from NC switch reading ---
// The rev microswitch sits behind the trigger with its plunger held
// depressed by the trigger face at rest, so the C+NC pair is open and
// INPUT_PULLUP leaves the pin HIGH. Pulling the trigger slightly releases
// the plunger, the NC contacts close, and the pin is pulled LOW via GND;
// pre-rev therefore engages on pin LOW to idle the flywheels. Further
// trigger travel actuates the main trigger (PIN_TRIGGER) for full RPM
// and firing.
bool isPreRevActive( bool pinHigh )
{
  return !pinHigh;
}

// --- Pure Logic: Determine MP5 slap safety state from NC switch reading ---
// NC switch wiring: closed at rest (bolt locked) pulls pin LOW via GND.
// When bolt is open the switch opens and the pullup pulls pin HIGH = unsafe.
bool isMp5SlapSafe( bool pinHigh )
{
  return !pinHigh;
}

// --- Pure Logic: Check if voltage display needs updating ---
bool hasVoltageChanged( float oldVoltage, float newVoltage )
{
  int oldTenths = (int)( oldVoltage * 10 + 0.5 );
  int newTenths = (int)( newVoltage * 10 + 0.5 );
  return oldTenths != newTenths;
}

// --- Pure Logic: Format voltage for compact 5-char slot ("XX.XV") ---
void formatVoltageShort( float voltage, char* buf, size_t bufSize )
{
  int tenths = (int)( voltage * 10 + 0.5 );
  int whole = tenths / 10;
  int frac = tenths % 10;
  if( whole < 10 )
    snprintf( buf, bufSize, " %d.%dV", whole, frac );
  else
    snprintf( buf, bufSize, "%d.%dV", whole, frac );
}

// --- Pure Logic: Advance display state on encoder click ---
DisplayState transitionDisplayState( DisplayState current, MenuItem selected, bool clicked )
{
  if( !clicked )
    return current;
  if( current == DISPLAY_VIEW )
    return DISPLAY_MENU;
  if( current == DISPLAY_MENU )
  {
    if( selected == MENU_ITEM_BACK )  return DISPLAY_VIEW;
    if( selected == MENU_ITEM_ABOUT ) return DISPLAY_ABOUT;
    return DISPLAY_EDIT;
  }
  return DISPLAY_MENU;
}

// --- Pure Logic: Cycle menu selection with wrap-around ---
MenuItem cycleMenuItem( MenuItem current, int direction )
{
  if( direction == 0 )
    return current;
  int idx = (int)current + direction;
  while( idx < 0 ) idx += MENU_ITEM_COUNT;
  while( idx >= MENU_ITEM_COUNT ) idx -= MENU_ITEM_COUNT;
  return (MenuItem)idx;
}

// --- Pure Logic: Detect menu inactivity timeout (handles millis() rollover) ---
bool isMenuTimeoutExpired( uint32_t lastActivityMs, uint32_t nowMs, uint32_t timeoutMs )
{
  return ( nowMs - lastActivityMs ) >= timeoutMs;
}

// --- Pure Logic: Get menu item label ---
const char* getMenuItemLabel( MenuItem item )
{
  switch( item )
  {
    case MENU_ITEM_BURST:  return "Burst";
    case MENU_ITEM_PREREV: return "PreRev";
    case MENU_ITEM_ABOUT:  return "About";
    case MENU_ITEM_BACK:   return "Back";
    case MENU_ITEM_RPM:
    default:               return "RPM";
  }
}

// --- Pure Logic: Get short centred fire-mode label ("AUTO" for FULL_AUTO) ---
const char* getCenterModeLabel( FireMode mode, bool safe )
{
  if( safe ) return "SAFE";
  switch( mode )
  {
    case FIRE_MODE_BURST:     return "BURST";
    case FIRE_MODE_FULL_AUTO: return "AUTO";
    case FIRE_MODE_SINGLE:
    default:                  return "SINGLE";
  }
}

// --- Pure Logic: Compute starting tile column to horizontally centre a label ---
// on a 16-tile-wide display given its length (in chars) and font scale.
uint8_t centerTileCol( uint8_t labelLen, uint8_t scale )
{
  uint16_t widthTiles = (uint16_t)labelLen * scale;
  if( widthTiles >= 16 ) return 0;
  return ( 16 - widthTiles ) / 2;
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

// --- Sample pins and run one debounce step, returning the committed mode ---
FireMode readAndDebounceFireMode()
{
  FireMode raw = getFireMode(
    digitalRead( PIN_SELECT_1 ) == LOW,
    digitalRead( PIN_SELECT_2 ) == LOW
  );
  committedFireMode = debounceFireMode(
    committedFireMode, raw,
    &pendingFireMode, &pendingFireModeSinceMs,
    millis(), FIRE_MODE_DEBOUNCE_MS
  );
  return committedFireMode;
}

// --- Fire Mode Dispatcher ---
void selectFire( FireMode mode )
{
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

// --- Render full-screen PROGMEM splash via u8x8 tiles ---
// Uses a 128-byte stack buffer (one tile row) so the splash can live in flash.
void drawSplash()
{
  uint8_t buf[128];
  for( uint8_t row = 0; row < 8; row++ )
  {
    memcpy_P( buf, splash + row * 128, 128 );
    display.drawTile( 0, row, 16, buf );
  }
}

// --- VIEW layout: 4 corners (RPM / Burst / PreRev / Battery) + centred mode ---
void drawView( FireMode mode, unsigned long rpm, int burst, unsigned long preRevRPMVal, bool safe, float voltage, bool fullRedraw )
{
  char buf[8];

  if( fullRedraw || hasRPMChanged( displayedRPM, rpm ) )
  {
    snprintf( buf, sizeof( buf ), "R:%-4lu", rpm );
    display.drawString( 0, 0, buf );
    displayedRPM = rpm;
  }

  if( fullRedraw || burst != displayedBurstCount )
  {
    snprintf( buf, sizeof( buf ), "B:%2d", burst );
    display.drawString( 12, 0, buf );
    displayedBurstCount = burst;
  }

  if( fullRedraw || hasRPMChanged( displayedPreRevRPM, preRevRPMVal ) )
  {
    snprintf( buf, sizeof( buf ), "P:%-4lu", preRevRPMVal );
    display.drawString( 0, 7, buf );
    displayedPreRevRPM = preRevRPMVal;
  }

  if( fullRedraw || hasVoltageChanged( displayedVoltage, voltage ) )
  {
    formatVoltageShort( voltage, buf, sizeof( buf ) );
    display.drawString( 11, 7, buf );
    displayedVoltage = voltage;
  }

  if( fullRedraw || mode != displayedMode || safe != displayedSafe )
  {
    // Clear the two tile rows the 2x2 label occupies before redrawing so
    // longer->shorter label transitions do not leave stale characters.
    display.clearLine( 3 );
    display.clearLine( 4 );
    const char* label = getCenterModeLabel( mode, safe );
    uint8_t col = centerTileCol( (uint8_t)strlen( label ), 2 );
    display.draw2x2String( col, 3, label );
    displayedMode = mode;
    displayedSafe = safe;
  }
}

// --- MENU layout: header + items with cursor ---
void drawMenu( MenuItem selected, bool fullRedraw )
{
  if( fullRedraw )
  {
    display.clear();
    display.drawString( centerTileCol( 4, 2 ), 0, "MENU" );
  }

  if( !fullRedraw && selected == displayedMenuSelection )
    return;

  static const MenuItem order[5] = { MENU_ITEM_RPM, MENU_ITEM_BURST, MENU_ITEM_PREREV, MENU_ITEM_ABOUT, MENU_ITEM_BACK };
  for( uint8_t i = 0; i < 5; i++ )
  {
    uint8_t row = 3 + i;
    display.clearLine( row );
    display.drawString( 2, row, order[i] == selected ? ">" : " " );
    display.drawString( 4, row, getMenuItemLabel( order[i] ) );
  }
  displayedMenuSelection = selected;
}

// --- ABOUT layout: static credits text, word-wrapped to 16-tile width ---
void drawAbout( bool fullRedraw )
{
  if( !fullRedraw )
    return;
  display.clear();
  display.drawString( 0, 0, "Owned by: Eluhim" );
  display.drawString( 0, 1, "Made by: Maleko" );
  display.drawString( 0, 3, "Thanks Airzone-" );
  display.drawString( 0, 4, "sama for the" );
  display.drawString( 0, 5, "Narfduino NBC &" );
  display.drawString( 0, 6, "Gifd for the" );
  display.drawString( 0, 7, "Phantasm design" );
}

// --- EDIT layout: header + large current value ---
void drawEdit( MenuItem item, unsigned long rpm, int burst, unsigned long preRevRPMVal, bool fullRedraw )
{
  char buf[8];

  if( fullRedraw )
  {
    display.clear();
    display.drawString( 0, 0, "EDIT " );
    display.drawString( 5, 0, getMenuItemLabel( item ) );
    display.drawString( 0, 7, "Click to save" );
  }

  switch( item )
  {
    case MENU_ITEM_BURST:
      snprintf( buf, sizeof( buf ), "%d", burst );
      break;
    case MENU_ITEM_PREREV:
      snprintf( buf, sizeof( buf ), "%lu", preRevRPMVal );
      break;
    case MENU_ITEM_RPM:
    default:
      snprintf( buf, sizeof( buf ), "%lu", rpm );
      break;
  }
  display.clearLine( 3 );
  display.clearLine( 4 );
  uint8_t col = centerTileCol( (uint8_t)strlen( buf ), 2 );
  display.draw2x2String( col, 3, buf );
}

// --- Display dispatcher (clears on state change; dirty-checks within state) ---
void updateDisplay( FireMode mode, unsigned long rpm, int burst, unsigned long preRevRPMVal, bool safe, float voltage )
{
  bool stateChanged = ( displayState != displayedState );
  if( stateChanged && displayState == DISPLAY_VIEW )
    display.clear();

  switch( displayState )
  {
    case DISPLAY_MENU:
      drawMenu( menuSelection, stateChanged );
      break;
    case DISPLAY_EDIT:
      drawEdit( menuSelection, rpm, burst, preRevRPMVal, stateChanged );
      break;
    case DISPLAY_ABOUT:
      drawAbout( stateChanged );
      break;
    case DISPLAY_VIEW:
    default:
      drawView( mode, rpm, burst, preRevRPMVal, safe, voltage, stateChanged );
      break;
  }

  displayedState = displayState;
}

// --- Setup ---
void setup()
{
  Serial.begin( 115200 );
  Serial.println( F( "--- Phantasm boot ---" ) );

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
  drawSplash();

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
  displayState = DISPLAY_VIEW;
  displayedState = DISPLAY_VIEW;
  menuSelection = MENU_ITEM_RPM;
  displayedMenuSelection = MENU_ITEM_RPM;
  displayedMode = FIRE_MODE_SINGLE;
  displayedSafe = false;
  displayedRPM = 0;
  displayedBurstCount = 0;
  displayedPreRevRPM = 0;
  displayedVoltage = -1.0;
  lastActivityMs = millis();

  // Ensure the debounce period has already "elapsed" so the very first button
  // press in loop() registers immediately, regardless of how quickly setup() ran.
  lastButtonDebounceTime = millis() - ( BUTTON_DEBOUNCE_MS + 1 );
}

// --- Apply encoder rotation to currently-selected parameter in EDIT state ---
void applyEditRotation( MenuItem item, int direction )
{
  if( direction == 0 )
    return;

  if( item == MENU_ITEM_RPM )
  {
    unsigned long newRPM = calculateEncoderRPM( motorRPM, direction, ENCODER_RPM_STEP, MOTOR_RPM_MIN, MOTOR_RPM_MAX );
    if( hasRPMChanged( motorRPM, newRPM ) )
    {
      motorRPM = newRPM;
      if( !idling )
        FlyshotSetNewMotorSpeed( motorRPM );
    }
  }
  else if( item == MENU_ITEM_BURST )
  {
    burstCount = calculateEncoderBurst( burstCount, direction, ENCODER_BURST_STEP, BURST_COUNT_MIN, BURST_COUNT_MAX );
  }
  else if( item == MENU_ITEM_PREREV )
  {
    unsigned long newPreRevRPM = calculateEncoderPreRevRPM( preRevRPM, direction, ENCODER_PRE_REV_STEP, PRE_REV_RPM_MIN, PRE_REV_RPM_MAX );
    if( hasRPMChanged( preRevRPM, newPreRevRPM ) )
    {
      preRevRPM = newPreRevRPM;
      if( idling )
        FlyshotSetNewMotorSpeed( preRevRPM );
    }
  }
}

// --- Main Loop ---
void loop()
{
  float currentVoltage = NBCGetVoltage();
  if( currentVoltage < MIN_BATTERY_VOLTAGE )
    return;

  // Encoder button drives the VIEW <-> MENU <-> EDIT state machine
  bool clicked = pollEncoderButton();
  if( clicked )
  {
    displayState = transitionDisplayState( displayState, menuSelection, true );
    lastActivityMs = millis();
  }

  // Encoder rotation is routed by current display state
  int encoderDir = pollEncoderDirection();
  if( encoderDir != 0 )
  {
    lastActivityMs = millis();
    if( displayState == DISPLAY_MENU )
    {
      menuSelection = cycleMenuItem( menuSelection, encoderDir );
    }
    else if( displayState == DISPLAY_EDIT )
    {
      applyEditRotation( menuSelection, encoderDir );
    }
    // Rotation in VIEW is ignored; the menu is the only place to edit values.
  }

  // Fall back to VIEW after inactivity in MENU/EDIT
  if( displayState != DISPLAY_VIEW && isMenuTimeoutExpired( lastActivityMs, millis(), DISPLAY_MENU_TIMEOUT_MS ) )
  {
    displayState = DISPLAY_VIEW;
  }

  FireMode mode = readAndDebounceFireMode();

  bool preRevActive = isPreRevActive( digitalRead( PIN_PRE_REV ) == HIGH );
  bool mp5SlapSafe = isMp5SlapSafe( digitalRead( PIN_MP5_SLAP ) == HIGH );

  // Edge-detecting serial logging for switch state changes (debug aid)
  static FireMode lastLoggedMode = (FireMode)-1;
  static int lastLoggedPreRev = -1;
  static int lastLoggedMp5Safe = -1;
  static int lastLoggedTrigger = -1;

  if( mode != lastLoggedMode )
  {
    Serial.print( F( "Fire mode: " ) );
    Serial.println( getFireModeLabel( mode ) );
    lastLoggedMode = mode;
  }
  if( (int)preRevActive != lastLoggedPreRev )
  {
    Serial.print( F( "Pre-rev switch: " ) );
    Serial.println( preRevActive ? F( "ON" ) : F( "OFF" ) );
    lastLoggedPreRev = (int)preRevActive;
  }
  if( (int)mp5SlapSafe != lastLoggedMp5Safe )
  {
    Serial.print( F( "MP5 slap: " ) );
    Serial.println( mp5SlapSafe ? F( "SAFE (bolt locked)" ) : F( "UNSAFE (bolt open)" ) );
    lastLoggedMp5Safe = (int)mp5SlapSafe;
  }
  int triggerNow = digitalRead( PIN_TRIGGER );
  if( triggerNow != lastLoggedTrigger )
  {
    Serial.println( triggerNow == LOW ? F( "Trigger: PRESSED" ) : F( "Trigger: RELEASED" ) );
    lastLoggedTrigger = triggerNow;
  }

  // Any firing/prerev activity snaps the UI back to the default VIEW so the
  // operator can see live stats rather than a stale edit screen.
  if( ( digitalRead( PIN_TRIGGER ) == LOW || preRevActive ) && displayState != DISPLAY_VIEW )
  {
    displayState = DISPLAY_VIEW;
  }

  updateDisplay( mode, motorRPM, burstCount, preRevRPM, !mp5SlapSafe, currentVoltage );

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

    selectFire( readAndDebounceFireMode() );
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
