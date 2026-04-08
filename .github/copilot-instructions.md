<!-- Use this file to provide workspace-specific custom instructions to Copilot. For more details, visit https://code.visualstudio.com/docs/copilot/copilot-customization#_use-a-githubcopilotinstructionsmd-file -->

# Tabquarium - ESP-IDF Aquarium Game Project

This is an ESP-IDF project for a simple aquarium game running on an ESP32 with an SSD1306 OLED display (128x64 monochrome).

## Project Structure
- `main/` - Main application code with aquarium game logic
- `main/aquarium.c` - Fish, food, and game state management
- `main/display.c` - SSD1306 display driver and rendering
- `main/input.c` - Button input handling
- `CMakeLists.txt` - ESP-IDF project configuration

## Building and Flashing
- Build: Use ESP-IDF VS Code extension or `idf.py build`
- Flash: Use ESP-IDF VS Code extension or `idf.py flash`
- Monitor: Use ESP-IDF VS Code extension or `idf.py monitor`

## Hardware Requirements
- ESP32 development board
- SSD1306 OLED display (128x64) via I2C
- Buttons for game input (GPIO pins configurable)
