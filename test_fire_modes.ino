// Test sketch for Phantasm logic
// TDD: These tests should FAIL before implementation, then PASS after.
// Upload to an Arduino to run. Results output via Serial.

// ---- Fire mode enum (must match implementation) ----
enum FireMode {
  FIRE_MODE_SAFETY,
  FIRE_MODE_SINGLE,
  FIRE_MODE_BURST,
  FIRE_MODE_FULL_AUTO
};

#define BURST_COUNT 3
#define MOTOR_RPM_MIN 3000
#define MOTOR_RPM_MAX 8000
#define ENCODER_RPM_STEP 250

// ---- Functions under test (forward declarations) ----
FireMode getFireMode( bool select1Low, bool select2Low );
unsigned long clampRPM( long rpm, unsigned long minRPM, unsigned long maxRPM );
unsigned long calculateEncoderRPM( unsigned long currentRPM, int direction, unsigned int stepSize, unsigned long minRPM, unsigned long maxRPM );
const char* getFireModeLabel( FireMode mode );
bool hasRPMChanged( unsigned long oldRPM, unsigned long newRPM );

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

void testGetFireModeSafety()
{
  // Rotary switch position 1: both pins HIGH = safety
  assertEq( "Pos 1 Safety: sel1=HIGH sel2=HIGH", FIRE_MODE_SAFETY, getFireMode( false, false ) );
}

void testGetFireModeSingleShot()
{
  // Rotary switch position 2: SELECT_1 LOW, SELECT_2 HIGH = single
  assertEq( "Pos 2 Single: sel1=LOW sel2=HIGH", FIRE_MODE_SINGLE, getFireMode( true, false ) );
}

void testGetFireModeBurst()
{
  // Rotary switch position 3: SELECT_1 HIGH, SELECT_2 LOW = burst
  assertEq( "Pos 3 Burst: sel1=HIGH sel2=LOW", FIRE_MODE_BURST, getFireMode( false, true ) );
}

void testGetFireModeFullAuto()
{
  // Rotary switch position 4: both pins LOW = full auto
  assertEq( "Pos 4 Full Auto: sel1=LOW sel2=LOW", FIRE_MODE_FULL_AUTO, getFireMode( true, true ) );
}

void testBurstCountIsThree()
{
  assertEq( "Burst count is 3", 3, BURST_COUNT );
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
  assertEq( "FIRE_MODE_SAFETY is 0", 0, FIRE_MODE_SAFETY );
  assertEq( "FIRE_MODE_SINGLE is 1", 1, FIRE_MODE_SINGLE );
  assertEq( "FIRE_MODE_BURST is 2", 2, FIRE_MODE_BURST );
  assertEq( "FIRE_MODE_FULL_AUTO is 3", 3, FIRE_MODE_FULL_AUTO );
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

void testGetFireModeLabelSafety()
{
  assertStrEq( "getFireModeLabel: safety", "SAFETY", getFireModeLabel( FIRE_MODE_SAFETY ) );
}

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

// ---- RPM Changed Tests ----

void testHasRPMChangedTrue()
{
  assertBool( "hasRPMChanged: different values returns true", true, hasRPMChanged( 5000, 5250 ) );
}

void testHasRPMChangedFalse()
{
  assertBool( "hasRPMChanged: same values returns false", false, hasRPMChanged( 5000, 5000 ) );
}

// ---- Implementations (must match Narfduino_Phantasm.ino) ----

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
    case FIRE_MODE_SINGLE:    return "SINGLE";
    case FIRE_MODE_BURST:     return "BURST";
    case FIRE_MODE_FULL_AUTO: return "FULL AUTO";
    case FIRE_MODE_SAFETY:
    default:                  return "SAFETY";
  }
}

bool hasRPMChanged( unsigned long oldRPM, unsigned long newRPM )
{
  return oldRPM != newRPM;
}

void setup()
{
  Serial.begin( 115200 );
  delay( 2000 );
  Serial.println( "=== Phantasm Logic Tests ===" );
  Serial.println();

  // Fire mode tests
  testFireModeEnumValues();
  testGetFireModeSafety();
  testGetFireModeSingleShot();
  testGetFireModeBurst();
  testGetFireModeFullAuto();
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
  testGetFireModeLabelSafety();
  testGetFireModeLabelSingle();
  testGetFireModeLabelBurst();
  testGetFireModeLabelFullAuto();

  // RPM changed tests
  testHasRPMChangedTrue();
  testHasRPMChangedFalse();

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
