/*
 * EasyWatch - ESP32 physical-button firmware
 * ===========================================
 * One IR button press  ->  a random batch of that genre's YouTube videos is
 * cast to the TV's native YouTube app. No TV app, no menus, no reading.
 *
 * It does exactly what the Tizen app did, but from a $5 microcontroller:
 *   1. Listen for one of 4 IR codes from the dedicated remote.
 *   2. Ask the YouTube Data API for a random batch of that genre.
 *   3. Cast the queue to the already-paired TV via the YouTube Lounge API.
 *
 * ---------------------------------------------------------------------------
 * HARDWARE
 *   Board (Arduino IDE): "ESP32 Dev Module"   (generic ESP32-WROOM DevKit)
 *   IR receiver OUT -> D4 (GPIO4),  VCC -> 3V3,  GND -> GND
 *
 * LIBRARIES (Arduino IDE -> Manage Libraries -> install each):
 *   - IRremote        by Armin Joachimsmeyer
 *   - ArduinoJson     by Benoit Blanchon   (v6 or v7)
 *   (WiFi / WiFiClientSecure / HTTPClient / Preferences ship with the ESP32 core.)
 *
 * SETUP
 *   - Copy secrets.example.h -> secrets.h and fill in WIFI_SSID / WIFI_PASSWORD.
 *     (API key + screen id are already filled from the TV-app setup.)
 *   - The TV and the ESP32 must be on the SAME 2.4GHz Wi-Fi.
 *
 * This is v1. Everything logs to Serial @115200 so we can watch each step
 * and debug the casting handshake together if a genre doesn't start.
 * ---------------------------------------------------------------------------
 */

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <ArduinoJson.h>

#define DECODE_NEC                 // we only need NEC for this remote
#include <IRremote.hpp>

#include "secrets.h"

// ---- IR -------------------------------------------------------------------
#define IR_RECEIVE_PIN 4

// The 4 button codes (full 32-bit NEC values captured with the sniffer).
#define CODE_RED     0xF40BFF00    // Telugu comedy
#define CODE_GREEN   0xF30CFF00    // South Indian comedy
#define CODE_YELLOW  0xF20DFF00    // Full movies
#define CODE_BLUE    0xF10EFF00    // Devotional / god songs

// Ignore the same button if pressed again within this window (ms),
// so a long press / repeat frames don't fire twice.
const unsigned long DEBOUNCE_MS = 4000;
uint32_t lastCode = 0;
unsigned long lastCodeAt = 0;

// ---- Genres ----------------------------------------------------------------
// Each genre has a few search queries; we pick one at random every press so
// she never gets the same loop of videos. "longOnly" adds the full-length
// filter (used for movies).
struct Genre {
  const char*  name;
  const char** queries;
  int          queryCount;
  const char*  duration;    // "medium" / "long" / nullptr - filters out Shorts
  bool         dayBased;    // pick query by weekday (devotional)
};

const char* Q_RED[] = {
  "telugu movie comedy scenes",
  "తెలుగు సినిమా కామెడీ సీన్స్",
  "telugu best comedy scenes back to back",
  "telugu latest movie comedy scenes",
};
const char* Q_GREEN[] = {
  "south indian movie comedy scenes",
  "tamil movie comedy scenes",
  "telugu dubbed comedy scenes",
  "south movie comedy scenes back to back",
};
const char* Q_YELLOW[] = {
  "telugu full movie",
  "telugu full length movie hd",
  "telugu old full movie",
};
// Devotional is chosen by weekday (see devotionalForToday()); this list is
// only a fallback if the clock isn't set yet.
const char* Q_BLUE[] = {
  "telugu devotional songs",
  "venkateswara swamy songs telugu",
};

Genre GENRE_RED    = { "Telugu comedy",  Q_RED,    4, "medium", false };
Genre GENRE_GREEN  = { "South comedy",   Q_GREEN,  4, "medium", false };
Genre GENRE_YELLOW = { "Movies",         Q_YELLOW, 3, "long",   false };
Genre GENRE_BLUE   = { "God songs",      Q_BLUE,   2, "medium", true  };

// ---- Devotional: deity of the day (Telugu tradition) ----------------------
// tm_wday: 0=Sunday ... 6=Saturday
const char* DEV_SUN[] = { "surya bhagavan songs telugu", "vishnu songs telugu", "telugu devotional songs" };
const char* DEV_MON[] = { "lord shiva songs telugu", "om namah shivaya songs telugu", "shiva devotional songs telugu" };
const char* DEV_TUE[] = { "anjaneya swamy songs telugu", "hanuman chalisa telugu", "durga devi songs telugu" };
const char* DEV_WED[] = { "lord krishna songs telugu", "krishna devotional songs telugu", "vishnu songs telugu" };
const char* DEV_THU[] = { "sai baba songs telugu", "dattatreya songs telugu", "guru devotional songs telugu" };
const char* DEV_FRI[] = { "durga devi songs telugu", "lakshmi devi songs telugu", "ammavaru songs telugu" };
const char* DEV_SAT[] = { "venkateswara swamy songs telugu", "govinda govinda balaji songs", "sri venkateswara suprabhatam" };

// Returns today's deity query list; falls back to Saturday/Venkateswara
// if the clock isn't set.
void devotionalForToday(const char**& arr, int& count, const char*& dayName) {
  struct tm t;
  int wday = 6;
  if (getLocalTime(&t, 200)) wday = t.tm_wday;
  switch (wday) {
    case 0: arr = DEV_SUN; count = 3; dayName = "Sunday/Surya";     break;
    case 1: arr = DEV_MON; count = 3; dayName = "Monday/Shiva";     break;
    case 2: arr = DEV_TUE; count = 3; dayName = "Tuesday/Anjaneya"; break;
    case 3: arr = DEV_WED; count = 3; dayName = "Wednesday/Krishna";break;
    case 4: arr = DEV_THU; count = 3; dayName = "Thursday/SaiBaba"; break;
    case 5: arr = DEV_FRI; count = 3; dayName = "Friday/Durga";     break;
    default:arr = DEV_SAT; count = 3; dayName = "Saturday/Venkateswara"; break;
  }
}

// ---- Casting state ---------------------------------------------------------
Preferences prefs;
String deviceId;   // stable per-board id for the Lounge session

const int    MAX_IDS = 20;
String       videoIds[MAX_IDS];
int          videoCount = 0;

// ===========================================================================
// Helpers
// ===========================================================================

// Percent-encode a UTF-8 string so Telugu queries and spaces survive in a URL.
String urlEncode(const String& s) {
  const char* hex = "0123456789ABCDEF";
  String out;
  for (size_t i = 0; i < s.length(); i++) {
    uint8_t c = (uint8_t)s[i];
    bool unreserved = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                      (c >= '0' && c <= '9') || c == '-' || c == '_' ||
                      c == '.' || c == '~';
    if (unreserved) {
      out += (char)c;
    } else {
      out += '%';
      out += hex[c >> 4];
      out += hex[c & 0x0F];
    }
  }
  return out;
}

String randomHex(int bytes) {
  const char* hex = "0123456789abcdef";
  String out;
  for (int i = 0; i < bytes * 2; i++) out += hex[random(16)];
  return out;
}

// Pull a value that follows a marker, e.g. after ["c"," up to the next quote.
String extractAfter(const String& body, const String& marker) {
  int i = body.indexOf(marker);
  if (i < 0) return "";
  i += marker.length();
  int j = body.indexOf('"', i);
  if (j < 0) return "";
  return body.substring(i, j);
}

// ===========================================================================
// 1) Search the YouTube Data API for a random batch of a genre
// ===========================================================================
bool searchGenre(const Genre& g) {
  videoCount = 0;

  const char* q;
  if (g.dayBased) {
    const char** dq; int dc; const char* dayName;
    devotionalForToday(dq, dc, dayName);
    q = dq[random(dc)];
    Serial.printf("[search] devotional day = %s\n", dayName);
  } else {
    q = g.queries[random(g.queryCount)];
  }
  // Vary ordering so repeated presses of the same query still differ.
  const char* orders[] = { "relevance", "date", "viewCount", "rating" };
  const char* order = orders[random(4)];

  String url = "https://www.googleapis.com/youtube/v3/search";
  url += "?part=snippet&type=video&maxResults=25";
  url += "&videoEmbeddable=true&regionCode=IN&relevanceLanguage=te";
  url += "&order=";  url += order;
  url += "&q=";      url += urlEncode(String(q));
  if (g.duration) { url += "&videoDuration="; url += g.duration; }
  url += "&fields=items(id/videoId)";   // tiny response: just the ids
  url += "&key=";    url += YT_API_KEY;

  Serial.printf("[search] genre=%s  q=\"%s\"  order=%s\n", g.name, q, order);

  // Try up to 3 times - the ESP32's TLS stack sometimes fails the first
  // connect (HTTP -1) and succeeds on a retry.
  int code = -1;
  String body;
  for (int attempt = 1; attempt <= 3 && code != 200; attempt++) {
    WiFiClientSecure client;
    client.setInsecure();               // home project: skip cert pinning
    HTTPClient http;
    http.begin(client, url);
    http.setConnectTimeout(8000);
    http.setTimeout(8000);
    code = http.GET();
    Serial.printf("[search] attempt %d -> HTTP %d\n", attempt, code);
    if (code == 200) body = http.getString();
    else             body = http.getString();  // capture any error text
    http.end();
    if (code != 200) { Serial.println(body.substring(0, 200)); delay(500); }
  }
  if (code != 200) return false;

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, body);
  if (err) {
    Serial.printf("[search] json error: %s\n", err.c_str());
    return false;
  }

  for (JsonObject item : doc["items"].as<JsonArray>()) {
    const char* id = item["id"]["videoId"];
    if (id && videoCount < MAX_IDS) videoIds[videoCount++] = String(id);
  }
  Serial.printf("[search] got %d videos\n", videoCount);

  // Shuffle (Fisher-Yates) so the first video isn't always the same.
  for (int i = videoCount - 1; i > 0; i--) {
    int j = random(i + 1);
    String t = videoIds[i]; videoIds[i] = videoIds[j]; videoIds[j] = t;
  }
  return videoCount > 0;
}

// ===========================================================================
// 2) Cast the queue to the TV via the YouTube Lounge API
// ===========================================================================

// Step 2a: screen id -> lounge token
String getLoungeToken() {
  WiFiClientSecure client; client.setInsecure();
  HTTPClient http;
  http.begin(client, "https://www.youtube.com/api/lounge/pairing/get_lounge_token_batch");
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  String body = "screen_ids=" + String(YT_SCREEN_ID);
  int code = http.POST(body);
  String resp = http.getString();
  http.end();
  Serial.printf("[lounge] token HTTP %d\n", code);
  if (code != 200) { Serial.println(resp.substring(0, 300)); return ""; }

  JsonDocument doc;
  if (deserializeJson(doc, resp)) return "";
  const char* tok = doc["screens"][0]["loungeToken"];
  String token = tok ? String(tok) : "";
  Serial.printf("[lounge] token=%s\n", token.c_str());
  return token;
}

// Step 2b: bind to get a session (SID + gsessionid)
bool bindSession(const String& token, String& sid, String& gsession) {
  WiFiClientSecure client; client.setInsecure();
  HTTPClient http;
  String url = "https://www.youtube.com/api/lounge/bc/bind";
  url += "?RID=1&VER=8&CVER=1&auth_failure_option=send_error";
  url += "&device=REMOTE_CONTROL&app=youtube-desktop&mdx-version=3";
  url += "&loungeIdToken=" + token;
  url += "&id=" + deviceId + "&name=EasyWatch&v=2&t=1";
  url += "&zx=" + randomHex(6);
  http.begin(client, url);
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  int code = http.POST("count=0");
  String resp = http.getString();
  http.end();
  Serial.printf("[lounge] bind HTTP %d\n", code);
  if (code != 200) { Serial.println(resp.substring(0, 300)); return false; }

  sid      = extractAfter(resp, "[\"c\",\"");
  gsession = extractAfter(resp, "[\"S\",\"");
  Serial.printf("[lounge] SID=%s  gsession=%s\n", sid.c_str(), gsession.c_str());
  return sid.length() && gsession.length();
}

// Step 2c: send setPlaylist with the whole queue
bool setPlaylist(const String& token, const String& sid, const String& gsession) {
  String csv;
  for (int i = 0; i < videoCount; i++) { if (i) csv += ","; csv += videoIds[i]; }

  WiFiClientSecure client; client.setInsecure();
  HTTPClient http;
  String url = "https://www.youtube.com/api/lounge/bc/bind";
  url += "?RID=2&VER=8&CVER=1&device=REMOTE_CONTROL&app=youtube-desktop";
  url += "&loungeIdToken=" + token;
  url += "&id=" + deviceId + "&name=EasyWatch&v=2&t=1";
  url += "&SID=" + sid + "&gsessionid=" + gsession;
  url += "&zx=" + randomHex(6);
  http.begin(client, url);
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");

  String body = "count=1&ofs=0";
  body += "&req0__sc=setPlaylist";
  body += "&req0_listId=";
  body += "&req0_currentTime=0";
  body += "&req0_currentIndex=0";
  body += "&req0_audioOnly=false";
  body += "&req0_videoId=" + videoIds[0];
  body += "&req0_videoIds=" + csv;

  int code = http.POST(body);
  String resp = http.getString();
  http.end();
  Serial.printf("[lounge] setPlaylist HTTP %d\n", code);
  if (code != 200) Serial.println(resp.substring(0, 300));
  return code == 200;
}

bool cast() {
  String token = getLoungeToken();
  if (!token.length()) return false;
  String sid, gsession;
  if (!bindSession(token, sid, gsession)) return false;
  return setPlaylist(token, sid, gsession);
}

// ===========================================================================
// 3) Make sure the YouTube app is open on the TV (DIAL) - cold start
// ===========================================================================
#define DIAL_PORT 8080
String dialBasePath = "/ws/apps/YouTube";   // Samsung path; fallback below
bool   waitingForYT = false;

// Returns "running", "stopped", or "" (TV/endpoint unreachable).
String dialState() {
  const char* candidates[] = { "/ws/apps/YouTube", "/apps/YouTube" };
  for (int c = 0; c < 2; c++) {
    String url = "http://" + String(TV_IP) + ":" + String(DIAL_PORT) + candidates[c];
    HTTPClient http;
    http.begin(url);
    http.setConnectTimeout(1500);        // fail fast if the TV isn't answering
    http.setTimeout(1500);
    int code = http.GET();
    String body = http.getString();
    http.end();
    if (code == 200) {
      dialBasePath = candidates[c];
      if (body.indexOf("<state>running") >= 0) return "running";
      return "stopped";
    }
  }
  return "";
}

bool dialLaunch() {
  String url = "http://" + String(TV_IP) + ":" + String(DIAL_PORT) + dialBasePath;
  HTTPClient http;
  http.begin(url);
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  http.addHeader("Content-Length", "0");   // Samsung requires this (else 411)
  int code = http.POST((uint8_t*)"", 0);   // empty launch: just open YouTube
  http.end();
  Serial.printf("[dial] launch HTTP %d\n", code);
  return code == 200 || code == 201;
}

// Start opening YouTube (don't block). Called before the search.
void prepYouTube() {
  String st = dialState();
  Serial.printf("[dial] YouTube state=%s\n", st.length() ? st.c_str() : "unreachable");
  if (st == "running") { waitingForYT = false; return; }
  if (st == "")        { waitingForYT = false; return; } // can't reach TV; try cast anyway
  dialLaunch();
  waitingForYT = true;
}

// Finish waiting until YouTube reports running (called after the search).
bool finishYouTube() {
  if (!waitingForYT) return true;
  for (int i = 0; i < 25; i++) {
    delay(1000);
    if (dialState() == "running") {
      Serial.println("[dial] YouTube is up");
      delay(2500);        // let it connect to the lounge before we cast
      return true;
    }
  }
  Serial.println("[dial] timed out waiting for YouTube to open");
  return false;
}

// ===========================================================================
// Main flow
// ===========================================================================
void playGenre(const Genre& g) {
  Serial.printf("\n=== %s ===\n", g.name);
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[wifi] not connected, reconnecting...");
    WiFi.reconnect();
    return;
  }
  prepYouTube();                                        // start opening YouTube
  if (!searchGenre(g)) { Serial.println("[play] search failed"); return; }
  finishYouTube();                                      // wait if it was closed
  if (!cast())         { Serial.println("[play] cast failed");   return; }
  Serial.println("[play] casting - it should start on the TV now.");
}

void handleCode(uint32_t code) {
  switch (code) {
    case CODE_RED:    playGenre(GENRE_RED);    break;
    case CODE_GREEN:  playGenre(GENRE_GREEN);  break;
    case CODE_YELLOW: playGenre(GENRE_YELLOW); break;
    case CODE_BLUE:   playGenre(GENRE_BLUE);   break;
    default:
      Serial.printf("[ir] unmapped code 0x%08X (ignored)\n", code);
      break;
  }
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\nEasyWatch ESP32 firmware starting...");

  randomSeed(esp_random());

  // Stable device id for the Lounge session (kept across reboots).
  prefs.begin("easywatch", false);
  deviceId = prefs.getString("devid", "");
  if (!deviceId.length()) {
    deviceId = randomHex(16);
    prefs.putString("devid", deviceId);
  }
  Serial.printf("[id] device=%s\n", deviceId.c_str());

  IrReceiver.begin(IR_RECEIVE_PIN, ENABLE_LED_FEEDBACK);

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);   // silently rejoin Wi-Fi if it ever drops (24/7)
  WiFi.persistent(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("[wifi] connecting");
  for (int i = 0; i < 40 && WiFi.status() != WL_CONNECTED; i++) {
    delay(500); Serial.print(".");
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("[wifi] connected, IP %s\n", WiFi.localIP().toString().c_str());
    // IST = UTC+5:30 (19800 s). Needed so devotional picks the right deity.
    configTime(19800, 0, "pool.ntp.org", "time.google.com");
    struct tm t;
    if (getLocalTime(&t, 5000))
      Serial.printf("[time] %04d-%02d-%02d  weekday=%d\n",
                    t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_wday);
    else
      Serial.println("[time] NTP not synced yet (devotional falls back to Saturday)");
  } else {
    Serial.println("[wifi] NOT connected - check SSID/password (2.4GHz only)");
  }

  Serial.println("Ready. Point the remote and press a colour button.");
}

void loop() {
  if (IrReceiver.decode()) {
    auto& d = IrReceiver.decodedIRData;
    uint32_t code = d.decodedRawData;
    bool isRepeat = d.flags & IRDATA_FLAGS_IS_REPEAT;

    if (!isRepeat && code != 0) {
      unsigned long now = millis();
      if (!(code == lastCode && now - lastCodeAt < DEBOUNCE_MS)) {
        lastCode = code; lastCodeAt = now;
        Serial.printf("[ir] code 0x%08X\n", code);
        handleCode(code);
      }
    }
    IrReceiver.resume();
  }
}
