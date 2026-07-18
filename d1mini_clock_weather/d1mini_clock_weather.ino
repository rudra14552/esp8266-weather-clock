/*
  ESP-12 D1 Mini V2 + 4x MAX7219 + Open-Meteo Weather + NTP Clock
  Cycles forever:
    - Clock (12hr, centered, blinking colon)   shown for 10 seconds
    - Weather icon + temperature               shown for 5 seconds
  All rendering is raw pixels via MD_MAX72XX (no MD_Parola text).

  Libraries: MD_MAX72XX, ArduinoJson
  Board: LOLIN(WEMOS) D1 R2 & mini

  Wiring (MAX7219 -> D1 Mini):
    VCC -> 5V   GND -> GND
    DIN -> D7 (GPIO13, MOSI)
    CS  -> D8 (GPIO15)
    CLK -> D5 (GPIO14, SCK)
*/

#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <MD_MAX72xx.h>
#include <SPI.h>
#include <time.h>

// ---------- WiFi ----------
const char* WIFI_SSID = "Arcade_5G";
const char* WIFI_PASSWORD = "12345678";

// ---------- Location ----------
const char* LAT = "28.6330";
const char* LON = "77.2196";

// ---------- Timezone (India = UTC+5:30) ----------
const long GMT_OFFSET_SEC = 19800;
const int  DAYLIGHT_OFFSET_SEC = 0;
const char* NTP_SERVER_1 = "pool.ntp.org";
const char* NTP_SERVER_2 = "time.nist.gov";

// ---------- Display ----------
#define HARDWARE_TYPE MD_MAX72XX::FC16_HW
#define MAX_DEVICES 4
#define DATA_PIN D7
#define CLK_PIN  D5
#define CS_PIN   D8

MD_MAX72XX mx = MD_MAX72XX(HARDWARE_TYPE, DATA_PIN, CLK_PIN, CS_PIN, MAX_DEVICES);

unsigned long lastFetch = 0;
const unsigned long FETCH_INTERVAL = 5UL * 60UL * 1000UL; // 10 min

// ---------- Display cycle ----------
enum DisplayState { SHOW_CLOCK, SHOW_WEATHER };
DisplayState currentState = SHOW_CLOCK;
unsigned long stateStart = 0;
const unsigned long CLOCK_DURATION   = 10000; // 10 sec
const unsigned long WEATHER_DURATION = 5000;  // 5 sec

unsigned long lastClockRedraw = 0;
int lastDrawnMinute = -1; // tracks last minute actually drawn, avoids redundant redraws (fixes flicker)
bool colonVisible = true;
unsigned long lastColonToggle = 0;
int colonCol = 0; // remembers where the colon sits so we can redraw just that spot

// last fetched weather, redrawn when weather frame comes up
const char** lastIcon = nullptr;
int lastTemp = 0;
bool weatherReady = false;

// ---------- Icon grids (16 cols x 8 rows) ----------
const char* SUN[8] = {
  "....#......#....",
  ".....#.##.#.....",
  "......####......",
  ".....######.....",
  ".....######.....",
  "......####......",
  ".....#.##.#.....",
  "....#......#....",
};
const char* CLOUD[8] = {
  "................",
  ".....##.........",
  "...######.......",
  ".##########.....",
  "#############...",
  "###############.",
  "................",
  "................",
};
const char* RAIN[8] = {
  ".....###........",
  "...#######......",
  ".###########....",
  "##############..",
  "................",
  "................",
  "..##..##..##....",
  ".##..##..##.....",
};
const char* STORM[8] = {
  "...#######......",
  ".###########....",
  "################",
  "................",
  "......####......",
  ".....###........",
  "....##..........",
  "....#...........",
};
const char* SNOW[8] = {
  ".#....#....#....",
  "###..###..###...",
  ".#....#....#....",
  "................",
  ".#....#....#....",
  "###..###..###...",
  ".#....#....#....",
  "................",
};
const char* FOG[8] = {
  "................",
  "###############.",
  "................",
  "..##########....",
  "................",
  "###############.",
  "................",
  "..##########....",
};
const char* QMARK[8] = {
  "..######........",
  ".##....##.......",
  ".......##.......",
  "......##........",
  ".....##.........",
  "................",
  "....##..........",
  "....##..........",
};

// ---------- Tiny 4-wide digit/letter font (rows 0-6 = glyph, row 7 blank) ----------
const char* F_0[8] = {"####","#..#","#..#","#..#","#..#","#..#","#..#","####"};
const char* F_1[8] = {".##.","###.",".##.",".##.",".##.",".##.",".##.","####"};
const char* F_2[8] = {"####","...#","...#","####","#...","#...","#...","####"};
const char* F_3[8] = {"####","...#","...#",".###","...#","...#","...#","####"};
const char* F_4[8] = {"#..#","#..#","#..#","####","...#","...#","...#","...#"};
const char* F_5[8] = {"####","#...","#...","####","...#","...#","...#","####"};
const char* F_6[8] = {"####","#...","#...","####","#..#","#..#","#..#","####"};
const char* F_7[8] = {"####","...#","...#","..#.",".#..",".#..",".#..",".#.."};
const char* F_8[8] = {"####","#..#","#..#","####","#..#","#..#","#..#","####"};
const char* F_9[8] = {"####","#..#","#..#","####","...#","...#","...#","####"};
const char* F_C[8] = {".###","#...","#...","#...","#...","#...",".###","...."};
const char* F_c[8] = {"....","....","....",".###","#...","#...",".###","...."};
const char* F_MINUS[8] = {"....","....","....","####","....","....","....","...."};

// 4-wide letters for AM/PM indicator
const char* F_A[8] = {".##.","#..#","#..#","####","#..#","#..#","#..#","...."};
const char* F_P[8] = {"###.","#..#","#..#","###.","#...","#...","#...","...."};

// 2-wide colon made of two actual 2x2 pixel squares
const char* F_COLON[8] = {"..","##","##","..","..","##","##",".."};

const char** getGlyph(char c) {
  switch (c) {
    case '0': return F_0; case '1': return F_1; case '2': return F_2;
    case '3': return F_3; case '4': return F_4; case '5': return F_5;
    case '6': return F_6; case '7': return F_7; case '8': return F_8;
    case '9': return F_9; case 'C': return F_C; case 'c': return F_c;
    case '-': return F_MINUS;
  }
  return nullptr;
}

// ---------- Raw pixel drawing helpers ----------
void drawGridAt(const char** grid, int startCol, int width) {
  for (int col = 0; col < width; col++) {
    uint8_t colByte = 0;
    for (int row = 0; row < 8; row++) {
      if (grid[row][col] == '#') {
        colByte |= (1 << (7 - row)); // flip so grid's top row = display's top row
      }
    }
    mx.setColumn(startCol + col, colByte);
  }
}

void clearRange(int startCol, int endCol) {
  for (int col = startCol; col <= endCol; col++) {
    mx.setColumn(col, 0);
  }
}

void drawIcon(const char** icon) {
  drawGridAt(icon, 0, 16); // columns 0-15
}

void drawTemperature(int tempInt) {
  char buf[6];
  sprintf(buf, "%dc", tempInt);

  clearRange(16, 31); // only wipe the temperature side, icon untouched

  int numChars = strlen(buf);
  int totalWidth = (numChars * 5) - 1; // 4-wide glyph + 1 gap each, no gap after last char
  int startCol = 32 - totalWidth;      // right-align so it ends exactly at column 31

  int col = startCol;
  for (int i = 0; buf[i] != '\0'; i++) {
    const char** glyph = getGlyph(buf[i]);
    if (glyph) {
      drawGridAt(glyph, col, 4);
      col += 5;
    }
  }
}

// ---------- Clock rendering ----------
// Layout: H H : M M   total width = 22, centered in 32 columns
void drawClock(int h12, int minute) {
  clearRange(0, 31);

  char h1 = '0' + (h12 / 10);
  char h2 = '0' + (h12 % 10);
  char m1 = '0' + (minute / 10);
  char m2 = '0' + (minute % 10);

  const int DIGIT_GAP = 1; // gap between the two hour digits / two minute digits
  const int COLON_GAP = 2; // gap on EACH side of the colon

  int totalWidth = 4 + DIGIT_GAP + 4 + COLON_GAP + 2 + COLON_GAP + 4 + DIGIT_GAP + 4; // = 24
  int startCol = (32 - totalWidth) / 2;

  int col = startCol;
  drawGridAt(getGlyph(h1), col, 4); col += 4 + DIGIT_GAP;
  drawGridAt(getGlyph(h2), col, 4); col += 4;

  col += COLON_GAP; // gap BEFORE colon

  colonCol = col;
  if (colonVisible) drawGridAt(F_COLON, colonCol, 2);
  col += 2;

  col += COLON_GAP; // gap AFTER colon

  drawGridAt(getGlyph(m1), col, 4); col += 4 + DIGIT_GAP;
  drawGridAt(getGlyph(m2), col, 4);
}
// ---------- Weather ----------
const char** pickIcon(const String& condition) {
  if (condition == "Clear")                        return SUN;
  if (condition == "Clouds")                        return CLOUD;
  if (condition == "Rain" || condition == "Drizzle") return RAIN;
  if (condition == "Thunderstorm")                   return STORM;
  if (condition == "Snow")                           return SNOW;
  if (condition == "Fog")                            return FOG;
  return QMARK;
}

void toggleColon() {
  colonVisible = !colonVisible;
  if (colonVisible) {
    drawGridAt(F_COLON, colonCol, 2);
  } else {
    clearRange(colonCol, colonCol + 1);
  }
}

String decodeWeatherCode(int code) {
  if (code == 0) return "Clear";
  if (code == 1 || code == 2 || code == 3) return "Clouds";
  if (code == 45 || code == 48) return "Fog";
  if (code >= 51 && code <= 57) return "Drizzle";
  if (code >= 61 && code <= 67) return "Rain";
  if (code >= 71 && code <= 77) return "Snow";
  if (code >= 80 && code <= 82) return "Rain";
  if (code >= 85 && code <= 86) return "Snow";
  if (code >= 95 && code <= 99) return "Thunderstorm";
  return "Unknown";
}

void showWeatherFrame() {
  if (!weatherReady) return;
  drawIcon(lastIcon);
  drawTemperature(lastTemp);
}

//----------------------------------------

void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting");
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 20000) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("WiFi failed, will retry.");
  }
}

void setupTime() {
  configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER_1, NTP_SERVER_2);
  Serial.print("Waiting for NTP sync");
  time_t now = time(nullptr);
  int tries = 0;
  while (now < 100000 && tries < 30) {
    delay(500);
    Serial.print(".");
    now = time(nullptr);
    tries++;
  }
  Serial.println();
  Serial.println(now < 100000 ? "NTP sync failed." : "Time synced.");
}

void fetchWeather() {
  if (WiFi.status() != WL_CONNECTED) return;

  WiFiClientSecure client;
  client.setInsecure(); // skip certificate validation - simplest approach for a hobby project

  HTTPClient http;
  String url = "https://api.open-meteo.com/v1/forecast?latitude=" + String(LAT) +
               "&longitude=" + String(LON) +
               "&current=temperature_2m,weather_code";

  Serial.println(url);
  http.begin(client, url);
  int code = http.GET();

  if (code != 200) {
    Serial.print("HTTP Error: ");
    Serial.println(code);
    http.end();
    return;
  }

  String payload = http.getString();
  http.end();

  StaticJsonDocument<512> doc;
  DeserializationError err = deserializeJson(doc, payload);
  if (err) {
    Serial.println("JSON Error");
    return;
  }

  float temp = doc["current"]["temperature_2m"];
  int wcode = doc["current"]["weather_code"];
  String condition = decodeWeatherCode(wcode);

  Serial.print("Condition: ");
  Serial.print(condition);
  Serial.print(" | Temperature: ");
  Serial.println(temp);

  lastIcon = pickIcon(condition);
  lastTemp = (int)round(temp);
  weatherReady = true;

  if (currentState == SHOW_WEATHER) showWeatherFrame(); // refresh immediately if currently visible
}

//----------------------------------------

void setup() {
  Serial.begin(115200);

  mx.begin();
  mx.control(MD_MAX72XX::INTENSITY, 1);
  mx.clear();

  connectWiFi();
  setupTime();
  fetchWeather();
  lastFetch = millis();

  stateStart = millis();
  currentState = SHOW_CLOCK;
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) connectWiFi();

  if (millis() - lastFetch > FETCH_INTERVAL) {
    fetchWeather();
    lastFetch = millis();
  }

  unsigned long elapsed = millis() - stateStart;

  if (currentState == SHOW_CLOCK) {
    // check every second, but only actually redraw when the minute changes (prevents flicker)
    if (millis() - lastClockRedraw > 1000) {
      time_t now = time(nullptr);
      struct tm* t = localtime(&now);
      int h24 = t->tm_hour;
      int h12 = h24 % 12;
      if (h12 == 0) h12 = 12;

      if (t->tm_min != lastDrawnMinute) {
        drawClock(h12, t->tm_min);
        lastDrawnMinute = t->tm_min;
      }
      lastClockRedraw = millis();
    }
    if (millis() - lastColonToggle > 500) {
      toggleColon();
      lastColonToggle = millis();
    }
    if (elapsed > CLOCK_DURATION) {
      currentState = SHOW_WEATHER;
      stateStart = millis();
      showWeatherFrame();
    }
  } else { // SHOW_WEATHER
    if (elapsed > WEATHER_DURATION) {
      currentState = SHOW_CLOCK;
      stateStart = millis();
      lastDrawnMinute = -1; // force immediate redraw when clock reappears
    }
  }
}
