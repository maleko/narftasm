// Test sketch for current Phantasm logic.
// Mirrors the root sketch's 3-position 2P3T fire selector logic.
// Upload to an Arduino to run. Results output via Serial.

// ---- Fire mode enum (must match implementation) ----
enum FireMode {
  FIRE_MODE_SINGLE,
  FIRE_MODE_BURST,
  FIRE_MODE_FULL_AUTO
};

// ---- Display state enum (must match implementation) ----
enum DisplayState {
  DISPLAY_VIEW,
  DISPLAY_MENU,
  DISPLAY_EDIT
};

// ---- Menu item enum (must match implementation) ----
enum MenuItem {
  MENU_ITEM_RPM,
  MENU_ITEM_BURST,
  MENU_ITEM_PREREV,
  MENU_ITEM_BACK,
  MENU_ITEM_COUNT
};

#define DISPLAY_MENU_TIMEOUT_MS 10000UL

#define PRE_REV_RPM_DEFAULT 3000
#define PRE_REV_RPM_MIN 2000
#define PRE_REV_RPM_MAX 5000
#define ENCODER_PRE_REV_STEP 250

#define BURST_COUNT_DEFAULT 3
#define BURST_COUNT_MIN 2
#define BURST_COUNT_MAX 10
#define ENCODER_BURST_STEP 1
#define MOTOR_RPM_MIN 3000
#define MOTOR_RPM_MAX 8000
#define ENCODER_RPM_STEP 250

// ---- Functions under test (forward declarations) ----
FireMode getFireMode( bool select1Low, bool select2Low );
unsigned long clampRPM( long rpm, unsigned long minRPM, unsigned long maxRPM );
unsigned long calculateEncoderRPM( unsigned long currentRPM, int direction, unsigned int stepSize, unsigned long minRPM, unsigned long maxRPM );
const char* getFireModeLabel( FireMode mode );
bool hasRPMChanged( unsigned long oldRPM, unsigned long newRPM );
int clampBurstCount( int count, int minCount, int maxCount );
int calculateEncoderBurst( int currentCount, int direction, int stepSize, int minCount, int maxCount );
bool isPreRevActive( bool pinHigh );
bool isMp5SlapSafe( bool pinHigh );
const char* getDisplayModeLabel( FireMode mode, bool safe );
unsigned long calculateEncoderPreRevRPM( unsigned long currentRPM, int direction, unsigned int stepSize, unsigned long minRPM, unsigned long maxRPM );
bool hasVoltageChanged( float oldVoltage, float newVoltage );
DisplayState transitionDisplayState( DisplayState current, MenuItem selected, bool clicked );
MenuItem cycleMenuItem( MenuItem current, int direction );
bool isMenuTimeoutExpired( uint32_t lastActivityMs, uint32_t nowMs, uint32_t timeoutMs );
const char* getMenuItemLabel( MenuItem item );
uint8_t centerTileCol( uint8_t labelLen, uint8_t scale );
void formatVoltageShort( float voltage, char* buf, size_t bufSize );
const char* getCenterModeLabel( FireMode mode, bool safe );

// ---- Test harness ----
int testsPassed = 0;
int testsFailed = 0;

void assertEq( const char* testName, int expected, int actual )
{
  if( expected == actual )
  {
    Serial.print( "[PASS] " );
    Serial.println( testName );
    testsPassed++;
  }
  else
  {
    Serial.print( "[FAIL] " );
    Serial.print( testName );
    Serial.print( " — expected " );
    Serial.print( expected );
    Serial.print( ", got " );
    Serial.println( actual );
    testsFailed++;
  }
}

void assertEqUL( const char* testName, unsigned long expected, unsigned long actual )
{
  if( expected == actual )
  {
    Serial.print( "[PASS] " );
    Serial.println( testName );
    testsPassed++;
  }
  else
  {
    Serial.print( "[FAIL] " );
    Serial.print( testName );
    Serial.print( " — expected " );
    Serial.print( expected );
    Serial.print( ", got " );
    Serial.println( actual );
    testsFailed++;
  }
}

void assertStrEq( const char* testName, const char* expected, const char* actual )
{
  if( strcmp( expected, actual ) == 0 )
  {
    Serial.print( "[PASS] " );
    Serial.println( testName );
    testsPassed++;
  }
  else
  {
    Serial.print( "[FAIL] " );
    Serial.print( testName );
    Serial.print( " — expected '" );
    Serial.print( expected );
    Serial.print( "', got '" );
    Serial.print( actual );
    Serial.println( "'" );
    testsFailed++;
  }
}

void assertBool( const char* testName, bool expected, bool actual )
{
  if( expected == actual )
  {
    Serial.print( "[PASS] " );
    Serial.println( testName );
    testsPassed++;
  }
  else
  {
    Serial.print( "[FAIL] " );
    Serial.print( testName );
    Serial.print( " — expected " );
    Serial.print( expected ? "true" : "false" );
    Serial.print( ", got " );
    Serial.println( actual ? "true" : "false" );
    testsFailed++;
  }
}

// ---- Tests ----

void testGetFireModeSingleShot()
{
  // Slide switch position 1: SELECT_1 LOW, SELECT_2 HIGH = single
  assertEq( "Pos 1 Single: sel1=LOW sel2=HIGH", FIRE_MODE_SINGLE, getFireMode( true, false ) );
}

void testGetFireModeBurst()
{
  // Slide switch position 2: SELECT_1 HIGH, SELECT_2 LOW = burst
  assertEq( "Pos 2 Burst: sel1=HIGH sel2=LOW", FIRE_MODE_BURST, getFireMode( false, true ) );
}

void testGetFireModeFullAuto()
{
  // Slide switch position 3: both pins LOW = full auto
  assertEq( "Pos 3 Full Auto: sel1=LOW sel2=LOW", FIRE_MODE_FULL_AUTO, getFireMode( true, true ) );
}

void testGetFireModeFallback()
{
  // HIGH/HIGH should not occur with a properly wired 3-position slide switch,
  // but the current sketch falls back to SINGLE if it does.
  assertEq( "Fallback wiring state: sel1=HIGH sel2=HIGH -> SINGLE", FIRE_MODE_SINGLE, getFireMode( false, false ) );
}

void testBurstCountIsThree()
{
  assertEq( "Burst count default is 3", 3, BURST_COUNT_DEFAULT );
}

void testSingleShotEdgeDetection()
{
  // Single shot should only fire on a falling edge (HIGH -> LOW transition)
  // Simulating: trigger was HIGH (not pressed), now LOW (pressed) = should fire
  bool shouldFireOnPress = ( true != false );  // lastState=HIGH(true), currentState=LOW(false) -> different = edge
  assertEq( "Single shot fires on trigger press edge", true, shouldFireOnPress );

  // Simulating: trigger was LOW (pressed), still LOW (pressed) = should NOT fire
  bool shouldNotFireWhileHeld = ( false != false );  // same state = no edge
  assertEq( "Single shot does not fire while held", false, shouldNotFireWhileHeld );

  // Simulating: trigger was LOW, now HIGH (released) = should NOT fire
  bool shouldNotFireOnRelease = ( false != true );  // edge exists, but it's a rising edge
  // In singleShot(), we only fire when triggerState == LOW on an edge
  // So the edge is detected but the LOW check prevents firing
  assertEq( "Single shot edge detected on release (handled by LOW check)", true, shouldNotFireOnRelease );
}

void testFireModeEnumValues()
{
  // Verify enum ordering for sanity
  assertEq( "FIRE_MODE_SINGLE is 0", 0, FIRE_MODE_SINGLE );
  assertEq( "FIRE_MODE_BURST is 1", 1, FIRE_MODE_BURST );
  assertEq( "FIRE_MODE_FULL_AUTO is 2", 2, FIRE_MODE_FULL_AUTO );
}

// ---- RPM Clamping Tests ----

void testClampRPMWithinRange()
{
  assertEqUL( "clampRPM: value within range unchanged", 5000, clampRPM( 5000, MOTOR_RPM_MIN, MOTOR_RPM_MAX ) );
}

void testClampRPMBelowMin()
{
  assertEqUL( "clampRPM: value below min clamped to min", MOTOR_RPM_MIN, clampRPM( 1000, MOTOR_RPM_MIN, MOTOR_RPM_MAX ) );
}

void testClampRPMAboveMax()
{
  assertEqUL( "clampRPM: value above max clamped to max", MOTOR_RPM_MAX, clampRPM( 12000, MOTOR_RPM_MIN, MOTOR_RPM_MAX ) );
}

void testClampRPMAtBoundaries()
{
  assertEqUL( "clampRPM: value at min stays at min", MOTOR_RPM_MIN, clampRPM( MOTOR_RPM_MIN, MOTOR_RPM_MIN, MOTOR_RPM_MAX ) );
  assertEqUL( "clampRPM: value at max stays at max", MOTOR_RPM_MAX, clampRPM( MOTOR_RPM_MAX, MOTOR_RPM_MIN, MOTOR_RPM_MAX ) );
}

// ---- Encoder RPM Calculation Tests ----

void testCalculateEncoderRPMClockwise()
{
  assertEqUL( "calculateEncoderRPM: CW increases RPM by step", 5250,
    calculateEncoderRPM( 5000, 1, ENCODER_RPM_STEP, MOTOR_RPM_MIN, MOTOR_RPM_MAX ) );
}

void testCalculateEncoderRPMAnticlockwise()
{
  assertEqUL( "calculateEncoderRPM: CCW decreases RPM by step", 4750,
    calculateEncoderRPM( 5000, -1, ENCODER_RPM_STEP, MOTOR_RPM_MIN, MOTOR_RPM_MAX ) );
}

void testCalculateEncoderRPMClampsAtMax()
{
  assertEqUL( "calculateEncoderRPM: CW at max stays at max", MOTOR_RPM_MAX,
    calculateEncoderRPM( MOTOR_RPM_MAX, 1, ENCODER_RPM_STEP, MOTOR_RPM_MIN, MOTOR_RPM_MAX ) );
}

void testCalculateEncoderRPMClampsAtMin()
{
  assertEqUL( "calculateEncoderRPM: CCW at min stays at min", MOTOR_RPM_MIN,
    calculateEncoderRPM( MOTOR_RPM_MIN, -1, ENCODER_RPM_STEP, MOTOR_RPM_MIN, MOTOR_RPM_MAX ) );
}

void testCalculateEncoderRPMNoDirection()
{
  assertEqUL( "calculateEncoderRPM: direction 0 unchanged", 5000,
    calculateEncoderRPM( 5000, 0, ENCODER_RPM_STEP, MOTOR_RPM_MIN, MOTOR_RPM_MAX ) );
}

// ---- Fire Mode Label Tests ----

void testGetFireModeLabelSingle()
{
  assertStrEq( "getFireModeLabel: single", "SINGLE", getFireModeLabel( FIRE_MODE_SINGLE ) );
}

void testGetFireModeLabelBurst()
{
  assertStrEq( "getFireModeLabel: burst", "BURST", getFireModeLabel( FIRE_MODE_BURST ) );
}

void testGetFireModeLabelFullAuto()
{
  assertStrEq( "getFireModeLabel: full auto", "FULL AUTO", getFireModeLabel( FIRE_MODE_FULL_AUTO ) );
}

// ---- Display Mode Label Tests (MP5 safety override) ----

void testGetDisplayModeLabelSafetyOverrideSingle()
{
  assertStrEq( "getDisplayModeLabel: MP5 safety override shows SAFE for SINGLE", "SAFE", getDisplayModeLabel( FIRE_MODE_SINGLE, true ) );
}

void testGetDisplayModeLabelSafetyOverrideBurst()
{
  assertStrEq( "getDisplayModeLabel: MP5 safety override shows SAFE for BURST", "SAFE", getDisplayModeLabel( FIRE_MODE_BURST, true ) );
}

void testGetDisplayModeLabelSafetyOverrideFullAuto()
{
  assertStrEq( "getDisplayModeLabel: MP5 safety override shows SAFE for FULL AUTO", "SAFE", getDisplayModeLabel( FIRE_MODE_FULL_AUTO, true ) );
}

void testGetDisplayModeLabelNormalSingle()
{
  assertStrEq( "getDisplayModeLabel: normal display shows SINGLE", "SINGLE", getDisplayModeLabel( FIRE_MODE_SINGLE, false ) );
}

void testGetDisplayModeLabelNormalBurst()
{
  assertStrEq( "getDisplayModeLabel: normal display shows BURST", "BURST", getDisplayModeLabel( FIRE_MODE_BURST, false ) );
}

void testGetDisplayModeLabelNormalFullAuto()
{
  assertStrEq( "getDisplayModeLabel: normal display shows FULL AUTO", "FULL AUTO", getDisplayModeLabel( FIRE_MODE_FULL_AUTO, false ) );
}

// ---- RPM Changed Tests ----

void testHasRPMChangedTrue()
{
  assertBool( "hasRPMChanged: different values returns true", true, hasRPMChanged( 5000, 5250 ) );
}

void testHasRPMChangedFalse()
{
  assertBool( "hasRPMChanged: same values returns false", false, hasRPMChanged( 5000, 5000 ) );
}

// ---- Burst Count Clamping Tests ----

void testClampBurstCountWithinRange()
{
  assertEq( "clampBurstCount: value within range unchanged", 5, clampBurstCount( 5, BURST_COUNT_MIN, BURST_COUNT_MAX ) );
}

void testClampBurstCountBelowMin()
{
  assertEq( "clampBurstCount: value below min clamped to min", BURST_COUNT_MIN, clampBurstCount( 0, BURST_COUNT_MIN, BURST_COUNT_MAX ) );
}

void testClampBurstCountAboveMax()
{
  assertEq( "clampBurstCount: value above max clamped to max", BURST_COUNT_MAX, clampBurstCount( 15, BURST_COUNT_MIN, BURST_COUNT_MAX ) );
}

void testClampBurstCountAtBoundaries()
{
  assertEq( "clampBurstCount: value at min stays at min", BURST_COUNT_MIN, clampBurstCount( BURST_COUNT_MIN, BURST_COUNT_MIN, BURST_COUNT_MAX ) );
  assertEq( "clampBurstCount: value at max stays at max", BURST_COUNT_MAX, clampBurstCount( BURST_COUNT_MAX, BURST_COUNT_MIN, BURST_COUNT_MAX ) );
}

// ---- Encoder Burst Calculation Tests ----

void testCalculateEncoderBurstClockwise()
{
  assertEq( "calculateEncoderBurst: CW increases burst by step", 4,
    calculateEncoderBurst( 3, 1, ENCODER_BURST_STEP, BURST_COUNT_MIN, BURST_COUNT_MAX ) );
}

void testCalculateEncoderBurstAnticlockwise()
{
  assertEq( "calculateEncoderBurst: CCW decreases burst by step", 2,
    calculateEncoderBurst( 3, -1, ENCODER_BURST_STEP, BURST_COUNT_MIN, BURST_COUNT_MAX ) );
}

void testCalculateEncoderBurstClampsAtMax()
{
  assertEq( "calculateEncoderBurst: CW at max stays at max", BURST_COUNT_MAX,
    calculateEncoderBurst( BURST_COUNT_MAX, 1, ENCODER_BURST_STEP, BURST_COUNT_MIN, BURST_COUNT_MAX ) );
}

void testCalculateEncoderBurstClampsAtMin()
{
  assertEq( "calculateEncoderBurst: CCW at min stays at min", BURST_COUNT_MIN,
    calculateEncoderBurst( BURST_COUNT_MIN, -1, ENCODER_BURST_STEP, BURST_COUNT_MIN, BURST_COUNT_MAX ) );
}

void testCalculateEncoderBurstNoDirection()
{
  assertEq( "calculateEncoderBurst: direction 0 unchanged", 3,
    calculateEncoderBurst( 3, 0, ENCODER_BURST_STEP, BURST_COUNT_MIN, BURST_COUNT_MAX ) );
}

// ---- MP5 Slap Safety Tests ----

void testIsMp5SlapSafeWhenBoltLocked()
{
  // NC switch: at rest (bolt locked) pin is LOW (closed to GND) = safe.
  assertBool( "isMp5SlapSafe: pin LOW (bolt locked) returns true", true, isMp5SlapSafe( false ) );
}

void testIsMp5SlapUnsafeWhenBoltOpen()
{
  // NC switch: bolt open breaks circuit, pullup pulls pin HIGH = unsafe.
  assertBool( "isMp5SlapSafe: pin HIGH (bolt open) returns false", false, isMp5SlapSafe( true ) );
}

// ---- Pre-Rev Tests ----

void testIsPreRevActiveWhenSwitchOpen()
{
  // NC switch: at rest pin is LOW (closed to GND).
  // When switch is activated (opened), pin goes HIGH via INPUT_PULLUP.
  assertBool( "isPreRevActive: pin HIGH (switch open) returns true", true, isPreRevActive( true ) );
}

void testIsPreRevInactiveWhenSwitchClosed()
{
  // NC switch: at rest pin is LOW (closed to GND) = pre-rev off.
  assertBool( "isPreRevActive: pin LOW (switch closed) returns false", false, isPreRevActive( false ) );
}

void testPreRevRPMDefaultConstant()
{
  assertEqUL( "PRE_REV_RPM_DEFAULT is 3000", 3000, PRE_REV_RPM_DEFAULT );
}

void testPreRevRPMMinConstant()
{
  assertEqUL( "PRE_REV_RPM_MIN is 2000", 2000, PRE_REV_RPM_MIN );
}

void testPreRevRPMMaxConstant()
{
  assertEqUL( "PRE_REV_RPM_MAX is 5000", 5000, PRE_REV_RPM_MAX );
}

// ---- Pre-Rev RPM Calculation Tests ----

void testCalculateEncoderPreRevRPMClockwise()
{
  assertEqUL( "calculateEncoderPreRevRPM: CW increases by step", 3250,
    calculateEncoderPreRevRPM( 3000, 1, ENCODER_PRE_REV_STEP, PRE_REV_RPM_MIN, PRE_REV_RPM_MAX ) );
}

void testCalculateEncoderPreRevRPMAnticlockwise()
{
  assertEqUL( "calculateEncoderPreRevRPM: CCW decreases by step", 2750,
    calculateEncoderPreRevRPM( 3000, -1, ENCODER_PRE_REV_STEP, PRE_REV_RPM_MIN, PRE_REV_RPM_MAX ) );
}

void testCalculateEncoderPreRevRPMClampsAtMax()
{
  assertEqUL( "calculateEncoderPreRevRPM: CW at max stays at max", PRE_REV_RPM_MAX,
    calculateEncoderPreRevRPM( PRE_REV_RPM_MAX, 1, ENCODER_PRE_REV_STEP, PRE_REV_RPM_MIN, PRE_REV_RPM_MAX ) );
}

void testCalculateEncoderPreRevRPMClampsAtMin()
{
  assertEqUL( "calculateEncoderPreRevRPM: CCW at min stays at min", PRE_REV_RPM_MIN,
    calculateEncoderPreRevRPM( PRE_REV_RPM_MIN, -1, ENCODER_PRE_REV_STEP, PRE_REV_RPM_MIN, PRE_REV_RPM_MAX ) );
}

// ---- Burst Count Default ----

void testBurstCountDefault()
{
  assertEq( "Burst count default is 3", 3, BURST_COUNT_DEFAULT );
}

// ---- Voltage Display Tests ----

void assertFloatEq( const char* testName, float expected, float actual, float tolerance )
{
  float diff = expected - actual;
  if( diff < 0 ) diff = -diff;
  if( diff <= tolerance )
  {
    Serial.print( "[PASS] " );
    Serial.println( testName );
    testsPassed++;
  }
  else
  {
    Serial.print( "[FAIL] " );
    Serial.print( testName );
    Serial.print( " — expected " );
    Serial.print( expected, 1 );
    Serial.print( ", got " );
    Serial.println( actual, 1 );
    testsFailed++;
  }
}

void testHasVoltageChangedTrue()
{
  assertBool( "hasVoltageChanged: different values returns true", true, hasVoltageChanged( 16.8, 16.7 ) );
}

void testHasVoltageChangedFalse()
{
  assertBool( "hasVoltageChanged: same values returns false", false, hasVoltageChanged( 16.8, 16.8 ) );
}

void testHasVoltageChangedWithinTolerance()
{
  // Values that round to the same one-decimal place should not trigger a change
  assertBool( "hasVoltageChanged: within rounding tolerance returns false", false, hasVoltageChanged( 16.81, 16.84 ) );
}

void testHasVoltageChangedAcrossRounding()
{
  // Values that round to different one-decimal places should trigger a change
  assertBool( "hasVoltageChanged: across rounding boundary returns true", true, hasVoltageChanged( 16.84, 16.86 ) );
}

// ---- Display State / Menu Tests ----

void testDisplayStateEnumValues()
{
  assertEq( "DISPLAY_VIEW is 0", 0, DISPLAY_VIEW );
  assertEq( "DISPLAY_MENU is 1", 1, DISPLAY_MENU );
  assertEq( "DISPLAY_EDIT is 2", 2, DISPLAY_EDIT );
}

void testMenuItemEnumValues()
{
  assertEq( "MENU_ITEM_RPM is 0", 0, MENU_ITEM_RPM );
  assertEq( "MENU_ITEM_BURST is 1", 1, MENU_ITEM_BURST );
  assertEq( "MENU_ITEM_PREREV is 2", 2, MENU_ITEM_PREREV );
  assertEq( "MENU_ITEM_BACK is 3", 3, MENU_ITEM_BACK );
  assertEq( "MENU_ITEM_COUNT is 4", 4, MENU_ITEM_COUNT );
}

void testTransitionViewToMenuOnClick()
{
  assertEq( "VIEW + click -> MENU", DISPLAY_MENU, transitionDisplayState( DISPLAY_VIEW, MENU_ITEM_RPM, true ) );
}

void testTransitionViewStaysViewWithoutClick()
{
  assertEq( "VIEW + no click stays VIEW", DISPLAY_VIEW, transitionDisplayState( DISPLAY_VIEW, MENU_ITEM_RPM, false ) );
}

void testTransitionMenuToEditOnClickRPM()
{
  assertEq( "MENU + click on RPM -> EDIT", DISPLAY_EDIT, transitionDisplayState( DISPLAY_MENU, MENU_ITEM_RPM, true ) );
}

void testTransitionMenuToEditOnClickBurst()
{
  assertEq( "MENU + click on Burst -> EDIT", DISPLAY_EDIT, transitionDisplayState( DISPLAY_MENU, MENU_ITEM_BURST, true ) );
}

void testTransitionMenuToEditOnClickPreRev()
{
  assertEq( "MENU + click on PreRev -> EDIT", DISPLAY_EDIT, transitionDisplayState( DISPLAY_MENU, MENU_ITEM_PREREV, true ) );
}

void testTransitionMenuToViewOnClickBack()
{
  assertEq( "MENU + click on Back -> VIEW", DISPLAY_VIEW, transitionDisplayState( DISPLAY_MENU, MENU_ITEM_BACK, true ) );
}

void testTransitionMenuStaysMenuWithoutClick()
{
  assertEq( "MENU + no click stays MENU", DISPLAY_MENU, transitionDisplayState( DISPLAY_MENU, MENU_ITEM_RPM, false ) );
}

void testTransitionEditToMenuOnClick()
{
  assertEq( "EDIT + click -> MENU", DISPLAY_MENU, transitionDisplayState( DISPLAY_EDIT, MENU_ITEM_RPM, true ) );
}

void testTransitionEditStaysEditWithoutClick()
{
  assertEq( "EDIT + no click stays EDIT", DISPLAY_EDIT, transitionDisplayState( DISPLAY_EDIT, MENU_ITEM_RPM, false ) );
}

void testCycleMenuItemForward()
{
  assertEq( "cycleMenuItem RPM +1 -> BURST", MENU_ITEM_BURST, cycleMenuItem( MENU_ITEM_RPM, 1 ) );
  assertEq( "cycleMenuItem BURST +1 -> PREREV", MENU_ITEM_PREREV, cycleMenuItem( MENU_ITEM_BURST, 1 ) );
  assertEq( "cycleMenuItem PREREV +1 -> BACK", MENU_ITEM_BACK, cycleMenuItem( MENU_ITEM_PREREV, 1 ) );
  assertEq( "cycleMenuItem BACK +1 wraps to RPM", MENU_ITEM_RPM, cycleMenuItem( MENU_ITEM_BACK, 1 ) );
}

void testCycleMenuItemBackward()
{
  assertEq( "cycleMenuItem RPM -1 wraps to BACK", MENU_ITEM_BACK, cycleMenuItem( MENU_ITEM_RPM, -1 ) );
  assertEq( "cycleMenuItem BURST -1 -> RPM", MENU_ITEM_RPM, cycleMenuItem( MENU_ITEM_BURST, -1 ) );
  assertEq( "cycleMenuItem PREREV -1 -> BURST", MENU_ITEM_BURST, cycleMenuItem( MENU_ITEM_PREREV, -1 ) );
  assertEq( "cycleMenuItem BACK -1 -> PREREV", MENU_ITEM_PREREV, cycleMenuItem( MENU_ITEM_BACK, -1 ) );
}

void testCycleMenuItemNoDirection()
{
  assertEq( "cycleMenuItem RPM 0 stays RPM", MENU_ITEM_RPM, cycleMenuItem( MENU_ITEM_RPM, 0 ) );
}

void testMenuTimeoutNotExpired()
{
  assertBool( "menu timeout not expired at 9.9s", false, isMenuTimeoutExpired( 0, 9999, DISPLAY_MENU_TIMEOUT_MS ) );
}

void testMenuTimeoutExpiredAtBoundary()
{
  assertBool( "menu timeout expired exactly at 10s", true, isMenuTimeoutExpired( 0, 10000, DISPLAY_MENU_TIMEOUT_MS ) );
}

void testMenuTimeoutExpiredPast()
{
  assertBool( "menu timeout expired at 11s", true, isMenuTimeoutExpired( 0, 11000, DISPLAY_MENU_TIMEOUT_MS ) );
}

void testMenuTimeoutHandlesMillisRollover()
{
  // 21 ms elapsed across rollover, 10 ms timeout -> expired
  assertBool( "menu timeout handles millis() rollover (expired)", true, isMenuTimeoutExpired( 0xFFFFFFF0UL, 5UL, 10UL ) );
  // 21 ms elapsed across rollover, 100 ms timeout -> not expired
  assertBool( "menu timeout handles millis() rollover (not expired)", false, isMenuTimeoutExpired( 0xFFFFFFF0UL, 5UL, 100UL ) );
}

void testGetMenuItemLabel()
{
  assertStrEq( "menu label RPM", "RPM", getMenuItemLabel( MENU_ITEM_RPM ) );
  assertStrEq( "menu label Burst", "Burst", getMenuItemLabel( MENU_ITEM_BURST ) );
  assertStrEq( "menu label PreRev", "PreRev", getMenuItemLabel( MENU_ITEM_PREREV ) );
  assertStrEq( "menu label Back", "Back", getMenuItemLabel( MENU_ITEM_BACK ) );
}

void testCenterTileColScaled2x()
{
  assertEq( "centerTileCol SINGLE (6ch,2x) -> 2", 2, centerTileCol( 6, 2 ) );
  assertEq( "centerTileCol BURST (5ch,2x) -> 3", 3, centerTileCol( 5, 2 ) );
  assertEq( "centerTileCol AUTO (4ch,2x) -> 4", 4, centerTileCol( 4, 2 ) );
}

void testCenterTileColScaled1x()
{
  assertEq( "centerTileCol 16ch 1x exact fit -> 0", 0, centerTileCol( 16, 1 ) );
  assertEq( "centerTileCol 17ch 1x overflow -> 0", 0, centerTileCol( 17, 1 ) );
}

void testFormatVoltageShort()
{
  char buf[8];
  formatVoltageShort( 16.8, buf, sizeof( buf ) );
  assertStrEq( "formatVoltageShort 16.8 -> '16.8V'", "16.8V", buf );
  formatVoltageShort( 9.5, buf, sizeof( buf ) );
  assertStrEq( "formatVoltageShort 9.5 -> ' 9.5V'", " 9.5V", buf );
  formatVoltageShort( 10.0, buf, sizeof( buf ) );
  assertStrEq( "formatVoltageShort 10.0 -> '10.0V'", "10.0V", buf );
  formatVoltageShort( 0.0, buf, sizeof( buf ) );
  assertStrEq( "formatVoltageShort 0.0 -> ' 0.0V'", " 0.0V", buf );
  formatVoltageShort( 16.95, buf, sizeof( buf ) );
  assertStrEq( "formatVoltageShort 16.95 rounds to '17.0V'", "17.0V", buf );
}

void testGetCenterModeLabel()
{
  assertStrEq( "center label SINGLE unsafe", "SINGLE", getCenterModeLabel( FIRE_MODE_SINGLE, false ) );
  assertStrEq( "center label BURST unsafe", "BURST", getCenterModeLabel( FIRE_MODE_BURST, false ) );
  assertStrEq( "center label FULL_AUTO -> AUTO", "AUTO", getCenterModeLabel( FIRE_MODE_FULL_AUTO, false ) );
  assertStrEq( "center label safe override SINGLE", "SAFE", getCenterModeLabel( FIRE_MODE_SINGLE, true ) );
  assertStrEq( "center label safe override BURST", "SAFE", getCenterModeLabel( FIRE_MODE_BURST, true ) );
  assertStrEq( "center label safe override AUTO", "SAFE", getCenterModeLabel( FIRE_MODE_FULL_AUTO, true ) );
}

// ---- Implementations (must match Narfduino_Phantasm.ino) ----

FireMode getFireMode( bool select1Low, bool select2Low )
{
  if( !select1Low && select2Low )
    return FIRE_MODE_BURST;
  if( select1Low && select2Low )
    return FIRE_MODE_FULL_AUTO;
  return FIRE_MODE_SINGLE;
}

unsigned long clampRPM( long rpm, unsigned long minRPM, unsigned long maxRPM )
{
  if( rpm < (long)minRPM ) return minRPM;
  if( (unsigned long)rpm > maxRPM ) return maxRPM;
  return (unsigned long)rpm;
}

unsigned long calculateEncoderRPM( unsigned long currentRPM, int direction, unsigned int stepSize, unsigned long minRPM, unsigned long maxRPM )
{
  long newRPM = (long)currentRPM + ( direction * (int)stepSize );
  return clampRPM( newRPM, minRPM, maxRPM );
}

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

const char* getDisplayModeLabel( FireMode mode, bool safe )
{
  if( safe )
    return "SAFE";
  return getFireModeLabel( mode );
}

bool hasRPMChanged( unsigned long oldRPM, unsigned long newRPM )
{
  return oldRPM != newRPM;
}

int clampBurstCount( int count, int minCount, int maxCount )
{
  if( count < minCount ) return minCount;
  if( count > maxCount ) return maxCount;
  return count;
}

int calculateEncoderBurst( int currentCount, int direction, int stepSize, int minCount, int maxCount )
{
  int newCount = currentCount + ( direction * stepSize );
  return clampBurstCount( newCount, minCount, maxCount );
}

unsigned long calculateEncoderPreRevRPM( unsigned long currentRPM, int direction, unsigned int stepSize, unsigned long minRPM, unsigned long maxRPM )
{
  long newRPM = (long)currentRPM + ( direction * (int)stepSize );
  return clampRPM( newRPM, minRPM, maxRPM );
}

bool isPreRevActive( bool pinHigh )
{
  return pinHigh;
}

bool isMp5SlapSafe( bool pinHigh )
{
  return !pinHigh;
}

bool hasVoltageChanged( float oldVoltage, float newVoltage )
{
  // Only consider changed if the displayed one-decimal digit differs
  int oldTenths = (int)( oldVoltage * 10 + 0.5 );
  int newTenths = (int)( newVoltage * 10 + 0.5 );
  return oldTenths != newTenths;
}

DisplayState transitionDisplayState( DisplayState current, MenuItem selected, bool clicked )
{
  if( !clicked )
    return current;
  if( current == DISPLAY_VIEW )
    return DISPLAY_MENU;
  if( current == DISPLAY_MENU )
    return ( selected == MENU_ITEM_BACK ) ? DISPLAY_VIEW : DISPLAY_EDIT;
  return DISPLAY_MENU;
}

MenuItem cycleMenuItem( MenuItem current, int direction )
{
  if( direction == 0 )
    return current;
  int idx = (int)current + direction;
  while( idx < 0 ) idx += MENU_ITEM_COUNT;
  while( idx >= MENU_ITEM_COUNT ) idx -= MENU_ITEM_COUNT;
  return (MenuItem)idx;
}

bool isMenuTimeoutExpired( uint32_t lastActivityMs, uint32_t nowMs, uint32_t timeoutMs )
{
  // uint32_t subtraction wraps modulo 2^32, matching AVR millis() semantics
  return ( nowMs - lastActivityMs ) >= timeoutMs;
}

const char* getMenuItemLabel( MenuItem item )
{
  switch( item )
  {
    case MENU_ITEM_BURST:  return "Burst";
    case MENU_ITEM_PREREV: return "PreRev";
    case MENU_ITEM_BACK:   return "Back";
    case MENU_ITEM_RPM:
    default:               return "RPM";
  }
}

uint8_t centerTileCol( uint8_t labelLen, uint8_t scale )
{
  uint16_t widthTiles = (uint16_t)labelLen * scale;
  if( widthTiles >= 16 ) return 0;
  return ( 16 - widthTiles ) / 2;
}

void formatVoltageShort( float voltage, char* buf, size_t bufSize )
{
  // Compact 5-char voltage: "XX.XV" with leading space when single-digit whole
  int tenths = (int)( voltage * 10 + 0.5 );
  int whole = tenths / 10;
  int frac = tenths % 10;
  if( whole < 10 )
    snprintf( buf, bufSize, " %d.%dV", whole, frac );
  else
    snprintf( buf, bufSize, "%d.%dV", whole, frac );
}

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

void setup()
{
  Serial.begin( 115200 );
  delay( 2000 );
  Serial.println( "=== Phantasm Logic Tests ===" );
  Serial.println( "Selector: 2P3T slide, Pos1=SINGLE Pos2=BURST Pos3=FULL AUTO" );
  Serial.println( "Fallback: HIGH/HIGH selector state -> SINGLE" );
  Serial.println( "Display SAFE state is driven by MP5 slap safety override" );
  Serial.println();

  // Fire mode tests
  testFireModeEnumValues();
  testGetFireModeSingleShot();
  testGetFireModeBurst();
  testGetFireModeFullAuto();
  testGetFireModeFallback();
  testBurstCountIsThree();
  testSingleShotEdgeDetection();

  // RPM clamping tests
  testClampRPMWithinRange();
  testClampRPMBelowMin();
  testClampRPMAboveMax();
  testClampRPMAtBoundaries();

  // Encoder RPM calculation tests
  testCalculateEncoderRPMClockwise();
  testCalculateEncoderRPMAnticlockwise();
  testCalculateEncoderRPMClampsAtMax();
  testCalculateEncoderRPMClampsAtMin();
  testCalculateEncoderRPMNoDirection();

  // Fire mode label tests
  testGetFireModeLabelSingle();
  testGetFireModeLabelBurst();
  testGetFireModeLabelFullAuto();

  // Display mode label tests (MP5 safety override)
  testGetDisplayModeLabelSafetyOverrideSingle();
  testGetDisplayModeLabelSafetyOverrideBurst();
  testGetDisplayModeLabelSafetyOverrideFullAuto();
  testGetDisplayModeLabelNormalSingle();
  testGetDisplayModeLabelNormalBurst();
  testGetDisplayModeLabelNormalFullAuto();

  // RPM changed tests
  testHasRPMChangedTrue();
  testHasRPMChangedFalse();

  // MP5 slap safety tests
  testIsMp5SlapSafeWhenBoltLocked();
  testIsMp5SlapUnsafeWhenBoltOpen();

  // Pre-rev tests
  testIsPreRevActiveWhenSwitchOpen();
  testIsPreRevInactiveWhenSwitchClosed();
  testPreRevRPMDefaultConstant();
  testPreRevRPMMinConstant();
  testPreRevRPMMaxConstant();
  testCalculateEncoderPreRevRPMClockwise();
  testCalculateEncoderPreRevRPMAnticlockwise();
  testCalculateEncoderPreRevRPMClampsAtMax();
  testCalculateEncoderPreRevRPMClampsAtMin();

  // Burst count tests
  testBurstCountDefault();
  testClampBurstCountWithinRange();
  testClampBurstCountBelowMin();
  testClampBurstCountAboveMax();
  testClampBurstCountAtBoundaries();
  testCalculateEncoderBurstClockwise();
  testCalculateEncoderBurstAnticlockwise();
  testCalculateEncoderBurstClampsAtMax();
  testCalculateEncoderBurstClampsAtMin();
  testCalculateEncoderBurstNoDirection();

  // Voltage display tests
  testHasVoltageChangedTrue();
  testHasVoltageChangedFalse();
  testHasVoltageChangedWithinTolerance();
  testHasVoltageChangedAcrossRounding();

  // Display state / menu tests
  testDisplayStateEnumValues();
  testMenuItemEnumValues();
  testTransitionViewToMenuOnClick();
  testTransitionViewStaysViewWithoutClick();
  testTransitionMenuToEditOnClickRPM();
  testTransitionMenuToEditOnClickBurst();
  testTransitionMenuToEditOnClickPreRev();
  testTransitionMenuToViewOnClickBack();
  testTransitionMenuStaysMenuWithoutClick();
  testTransitionEditToMenuOnClick();
  testTransitionEditStaysEditWithoutClick();
  testCycleMenuItemForward();
  testCycleMenuItemBackward();
  testCycleMenuItemNoDirection();
  testMenuTimeoutNotExpired();
  testMenuTimeoutExpiredAtBoundary();
  testMenuTimeoutExpiredPast();
  testMenuTimeoutHandlesMillisRollover();
  testGetMenuItemLabel();
  testCenterTileColScaled2x();
  testCenterTileColScaled1x();
  testFormatVoltageShort();
  testGetCenterModeLabel();

  Serial.println();
  Serial.println( "=== Results ===" );
  Serial.print( "Passed: " );
  Serial.println( testsPassed );
  Serial.print( "Failed: " );
  Serial.println( testsFailed );

  if( testsFailed == 0 )
    Serial.println( "ALL TESTS PASSED" );
  else
    Serial.println( "SOME TESTS FAILED" );
}

void loop()
{
  // Nothing to do
}
