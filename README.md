# Retro Arcade Clock

Standalone Arduino sketch for an ESP32 + ILI9341 build that renders a Space Invaders-inspired clock face.

## Project Writeup

Full build notes and photos are on Hackster:

- [Retro Arcade Clock on a CYD](https://www.hackster.io/bwente/retro-arcade-clock-on-a-cyd-591fc1)

## Files

- `Retro_Arcade_Clock/Retro_Arcade_Clock.ino`: main sketch
- `Retro_Arcade_Clock/secrets.example.h`: copy this to `secrets.h` for local Wi-Fi credentials

## Dependencies

- `Adafruit GFX Library`
- `Adafruit ILI9341`
- ESP32 Arduino core with `WiFi.h`

## Setup

1. Copy `Retro_Arcade_Clock/secrets.example.h` to `Retro_Arcade_Clock/secrets.h`.
2. Add your Wi-Fi name and password to `Retro_Arcade_Clock/secrets.h`.
3. Open `Retro_Arcade_Clock/Retro_Arcade_Clock.ino` in the Arduino IDE.
4. Install the required display libraries if they are not already present.
5. Select your ESP32 board and upload.

`Retro_Arcade_Clock/secrets.h` is git-ignored so your real credentials stay local.
