# Irrigation-System
A project for controlling irrigation valves using Nodemcu with an ESP8266 chipset

Hi all, for a while I was annoyed by the fact that I need to manualy adjust my watering system controller during each and every season. Adjusting the iirigation system required going out to the garden, opening the watertight casing and going through the long menu list to reschedule the watering times and durations. One day I decided, to build my own remote controled irrigation controller that will replace the curent controller.
The project originally used the Blynk platform; since the legacy Blynk cloud was shut down it uses MQTT with a self-hosted broker and web dashboard (see `server/`).

## Building

### PlatformIO (NodeMCU v3)

1. Install the **PlatformIO IDE** extension in VS Code and open this folder.
2. Copy `Watering_System/secrets.h.example` to `Watering_System/secrets.h` and fill in your WiFi and MQTT credentials.
3. Plug in the NodeMCU v3 (install the CH340 USB driver if no COM port appears).
4. Run **Upload**, then **Monitor** (9600 baud). Libraries are pulled automatically from `platformio.ini`.

The broker and dashboard setup is described in [server/README.md](server/README.md).

If uploads fail, set `upload_speed = 115200` (or `upload_port`) in `platformio.ini`.

### Arduino IDE

1. Open `Watering_System/Watering_System.ino` in the Arduino IDE (ESP8266 board core).
2. Copy `Watering_System/secrets.h.example` to `Watering_System/secrets.h` and fill in your WiFi and MQTT credentials. `secrets.h` is git-ignored — never commit it.
3. Required libraries: PubSubClient, ArduinoJson (v7), Adafruit GFX, Adafruit SSD1306.

Older sketch versions (v4, v8, v9) are available in the git history.
