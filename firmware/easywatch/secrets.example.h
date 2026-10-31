#pragma once
// ---------------------------------------------------------------------------
// Copy this file to  secrets.h  and fill in your own values.
// secrets.h is gitignored, so your Wi-Fi password and API key never get
// committed. Only this example (with placeholders) is in the repo.
// ---------------------------------------------------------------------------

// Your 2.4GHz Wi-Fi (the ESP32 cannot use 5GHz). Must be the SAME network
// the TV is on.
#define WIFI_SSID      "YOUR_WIFI_NAME"
#define WIFI_PASSWORD  "YOUR_WIFI_PASSWORD"

// YouTube Data API v3 key (Google Cloud Console).
#define YT_API_KEY     "YOUR_YOUTUBE_API_KEY"

// The TV's YouTube "screen id" from pairing once with "Link with TV code".
#define YT_SCREEN_ID   "YOUR_SCREEN_ID"

// The TV's IP address, used to launch YouTube (DIAL) from a cold start.
// Give the TV a fixed IP in your router so this never changes.
#define TV_IP          "YOUR_TV_IP"
