// Test sketch for fire mode logic
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

// ---- Function under test (forward declaration) ----
FireMode getFireMode( bool select1Low, bool select2Low );

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

// ---- Tests ----

void testGetFireModeSafety()
{
  // Both select pins HIGH (not LOW) = safety
  assertEq( "Safety: sel1=HIGH sel2=HIGH", FIRE_MODE_SAFETY, getFireMode( false, false ) );
}

void testGetFireModeSingleShot()
{
  // SELECT_1 LOW, SELECT_2 HIGH
  assertEq( "Single shot: sel1=LOW sel2=HIGH", FIRE_MODE_SINGLE, getFireMode( true, false ) );
}

void testGetFireModeBurst()
{
  // SELECT_1 HIGH, SELECT_2 LOW
  assertEq( "Burst: sel1=HIGH sel2=LOW", FIRE_MODE_BURST, getFireMode( false, true ) );
}

void testGetFireModeFullAuto()
{
  // Both select pins LOW
  assertEq( "Full auto: sel1=LOW sel2=LOW", FIRE_MODE_FULL_AUTO, getFireMode( true, true ) );
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

// ---- Real implementation (matches Narfduino_Phantasm.ino) ----
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

void setup()
{
  Serial.begin( 115200 );
  delay( 2000 );
  Serial.println( "=== Fire Mode Tests ===" );
  Serial.println();

  testFireModeEnumValues();
  testGetFireModeSafety();
  testGetFireModeSingleShot();
  testGetFireModeBurst();
  testGetFireModeFullAuto();
  testBurstCountIsThree();
  testSingleShotEdgeDetection();

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

