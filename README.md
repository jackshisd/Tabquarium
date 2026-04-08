# Tabquarium - ESP32 Aquarium Game

A simple aquarium game running on ESP32 with SSD1306 OLED display (128x64 monochrome).

## Hardware Setup

### Components
- ESP32 Development Board
- SSD1306 OLED Display (128x64) via I2C
- 2x Pushbuttons for game input

### Wiring
- **SSD1306 Display (I2C)**
  - VCC → 3.3V
  - GND → GND
  - SDA → GPIO 21
  - SCL → GPIO 22

- **Buttons**
  - Feed Button → GPIO 32 (press to drop food)
  - Clean Button → GPIO 33 (press to view stats)

## Game Features

- Multiple fish swimming in the tank
- Feed fish by pressing the FEED button
- Fish have hunger and happiness stats
- Score system (10 points per food eaten)
- Simple monochrome graphics
- Real-time game stats

## Building & Flashing

### Prerequisites
- ESP-IDF installed and configured
- `idf.py` available in PATH

### Build
```bash
idf.py build
```

### Flash (UART)
```bash
idf.py flash
```

### Monitor Serial Output
```bash
idf.py monitor
```

### Full Cycle
```bash
idf.py build flash monitor
```

## Project Structure

```
.
├── CMakeLists.txt          # Project-level CMake config
├── sdkconfig               # ESP-IDF configuration
├── main/
│   ├── CMakeLists.txt      # Component CMake config
│   ├── aquarium.c          # Game logic (fish, food, scoring)
│   ├── aquarium.h          # Game structs and declarations
│   ├── display.c           # SSD1306 driver and rendering
│   ├── input.c             # Button input handling
│   └── ui.c                # Main game loop and app entry
└── .github/
    └── copilot-instructions.md  # Project documentation for Copilot
```

## Game Controls

| Button | Action |
|--------|--------|
| FEED (GPIO 32) | Drop food in the tank |
| CLEAN (GPIO 33) | Display game stats in serial |

## Development Notes

### Frame Rate
- Target: ~60 FPS (17ms per frame)
- Adjustable via delay in `ui.c`

### Display
- 128x64 pixel monochrome display
- Vertical pages (8 pixels per row)
- I2C communication at 100 kHz

### Fish Behavior
- Random horizontal movement with wrapping
- Slight vertical drift (bobbing effect)
- Hunger decreases over time
- Fish dies if hunger reaches 0
- Fish gain happiness and reset hunger when eating

### Scalability
To expand the game:
- Add shop menu for buying more fish
- Implement save/load game state
- Add more fish behaviors (breeding, sleeping)
- Implement power-ups or bonus items
- Add sound effects via piezo buzzer
