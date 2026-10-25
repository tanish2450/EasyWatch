// Copy this file to config.js and fill in your own values.
// config.js is gitignored so your keys never get committed.
window.CONFIG = {
  // YouTube Data API v3 key — make one at https://console.cloud.google.com/
  // (create a project, enable "YouTube Data API v3", then Credentials > API key).
  API_KEY: 'YOUR_YOUTUBE_API_KEY',

  // The TV's YouTube "screen id". Get it by pairing once:
  // on the TV open YouTube > Settings > "Link with TV code", then run the
  // pairing helper in tools/ with that 12-digit code (see the README).
  SCREEN_ID: 'YOUR_SCREEN_ID',

  // Tizen app id of the TV's built-in YouTube app. 'com.samsung.tv.cobalt-yt'
  // on most recent Samsung sets; list apps with sdb to confirm yours.
  YOUTUBE_APP: 'com.samsung.tv.cobalt-yt'
};
