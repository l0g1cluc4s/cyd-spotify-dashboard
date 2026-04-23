#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <TFT_eSPI.h>
#include <TJpg_Decoder.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <cctype>
#include <cstring>
#include <memory>
#include <mbedtls/base64.h>
#include <time.h>

#include "CYD28_TouchscreenR.h"

#if __has_include("secrets.h")
#include "secrets.h"
#else
#error "Copy include/secrets_example.h to include/secrets.h and fill your Wi-Fi and Spotify credentials."
#endif

namespace {
constexpr int SCREEN_W = 320;
constexpr int SCREEN_H = 240;
constexpr uint8_t TFT_LANDSCAPE_ROTATION = 1;
constexpr int TFT_CS_PIN = 15;

constexpr uint16_t BG = TFT_BLACK;
constexpr uint16_t CARD = 0x1082;
constexpr uint16_t PANEL = 0x2104;
constexpr uint16_t ACCENT = 0x1FE8;
constexpr uint16_t TEXT = TFT_WHITE;
constexpr uint16_t MUTED = 0x9CF3;
constexpr uint16_t HOME_BG = 0x0841;
constexpr uint16_t HOME_PANEL = 0x18C3;
constexpr uint16_t HOME_CARD = 0x2945;
constexpr uint16_t WIFI_OK = 0x07E0;
constexpr uint16_t WIFI_BAD = TFT_RED;

constexpr unsigned long PLAYER_REFRESH_MS = 8000;
constexpr unsigned long TOUCH_DEBOUNCE_MS = 350;
constexpr unsigned long HOME_CLOCK_REFRESH_MS = 1000;
constexpr size_t MAX_IMAGE_BYTES = 190 * 1024;
constexpr int ALBUM_BOX_X = 112;
constexpr int ALBUM_BOX_Y = 42;
constexpr int ALBUM_BOX_W = 96;
constexpr int ALBUM_BOX_H = 96;
constexpr int ALBUM_X = 85;
constexpr int ALBUM_Y = 42;

TFT_eSPI tft;
CYD28_TouchR touch(SCREEN_W, SCREEN_H);

String accessToken;
unsigned long tokenExpiresAt = 0;
unsigned long lastPlayerPoll = PLAYER_REFRESH_MS;
unsigned long lastTouchMs = 0;
unsigned long lastTouchLogMs = 0;

String currentTrackId;
String currentImageUrl;
String currentTitle;
String currentArtist;
bool isPlaying = false;
bool albumArtDrawn = false;

struct Button {
  int x;
  int y;
  int w;
  int h;
  const char *label;
};

Button prevBtn{42, 188, 64, 38, "<<"};
Button playBtn{128, 182, 64, 50, "II"};
Button nextBtn{214, 188, 64, 38, ">>"};
Button homeBtn{264, 14, 32, 24, "H"};
Button openSpotifyBtn{24, 154, 128, 54, "Spotify"};

enum class TouchAction {
  None,
  Previous,
  PlayPause,
  Next,
};

enum class AppScreen {
  Home,
  Player,
};

AppScreen currentScreen = AppScreen::Home;
unsigned long lastHomeClockDraw = 0;
String lastHomeTime;
String lastHomeDate;

bool isPlaceholder(const char *value) {
  return value == nullptr || value[0] == '\0' || strncmp(value, "YOUR_", 5) == 0;
}

bool hasValidConfig() {
  return !isPlaceholder(WIFI_SSID) &&
         !isPlaceholder(WIFI_PASSWORD) &&
         !isPlaceholder(SPOTIFY_CLIENT_ID) &&
         !isPlaceholder(SPOTIFY_CLIENT_SECRET) &&
         !isPlaceholder(SPOTIFY_REFRESH_TOKEN);
}

void applyCydOrientation() {
  tft.setRotation(TFT_LANDSCAPE_ROTATION);
}

void clearDisplayMemory() {
  for (uint8_t r = 0; r < 4; ++r) {
    tft.setRotation(r);
    tft.fillScreen(BG);
  }
  applyCydOrientation();
  tft.fillScreen(BG);
}

String fitText(String text, size_t maxChars) {
  String normalized;
  normalized.reserve(text.length());

  for (size_t i = 0; i < text.length();) {
    uint8_t c = static_cast<uint8_t>(text[i]);
    if (c < 0x80) {
      normalized += static_cast<char>(c);
      ++i;
      continue;
    }

    if (i + 1 >= text.length()) {
      ++i;
      continue;
    }

    uint8_t next = static_cast<uint8_t>(text[i + 1]);
    if (c == 0xC3) {
      switch (next) {
      case 0x80: case 0x81: case 0x82: case 0x83: case 0x84: case 0x85:
        normalized += 'A';
        break;
      case 0x87:
        normalized += 'C';
        break;
      case 0x88: case 0x89: case 0x8A: case 0x8B:
        normalized += 'E';
        break;
      case 0x8C: case 0x8D: case 0x8E: case 0x8F:
        normalized += 'I';
        break;
      case 0x91:
        normalized += 'N';
        break;
      case 0x92: case 0x93: case 0x94: case 0x95: case 0x96:
        normalized += 'O';
        break;
      case 0x99: case 0x9A: case 0x9B: case 0x9C:
        normalized += 'U';
        break;
      case 0x9D: case 0x9F:
        normalized += 'Y';
        break;
      case 0xA0: case 0xA1: case 0xA2: case 0xA3: case 0xA4: case 0xA5:
        normalized += 'a';
        break;
      case 0xA7:
        normalized += 'c';
        break;
      case 0xA8: case 0xA9: case 0xAA: case 0xAB:
        normalized += 'e';
        break;
      case 0xAC: case 0xAD: case 0xAE: case 0xAF:
        normalized += 'i';
        break;
      case 0xB1:
        normalized += 'n';
        break;
      case 0xB2: case 0xB3: case 0xB4: case 0xB5: case 0xB6:
        normalized += 'o';
        break;
      case 0xB9: case 0xBA: case 0xBB: case 0xBC:
        normalized += 'u';
        break;
      case 0xBD: case 0xBF:
        normalized += 'y';
        break;
      default:
        break;
      }
      i += 2;
      continue;
    }

    i += 2;
  }

  text = normalized;
  if (text.length() <= maxChars) {
    return text;
  }
  return text.substring(0, maxChars - 3) + "...";
}

String formEncode(const String &value) {
  String out;
  const char *hex = "0123456789ABCDEF";
  for (size_t i = 0; i < value.length(); ++i) {
    const uint8_t c = static_cast<uint8_t>(value[i]);
    if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
      out += static_cast<char>(c);
    } else {
      out += '%';
      out += hex[(c >> 4) & 0x0F];
      out += hex[c & 0x0F];
    }
  }
  return out;
}

String base64Encode(const String &input) {
  size_t outLen = 0;
  mbedtls_base64_encode(nullptr, 0, &outLen,
                        reinterpret_cast<const unsigned char *>(input.c_str()),
                        input.length());

  std::unique_ptr<unsigned char[]> buf(new unsigned char[outLen + 1]);
  if (mbedtls_base64_encode(buf.get(), outLen + 1, &outLen,
                            reinterpret_cast<const unsigned char *>(input.c_str()),
                            input.length()) != 0) {
    return "";
  }

  buf[outLen] = '\0';
  return reinterpret_cast<char *>(buf.get());
}

void drawCenteredText(const String &text, int y, uint16_t color, uint16_t bg, uint8_t font = 2) {
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(color, bg);
  tft.drawString(text, SCREEN_W / 2, y, font);
}

void drawButton(const Button &btn, uint16_t fill, const char *label = nullptr) {
  tft.fillRoundRect(btn.x, btn.y, btn.w, btn.h, 12, fill);
  tft.drawRoundRect(btn.x, btn.y, btn.w, btn.h, 12, TFT_DARKGREY);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TEXT, fill);
  tft.drawString(label ? label : btn.label, btn.x + btn.w / 2, btn.y + btn.h / 2, 2);
}

void drawControls() {
  drawButton(prevBtn, PANEL);
  drawButton(playBtn, ACCENT, isPlaying ? "II" : ">");
  drawButton(nextBtn, PANEL);
}

void drawTrackText();

void drawPlayerScreen() {
  clearDisplayMemory();
  albumArtDrawn = false;
  tft.fillRoundRect(6, 6, 308, 228, 16, CARD);
  tft.drawRoundRect(6, 6, 308, 228, 16, 0x39E7);
  tft.fillRoundRect(16, 14, 288, 24, 10, BG);

  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(ACCENT, BG);
  tft.drawString("Spotify", 28, 26, 4);

  tft.setTextDatum(MR_DATUM);
  tft.setTextColor(MUTED, BG);
  tft.drawString("CYD", 252, 26, 2);
  drawButton(homeBtn, PANEL);

  if (currentTitle.length() > 0) {
    tft.fillRect(20, 40, 280, 100, CARD);
    tft.fillRoundRect(ALBUM_BOX_X, ALBUM_BOX_Y, ALBUM_BOX_W, ALBUM_BOX_H, 12, BG);
    tft.drawRoundRect(ALBUM_BOX_X, ALBUM_BOX_Y, ALBUM_BOX_W, ALBUM_BOX_H, 12, 0x39E7);
    drawTrackText();
  } else {
    drawCenteredText("Abrindo Spotify", 114, MUTED, CARD, 2);
  }
  drawControls();
}

void drawHomeChrome() {
  clearDisplayMemory();
  tft.fillScreen(HOME_BG);
  tft.fillRoundRect(8, 8, 304, 224, 18, HOME_PANEL);
  tft.drawRoundRect(8, 8, 304, 224, 18, 0x4208);

  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(ACCENT, HOME_PANEL);
  tft.drawString("CYD Hub", 24, 28, 4);

  tft.setTextDatum(MR_DATUM);
  tft.setTextColor(MUTED, HOME_PANEL);
  tft.drawString("ESP32-2432S028", 294, 25, 2);

  tft.fillRoundRect(24, 56, 272, 82, 16, HOME_CARD);
  tft.drawRoundRect(24, 56, 272, 82, 16, 0x528A);

  tft.fillRoundRect(168, 154, 128, 54, 14, HOME_CARD);
  tft.drawRoundRect(168, 154, 128, 54, 14, 0x4208);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TEXT, HOME_CARD);
  tft.drawString(WiFi.status() == WL_CONNECTED ? "Wi-Fi OK" : "Wi-Fi OFF", 232, 174, 2);
  tft.setTextColor(WiFi.status() == WL_CONNECTED ? WIFI_OK : WIFI_BAD, HOME_CARD);
  tft.drawString(WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : "sem rede", 232, 194, 1);

  drawButton(openSpotifyBtn, ACCENT);
}

String currentTimeText() {
  struct tm timeInfo;
  if (!getLocalTime(&timeInfo, 10)) {
    return "--:--";
  }
  char buf[8];
  strftime(buf, sizeof(buf), "%H:%M", &timeInfo);
  return String(buf);
}

String currentDateText() {
  struct tm timeInfo;
  if (!getLocalTime(&timeInfo, 10)) {
    return "Sincronizando data";
  }
  char buf[24];
  strftime(buf, sizeof(buf), "%d/%m/%Y", &timeInfo);
  return String(buf);
}

void drawHomeClock(bool force = false) {
  if (!force && millis() - lastHomeClockDraw < HOME_CLOCK_REFRESH_MS) {
    return;
  }
  lastHomeClockDraw = millis();

  String timeText = currentTimeText();
  String dateText = currentDateText();
  if (!force && timeText == lastHomeTime && dateText == lastHomeDate) {
    return;
  }

  lastHomeTime = timeText;
  lastHomeDate = dateText;

  tft.fillRoundRect(28, 60, 264, 74, 14, HOME_CARD);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TEXT, HOME_CARD);
  tft.drawString(timeText, SCREEN_W / 2, 88, 7);
  tft.setTextColor(MUTED, HOME_CARD);
  tft.drawString(dateText, SCREEN_W / 2, 123, 2);
}

void drawHomeScreen() {
  lastHomeTime = "";
  lastHomeDate = "";
  drawHomeChrome();
  drawHomeClock(true);
}

void switchToHome() {
  currentScreen = AppScreen::Home;
  drawHomeScreen();
}

void switchToPlayer() {
  currentScreen = AppScreen::Player;
  drawPlayerScreen();
  lastPlayerPoll = 0;
}

void drawTrackText() {
  tft.fillRect(20, 142, 280, 36, CARD);
  drawCenteredText(fitText(currentTitle, 32), 150, TEXT, CARD, 2);
  drawCenteredText(fitText(currentArtist, 36), 170, MUTED, CARD, 2);
}

bool jpgOutput(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t *bitmap) {
  if (y >= SCREEN_H || x >= SCREEN_W) {
    return false;
  }

  if (x >= ALBUM_BOX_X + ALBUM_BOX_W || y >= ALBUM_BOX_Y + ALBUM_BOX_H ||
      x + w <= ALBUM_BOX_X || y + h <= ALBUM_BOX_Y) {
    return true;
  }

  uint16_t cropLeft = x < ALBUM_BOX_X ? ALBUM_BOX_X - x : 0;
  uint16_t cropTop = y < ALBUM_BOX_Y ? ALBUM_BOX_Y - y : 0;
  uint16_t drawX = x + cropLeft;
  uint16_t drawY = y + cropTop;
  uint16_t drawW = min<uint16_t>(w - cropLeft, ALBUM_BOX_X + ALBUM_BOX_W - drawX);
  uint16_t drawH = min<uint16_t>(h - cropTop, ALBUM_BOX_Y + ALBUM_BOX_H - drawY);

  tft.setSwapBytes(false);
  tft.pushImage(drawX, drawY, drawW, drawH, bitmap + cropTop * w + cropLeft);
  return true;
}

bool refreshAccessToken() {
  if (accessToken.length() > 0 && millis() < tokenExpiresAt) {
    return true;
  }

  HTTPClient http;
  WiFiClientSecure client;
  String auth = base64Encode(String(SPOTIFY_CLIENT_ID) + ":" + SPOTIFY_CLIENT_SECRET);
  String body = "grant_type=refresh_token&refresh_token=" + formEncode(SPOTIFY_REFRESH_TOKEN);

  client.setInsecure();
  http.setReuse(false);
  http.setTimeout(10000);
  if (!http.begin(client, "https://accounts.spotify.com/api/token")) {
    return false;
  }
  http.addHeader("Authorization", "Basic " + auth);
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");

  int code = http.POST(body);
  if (code != 200) {
    Serial.printf("Token refresh failed: %d\n", code);
    Serial.println(http.getString());
    http.end();
    return false;
  }

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, http.getString());
  http.end();
  if (err) {
    Serial.printf("Token JSON error: %s\n", err.c_str());
    return false;
  }

  accessToken = doc["access_token"].as<String>();
  int expiresIn = doc["expires_in"] | 3600;
  tokenExpiresAt = millis() + (expiresIn - 60) * 1000UL;
  return accessToken.length() > 0;
}

int spotifyRequest(const char *method, const char *url, const char *body = "") {
  if (!refreshAccessToken()) {
    return -1000;
  }

  HTTPClient http;
  WiFiClientSecure client;
  client.setInsecure();
  http.setReuse(false);
  http.setTimeout(10000);
  if (!http.begin(client, url)) {
    return -1001;
  }
  http.addHeader("Authorization", "Bearer " + accessToken);
  http.addHeader("Content-Type", "application/json");

  int code = 0;
  if (strcmp(method, "PUT") == 0) {
    code = http.PUT(reinterpret_cast<uint8_t *>(const_cast<char *>(body)), strlen(body));
  } else if (strcmp(method, "POST") == 0) {
    code = http.POST(reinterpret_cast<uint8_t *>(const_cast<char *>(body)), strlen(body));
  } else {
    code = http.GET();
  }

  Serial.printf("Spotify command %s %s -> %d\n", method, url, code);
  if (code < 200 || code >= 300) {
    Serial.println(http.getString());
  }
  http.end();
  return code;
}

void showCommandStatus(const char *message, uint16_t color = MUTED) {
  tft.fillRect(20, 134, 280, 12, CARD);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(color, CARD);
  tft.drawString(message, SCREEN_W / 2, 140, 1);
}

void commandPrevious() {
  drawButton(prevBtn, ACCENT);
  int code = spotifyRequest("POST", "https://api.spotify.com/v1/me/player/previous", "{}");
  if (code < 200 || code >= 300) {
    char status[32];
    snprintf(status, sizeof(status), "previous falhou %d", code);
    showCommandStatus(status, TFT_RED);
  }
  drawControls();
  lastPlayerPoll = 0;
}

void commandNext() {
  drawButton(nextBtn, ACCENT);
  int code = spotifyRequest("POST", "https://api.spotify.com/v1/me/player/next", "{}");
  if (code < 200 || code >= 300) {
    char status[32];
    snprintf(status, sizeof(status), "next falhou %d", code);
    showCommandStatus(status, TFT_RED);
  }
  drawControls();
  lastPlayerPoll = 0;
}

void commandPlayPause() {
  const char *url = isPlaying
                        ? "https://api.spotify.com/v1/me/player/pause"
                        : "https://api.spotify.com/v1/me/player/play";
  drawButton(playBtn, PANEL, isPlaying ? "II" : ">");
  int code = spotifyRequest("PUT", url, "{}");
  if (code >= 200 && code < 300) {
    isPlaying = !isPlaying;
    drawControls();
  } else {
    char status[32];
    snprintf(status, sizeof(status), "play/pause falhou %d", code);
    showCommandStatus(status, TFT_RED);
    drawControls();
  }
  lastPlayerPoll = 0;
}

bool inButton(const Button &btn, uint16_t x, uint16_t y) {
  return x >= btn.x && x <= btn.x + btn.w && y >= btn.y && y <= btn.y + btn.h;
}

bool readTouch(uint16_t &x, uint16_t &y) {
  digitalWrite(TFT_CS_PIN, HIGH);

  if (!touch.touched()) {
    return false;
  }

  CYD28_TS_Point scaled = touch.getPointScaled();

  if (scaled.x < 0 || scaled.y < 0 || scaled.x >= SCREEN_W || scaled.y >= SCREEN_H) {
    return false;
  }

  x = static_cast<uint16_t>(scaled.x);
  y = static_cast<uint16_t>(scaled.y);

  if (millis() - lastTouchLogMs > 150) {
    lastTouchLogMs = millis();
    Serial.printf("touch pressure=%d mapped=(%u,%u)\n", scaled.z, x, y);
  }

  return true;
}

TouchAction touchActionAt(uint16_t x, uint16_t y) {
  if (inButton(homeBtn, x, y)) {
    return TouchAction::None;
  }
  if (inButton(prevBtn, x, y) || (y >= 160 && x < 112)) {
    return TouchAction::Previous;
  }
  if (inButton(playBtn, x, y) || (y >= 154 && x >= 112 && x <= 208)) {
    return TouchAction::PlayPause;
  }
  if (inButton(nextBtn, x, y) || (y >= 160 && x > 208)) {
    return TouchAction::Next;
  }
  return TouchAction::None;
}

void handlePlayerTouch(uint16_t x, uint16_t y) {
  if (inButton(homeBtn, x, y)) {
    Serial.println("touch screen=home");
    switchToHome();
    return;
  }

  if (y < 150) {
    return;
  }

  TouchAction action = touchActionAt(x, y);
  switch (action) {
  case TouchAction::Previous:
    Serial.println("touch action=previous");
    commandPrevious();
    break;
  case TouchAction::PlayPause:
    Serial.println("touch action=play_pause");
    commandPlayPause();
    break;
  case TouchAction::Next:
    Serial.println("touch action=next");
    commandNext();
    break;
  case TouchAction::None:
    break;
  }
}

void handleHomeTouch(uint16_t x, uint16_t y) {
  if (inButton(openSpotifyBtn, x, y)) {
    Serial.println("touch screen=player");
    switchToPlayer();
  }
}

void handleTouch() {
  uint16_t x = 0;
  uint16_t y = 0;
  if (!readTouch(x, y) || millis() - lastTouchMs < TOUCH_DEBOUNCE_MS) {
    return;
  }
  lastTouchMs = millis();

  if (currentScreen == AppScreen::Home) {
    handleHomeTouch(x, y);
  } else {
    handlePlayerTouch(x, y);
  }
}

bool drawAlbumArtUrl(const String &url) {
  if (url.isEmpty()) {
    return false;
  }

  HTTPClient http;
  WiFiClientSecure client;
  client.setInsecure();
  http.setReuse(false);
  http.setTimeout(15000);
  if (!http.begin(client, url)) {
    return false;
  }

  int code = http.GET();
  int size = http.getSize();
  if (code != 200 || size <= 0 || static_cast<size_t>(size) > MAX_IMAGE_BYTES) {
    Serial.printf("Image download failed: code=%d size=%d\n", code, size);
    http.end();
    return false;
  }

  std::unique_ptr<uint8_t[]> jpg(new uint8_t[size]);
  WiFiClient *stream = http.getStreamPtr();
  int read = stream->readBytes(jpg.get(), size);
  http.end();

  if (read != size) {
    Serial.printf("Image read mismatch: %d/%d\n", read, size);
    return false;
  }

  TJpgDec.setJpgScale(2);
  TJpgDec.drawJpg(ALBUM_X, ALBUM_Y, jpg.get(), size);
  return true;
}

String imageUrlInRange(JsonArray images, int minWidth, int maxWidth) {
  String bestUrl;
  int bestWidth = 0;
  for (JsonObject image : images) {
    int width = image["width"] | 0;
    String url = image["url"].as<String>();
    if (width >= minWidth && width <= maxWidth && width > bestWidth) {
      bestWidth = width;
      bestUrl = url;
    }
  }
  return bestUrl;
}

bool drawAlbumArt(JsonArray images) {
  const String urls[] = {
      imageUrlInRange(images, 250, 499),
      imageUrlInRange(images, 1, 249),
  };

  if (!albumArtDrawn) {
    tft.fillRoundRect(ALBUM_BOX_X, ALBUM_BOX_Y, ALBUM_BOX_W, ALBUM_BOX_H, 12, BG);
    tft.drawRoundRect(ALBUM_BOX_X, ALBUM_BOX_Y, ALBUM_BOX_W, ALBUM_BOX_H, 12, 0x39E7);
  }

  for (const String &url : urls) {
    if (url.length() == 0) {
      continue;
    }
    if (drawAlbumArtUrl(url)) {
      currentImageUrl = url;
      albumArtDrawn = true;
      return true;
    }
  }
  return false;
}

void showIdle(const String &message) {
  tft.fillRect(18, 44, 284, 136, CARD);
  drawCenteredText(message, 112, MUTED, CARD, 2);
  currentTrackId = "";
  currentImageUrl = "";
  currentTitle = "";
  currentArtist = "";
  albumArtDrawn = false;
  drawControls();
}

void pollPlayer() {
  if (millis() - lastPlayerPoll < PLAYER_REFRESH_MS) {
    return;
  }
  lastPlayerPoll = millis();

  if (!refreshAccessToken()) {
    showIdle("Erro no token");
    return;
  }

  HTTPClient http;
  WiFiClientSecure client;
  client.setInsecure();
  http.setReuse(false);
  http.setTimeout(10000);
  if (!http.begin(client, "https://api.spotify.com/v1/me/player/currently-playing?additional_types=track")) {
    return;
  }
  http.addHeader("Authorization", "Bearer " + accessToken);

  int code = http.GET();
  if (code == 204) {
    http.end();
    showIdle("Nada tocando");
    return;
  }
  if (code != 200) {
    Serial.printf("Player poll failed: %d\n", code);
    Serial.println(http.getString());
    http.end();
    showIdle("Spotify offline");
    return;
  }

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, http.getString());
  http.end();
  if (err) {
    Serial.printf("Player JSON error: %s\n", err.c_str());
    return;
  }

  JsonObject item = doc["item"];
  if (item.isNull()) {
    showIdle("Nada tocando");
    return;
  }

  String newTrackId = item["id"].as<String>();
  String newTitle = item["name"].as<String>();
  String newArtist = item["artists"][0]["name"].as<String>();
  JsonArray newImages = item["album"]["images"].as<JsonArray>();
  bool newIsPlaying = doc["is_playing"] | false;

  bool trackChanged = newTrackId != currentTrackId;
  bool textChanged = newTitle != currentTitle || newArtist != currentArtist;
  bool playChanged = newIsPlaying != isPlaying;

  currentTrackId = newTrackId;
  currentTitle = newTitle;
  currentArtist = newArtist;
  isPlaying = newIsPlaying;

  if (trackChanged || !albumArtDrawn) {
    if (!albumArtDrawn) {
      tft.fillRect(20, 40, 280, 100, CARD);
    }
    if (!drawAlbumArt(newImages)) {
      drawCenteredText("Sem capa", 92, MUTED, CARD, 2);
    }
  }
  if (trackChanged || textChanged) {
    drawTrackText();
  }
  if (playChanged || trackChanged) {
    drawControls();
  }
}

void connectWiFi() {
  WiFi.disconnect(true, true);
  delay(300);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  tft.fillRect(20, 92, 280, 34, CARD);
  drawCenteredText("Conectando Wi-Fi", 108, MUTED, CARD, 2);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
    Serial.print(".");
    if (millis() - start > 20000) {
      Serial.printf("\nWi-Fi timeout, status=%d\n", WiFi.status());
      tft.fillRect(20, 92, 280, 50, CARD);
      drawCenteredText("Wi-Fi falhou", 102, TFT_RED, CARD, 2);
      drawCenteredText("Reiniciando tentativa", 122, MUTED, CARD, 2);
      WiFi.disconnect();
      delay(500);
      WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
      start = millis();
    }
  }
  Serial.printf("\nWi-Fi conectado: %s\n", WiFi.localIP().toString().c_str());
}
} // namespace

void setup() {
  Serial.begin(115200);
  delay(300);

  touch.begin();
  touch.setRotation(1);
  touch.setThreshold(CYD28_TouchR_Z_THRESH);

  tft.init();
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);
  pinMode(TFT_CS_PIN, OUTPUT);
  digitalWrite(TFT_CS_PIN, HIGH);
  tft.invertDisplay(true);
  tft.setSwapBytes(false);
  TJpgDec.setSwapBytes(true);
  TJpgDec.setCallback(jpgOutput);

  clearDisplayMemory();
  drawCenteredText("Iniciando CYD Hub", 108, MUTED, BG, 2);
  Serial.println("CYD Spotify UI: custom mode c / MADCTL 0x88 / 320x240");

  if (!hasValidConfig()) {
    drawCenteredText("Configure secrets.h", 132, TFT_RED, BG, 2);
    Serial.println("Missing configuration in include/secrets.h");
    while (true) {
      delay(1000);
    }
  }

  connectWiFi();
  configTime(-3 * 3600, 0, "pool.ntp.org", "time.nist.gov");
  switchToHome();
}

void loop() {
  handleTouch();
  if (currentScreen == AppScreen::Home) {
    drawHomeClock();
  } else {
    pollPlayer();
  }
  delay(10);
}
