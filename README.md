# ESP-12 D1 Mini Weather Clock

A WiFi-connected desk clock built on an ESP-12 D1 Mini V2 and a 4-in-1 MAX7219 dot matrix display. It alternates between a live 12-hour clock (synced via NTP) and the current local weather — condition icon plus temperature — pulled live from the internet, no hardcoded data.

## Features


https://github.com/user-attachments/assets/03427684-f301-4356-8273-96b427219012




- 🕐 **Live clock** — 12-hour format, centered, with a blinking colon, synced over WiFi via NTP (no RTC module needed)
- 🌦️ **Live weather** — current temperature and condition (sunny, cloudy, rainy, stormy, snowy, foggy) for your exact GPS coordinates, fetched from [Open-Meteo](https://open-meteo.com) — completely free, no API key required
- 🎨 **Custom pixel icons** — hand-designed 16×8 weather icons, rendered as raw pixels (no external icon libraries)
- 🔁 **Auto-cycling display** — clock shown for 10 seconds, weather shown for 5 seconds, repeating forever
- ⚡ **Flicker-free rendering** — the display only redraws when the underlying value actually changes, not on a fixed timer

## Hardware Required

| Component | Notes |
|---|---|
| ESP-12 D1 Mini V2 | Any ESP8266-based D1 Mini board works |
| 4-in-1 MAX7219 dot matrix module | 4× cascaded 8×8 LED matrices (32×8 total resolution), Parola/ICSTATION wiring type |
| Micro USB cable | For power + programming |
| 5V power source | USB power bank, phone charger, or PC USB port |

## Wiring

| MAX7219 Module Pin | D1 Mini Pin | GPIO |
|---|---|---|
| VCC | 5V | — |
| GND | GND | — |
| DIN | D7 | GPIO13 (MOSI) |
| CS / LOAD | D8 | GPIO15 |
| CLK | D5 | GPIO14 (SCK) |

> **Note:** If the display shows garbled/random pixels, add a 100–1000 µF electrolytic capacitor across VCC/GND at the first module in the chain, and/or power the display from a separate 5V source rather than the D1 Mini's onboard regulator.

## Software Requirements

- [Arduino IDE](https://www.arduino.cc/en/software)
- **ESP8266 board package** installed via Boards Manager (board: *LOLIN(WEMOS) D1 R2 & mini*)
- Libraries (install via Arduino Library Manager):
  - `MD_MAX72XX` (by majicDesigns)
  - `ArduinoJson` (by Benoit Blanchon, v6.x)
  - `ESP8266WiFi`, `ESP8266HTTPClient`, `WiFiClientSecure` — bundled with the ESP8266 board package, no separate install needed

## Setup

1. Wire the display as described above.
2. Open the `.ino` sketch in Arduino IDE.
3. Fill in your details at the top of the file:
   ```cpp
   const char* WIFI_SSID     = "YOUR_WIFI_NAME";
   const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

   const char* LAT = "28.6330";  // your latitude 
   const char* LON = "77.2196";  // your longitude
   ```
   Get your coordinates by right-clicking your location on Google Maps.
4. Select **Board: LOLIN(WEMOS) D1 R2 & mini** and the correct COM port.
5. Upload.
6. Open Serial Monitor (115200 baud) to confirm WiFi connection, NTP sync, and weather fetch are all working.

## Configuration

| Setting | Location | Default |
|---|---|---|
| Clock display duration | `CLOCK_DURATION` | 10 seconds |
| Weather display duration | `WEATHER_DURATION` | 5 seconds |
| Weather refresh interval | `FETCH_INTERVAL` | 5 minutes |
| Display brightness | `mx.control(MD_MAX72XX::INTENSITY, ...)` | 1 (range 0–15) |
| Timezone offset | `GMT_OFFSET_SEC` | 19800 (IST, UTC+5:30) |

## How It Works

1. **WiFi connects** on boot, then **NTP** syncs the real time from `pool.ntp.org`.
2. **Open-Meteo** is queried for current temperature and a numeric weather code, decoded into a condition (Clear, Clouds, Rain, Thunderstorm, Snow, Fog).
3. Both the clock and the weather frame are drawn as **raw pixels** directly via `MD_MAX72XX` — no scrolling-text library is used, which avoids any library-side buffer conflicts between the icon and the digits.
4. A simple state machine in `loop()` alternates between the two frames on a timer, while weather refreshes independently in the background.

## Customizing the Icons

Each weather icon is defined as a 16×8 grid of `#` (lit) and `.` (off) characters directly in the code:

```cpp
const char* SUN[8] = {
  "....#......#....",
  ".....#.##.#.....",
  "......####......",
  ...
};
```

To tweak a shape, just edit the grid — no bit math or hex required.

## Troubleshooting

| Symptom | Likely Cause |
|---|---|
| Garbled/random pixels | Missing decoupling capacitor or insufficient power — see wiring note above |
| Icon or text upside-down/mirrored | Module's internal row/column addressing differs — flip the bit mapping in `drawGridAt()` |
| Temperature not showing | Check Serial Monitor for HTTP/JSON errors — usually a WiFi or coordinate issue |
| Time is wrong | Check `GMT_OFFSET_SEC` matches your timezone |

## License

MIT — free to use, modify, and share.
