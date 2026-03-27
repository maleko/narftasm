# Phantasm — Brushless Foam Dart Blaster

Custom firmware for a foam dart blaster built on the
[Narfduino Brushless Compleat (NBC)](https://blastersbyairzone.com/narfduino/) board.

## Features

- **Select Fire** — 4-position rotary switch (2P4T) with sequential modes:
  1. Safety (flywheels off, firing disabled)
  2. Single shot (one dart per trigger pull)
  3. 3-round burst (three darts per trigger pull)
  4. Full auto (continuous fire while trigger held)
- **Variable RPM** — KY-040 rotary encoder adjusts flywheel speed
  (3000–8000 RPM in 250 RPM steps)
- **OLED Display** — 0.96" I2C SSD1306 shows current fire mode and RPM
- **Battery Protection** — disables operation below minimum voltage (12V for 4S LiPo)

## Hardware

| Component | Pins |
|---|---|
| Trigger microswitch | D6 |
| 4-position 2P4T rotary switch | D2, D3 |
| KY-040 rotary encoder | D4 (CLK), D11 (DT), D12 (SW) |
| I2C SSD1306 OLED (5V tolerant) | A4 (SDA), A5 (SCL) |

See [Phantasm-Wiring-Diagram.md](Phantasm-Wiring-Diagram.md) for full wiring
details, truth tables, and terminal-level connection instructions.

## Dependencies

- **NBC.h** — Narfduino Brushless Compleat library (included)
- **U8g2** — Install via Arduino IDE: *Sketch → Include Library → Manage
  Libraries → search "U8g2"*

## Files

| File | Purpose |
|---|---|
| `Narfduino_Phantasm.ino` | Main sketch |
| `test_fire_modes.ino` | Test sketch (upload to Arduino, check Serial output) |
| `NBC.h` | NBC board library |
| `Phantasm-Wiring-Diagram.md` | Wiring diagram and truth tables |
| `NBC-Pinout.png` | Board pinout reference |
