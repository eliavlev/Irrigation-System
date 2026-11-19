# Irrigation-System
A project for controlling irrigation valves using Nodemcu with an ESP8266 chipset

Hi all, for a while I was annoyed by the fact that I need to manualy adjust my watering system controller during each and every season. Adjusting the iirigation system required going out to the garden, opening the watertight casing and going through the long menu list to reschedule the watering times and durations. One day I decided, to build my own remote controled irrigation controller that will replace the curent controller.
The project uses the Blynk platform and a Nomemcu controller

## Building

1. Open `Watering_System/Watering_System.ino` in the Arduino IDE (ESP8266 board core).
2. Copy `Watering_System/secrets.h.example` to `Watering_System/secrets.h` and fill in your Blynk auth token and WiFi credentials. `secrets.h` is git-ignored — never commit it.
3. Required libraries: Blynk (legacy), TimeLib, Adafruit GFX, Adafruit SSD1306, SimpleTimer.

Older sketch versions (v4, v8, v9) are available in the git history.
