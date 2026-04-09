# Tabquarium - ESP32-C3 Aquarium Game

Tabquarium is an ESP-IDF game for an SSD1306 128x64 OLED aquarium with fish collecting, decorations, blindbox rewards, persistent save data, and sleep-aware simulation.

## Current Platform

- Target MCU: ESP32-C3
- Framework: ESP-IDF v5.4.x
- Display: SSD1306 (I2C, monochrome 128x64)

## Hardware Wiring (ESP32-C3)

### OLED (SSD1306, I2C)

- VCC -> 3.3V
- GND -> GND
- SDA -> GPIO 21
- SCL -> GPIO 22

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
- Decor ownership tracking separate from active visible decor slots
- Tank-wide hunger system (single tank timer instead of per-fish hunger)
- Starvation system:
  - First random fish death when hunger reaches zero
  - Then one random fish death every 24h while unfed
- Wake/offline progression using saved unix timestamps
- Startup overlays:
  - Save corruption warning
  - Offline death summary
  - Dismissed by button press
- Save schema recovery:
  - Legacy fish blob migration path before corruption fallback
- Fish behaviors:
  - Food targeting with slight randomness
  - Species movement variants (including snail/crab behavior)
- Food system:
  - 2-3 food pieces per feed action
  - Top spawn, lifetime/consume timers, and touch/consume logic
- UI/sleep behavior:
  - Input lockout after wake to avoid accidental presses
  - Clock-set screen blocks idle sleep
- LED behavior:
  - Blinks when tank reaches half-hunger threshold
  - 2-second solid pulse when a tab reward is earned
- Rendering polish:
  - Multiple fish and decor sprite spacing/alignment tweaks
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
- Build outputs under `build/` are generated artifacts and not hand-edited.
