# Tabquarium - ESP32-C3 Aquarium Game

Tabquarium is an ESP-IDF aquarium game for an SSD1306 128x64 OLED with fish collecting, decorations, blindbox rewards, persistent save data, weather effects, and sleep-aware simulation.

## Current Platform

- Target MCU: ESP32-C3
- Framework: ESP-IDF v5.4.x
- Display: SSD1306 (I2C, monochrome 128x64)

## Hardware Wiring (ESP32-C3)

### OLED (SSD1306, I2C)

- VCC -> 3.3V
- GND -> GND
- SDA -> GPIO 5
- SCL -> GPIO 6

### Buttons

- Mode button -> GPIO 21
- Action button -> GPIO 20
- Tabs/Reward button -> GPIO 7

### LED

- Hunger/alert LED -> GPIO 10

## Game Controls

- `Mode`: cycles main screens (Blindbox -> Aquarium -> Fish -> Decorations)
- `Action` on Blindbox: shake/open rewards
- `Action` in Aquarium: feed fish (drops random top-spawn food pieces)
- `Tabs` on Blindbox: earn +1 tab with cooldown
- Clock set mode:
  - Enter/exit with mode+action hold combo
  - Action adjusts hour
  - Mode adjusts minute

## Core Features

- Blindbox rewards with 80% fish / 20% decoration odds
- Persistent fish, decorations, tabs, clock, and tank hunger state via NVS
- Persistent hourly weather seed via NVS
- Decor ownership tracking separate from active visible decor slots
- Tank-wide hunger system (single tank timer instead of per-fish hunger)
- Starvation system:
  - First random fish death when hunger reaches zero
  - Then one random fish death every 24h while unfed
- Wake/offline progression using saved unix timestamps
- Clock restore on boot from saved unix time
- Startup overlays:
  - Save corruption warning
  - Offline death summary
  - Dismissed by button press
- Save schema recovery:
  - Legacy fish blob migration path before corruption fallback
- Fish behaviors:
  - Committed food targeting so a fish chases its chosen pellet instead of constantly retargeting
  - Some fish intentionally prefer the second- or third-closest pellet when multiple food pieces exist
  - Species movement variants (including snail/crab behavior)
  - Nighttime sleep behavior with floating sleep `Z` particles
- Food system:
  - 2-3 food pieces per feed action
  - Top spawn, lifetime/consume timers, and touch/consume logic
  - Pellets keep falling even when untouched and expire after 10 seconds
- Weather system:
  - Hour-long weather seed with chunked rain and sun-ray windows
  - Rain occupies 15% of seeded time
  - Sun rays occupy 30% of non-rain seeded time
  - Rain suppresses sun rays while active
  - Thunderstorms can occur during rain and flash the composed frame
- UI/sleep behavior:
  - Input lockout after wake to avoid accidental presses
  - Clock-set screen blocks idle sleep
  - Changing the clock invalidates the current weather seed so a new hour schedule is generated
- LED behavior:
  - Blinks when tank reaches half-hunger threshold
  - 2-second solid pulse when a tab reward is earned
- Rendering polish:
  - Multiple fish and decor sprite spacing/alignment tweaks
  - Dotted sunbeams that keep a stable origin while active
  - Rain droplets that become expanding surface ripples
  - Clock-set display position adjustments
  - "Tabquarium" branding text in UI
  - Decor label update: "Oyster Pearl"

## Build and Flash

### Prerequisites

- ESP-IDF installed and exported in shell
- `idf.py` available

### Build

```bash
idf.py build
```

### Flash

```bash
idf.py flash
```

### Monitor

```bash
idf.py monitor
```

### Full Cycle

```bash
idf.py build flash monitor
```

## Project Layout

```text
.
├── CMakeLists.txt
├── sdkconfig
├── main/
│   ├── aquarium.c
│   ├── aquarium.h
│   ├── display.c
│   ├── gifts.c
│   ├── input.c
│   └── ui.c
└── .github/
    └── copilot-instructions.md
```

## Notes

- Frame cadence targets ~60 FPS (`vTaskDelay(17ms)` cadence in UI loop).
- Current dev toggles for weather testing live near the top of [`main/aquarium.c`](main/aquarium.c):
  - `always_raining`
  - `always_thunder`
