// Ammamma TV: colour key -> the TV's own YouTube app plays a queue of random videos of that genre.
// YouTube's embedded player no longer runs on this 2019 TV's browser, so we open the real YouTube
// app and cast the queue to it through the YouTube lounge (the same path phones use).
'use strict';

var API_KEY = (window.CONFIG && window.CONFIG.API_KEY) || '';

// The TV's YouTube app, paired once with "Link with TV code". The screen id is permanent.
var SCREEN_ID = (window.CONFIG && window.CONFIG.SCREEN_ID) || '';
var YOUTUBE_APP = (window.CONFIG && window.CONFIG.YOUTUBE_APP) || 'com.samsung.tv.cobalt-yt';

var KEY = { RED: 403, GREEN: 404, YELLOW: 405, BLUE: 406, BACK: 10009 };

var QUEUE_SIZE = 25;

// Each genre: random searches, minimum length in seconds, and the tile index in the menu.
var GENRES = {
  red: {
    tile: 0, label: 'తెలుగు కామెడీ', minSeconds: 180, duration: 'any',
    queries: [
      'telugu comedy scenes', 'brahmanandam comedy scenes', 'ali comedy scenes telugu',
      'sunil comedy scenes telugu', 'venu madhav comedy scenes', 'ms narayana comedy scenes',
      'kota babu mohan comedy scenes', 'telugu back to back comedy scenes',
      'allari naresh comedy scenes', 'rajendra prasad comedy scenes', 'jabardasth comedy'
    ]
  },
  green: {
    tile: 1, label: 'సౌత్ కామెడీ', minSeconds: 180, duration: 'any',
    queries: [
      'vadivelu comedy scenes', 'goundamani senthil comedy', 'santhanam comedy scenes',
      'vivek comedy scenes', 'yogi babu comedy scenes', 'tamil comedy scenes',
      'malayalam comedy scenes', 'kannada comedy scenes', 'south indian comedy scenes telugu dubbed'
    ]
  },
  yellow: {
    tile: 2, label: 'సినిమాలు', minSeconds: 3600, duration: 'long',
    queries: [
      'telugu full movie', 'old telugu full movie', 'telugu family full movie',
      'telugu super hit full movie', 'ntr old telugu full movie', 'anr old telugu full movie',
      'chiranjeevi telugu full movie', 'venkatesh telugu full movie', 'telugu comedy full movie'
    ]
  },
  blue: {
    tile: 3, label: 'దేవుడి పాటలు', minSeconds: 180, duration: 'any',
    queries: [
      'telugu devotional songs', 'venkateswara swamy songs telugu', 'shiva devotional songs telugu',
      'sai baba songs telugu', 'hanuman chalisa telugu', 'ayyappa swamy songs telugu',
      'ammavari songs telugu', 'annamayya keerthanalu', 'sri rama songs telugu', 'ganesh songs telugu'
    ]
  }
};

var HISTORY_MAX = 3000;
var POOL_LOW = 30;

var busy = false;
var refilling = {};

// ---------- storage ----------
function load(name, fallback) {
  try { var v = localStorage.getItem(name); return v ? JSON.parse(v) : fallback; } catch (e) { return fallback; }
}
function save(name, value) {
  try { localStorage.setItem(name, JSON.stringify(value)); } catch (e) {}
}
var watched = load('watched', []);
var pools = load('pools', { red: [], green: [], yellow: [], blue: [] });

function markWatched(ids) {
  watched = watched.concat(ids);
  if (watched.length > HISTORY_MAX) watched = watched.slice(-HISTORY_MAX);
  save('watched', watched);
}

// ---------- helpers ----------
function pick(arr) { return arr[Math.floor(Math.random() * arr.length)]; }

function shuffle(arr) {
  for (var i = arr.length - 1; i > 0; i--) {
    var j = Math.floor(Math.random() * (i + 1)); var t = arr[i]; arr[i] = arr[j]; arr[j] = t;
  }
  return arr;
}

function wait(ms) { return new Promise(function (r) { setTimeout(r, ms); }); }

function form(obj) {
  return Object.keys(obj).map(function (k) { return encodeURIComponent(k) + '=' + encodeURIComponent(obj[k]); }).join('&');
}

function request(method, url, body, contentType) {
  return fetch(url, {
    method: method,
    headers: body != null ? { 'Content-Type': contentType || 'application/x-www-form-urlencoded' } : {},
    body: body
  }).then(function (r) {
    return r.text().then(function (t) {
      if (!r.ok) throw new Error('HTTP ' + r.status + ' ' + url);
      return t;
    });
  });
}

function isoSeconds(iso) {
  var m = /PT(?:(\d+)H)?(?:(\d+)M)?(?:(\d+)S)?/.exec(iso || '');
  if (!m) return 0;
  return (+m[1] || 0) * 3600 + (+m[2] || 0) * 60 + (+m[3] || 0);
}

// ---------- YouTube Data API: random videos per genre ----------
// One random search (100 quota units) + one details lookup (1 unit), giving up to 50 fresh videos.
function refill(genreKey) {
  if (refilling[genreKey]) return refilling[genreKey];
  var g = GENRES[genreKey];
  var params = {
    part: 'snippet', type: 'video', maxResults: 50, safeSearch: 'strict',
    videoEmbeddable: 'true', regionCode: 'IN', videoDuration: g.duration,
    order: pick(['relevance', 'relevance', 'viewCount', 'rating']),
    q: pick(g.queries), key: API_KEY
  };
  // Half the time, limit to a random year so results vary between searches.
  if (Math.random() < 0.5) {
    var year = 2012 + Math.floor(Math.random() * (new Date().getFullYear() - 2012));
    params.publishedAfter = year + '-01-01T00:00:00Z';
    params.publishedBefore = (year + 1) + '-01-01T00:00:00Z';
  }

  refilling[genreKey] = request('GET', 'https://www.googleapis.com/youtube/v3/search?' + form(params))
    .then(function (text) {
      var ids = (JSON.parse(text).items || []).map(function (it) { return it.id.videoId; }).filter(function (id) {
        return id && watched.indexOf(id) < 0 && pools[genreKey].indexOf(id) < 0;
      });
      if (!ids.length) return [];
      return request('GET', 'https://www.googleapis.com/youtube/v3/videos?' +
        form({ part: 'contentDetails,status', id: ids.join(','), key: API_KEY })).then(function (t) {
        return (JSON.parse(t).items || []).filter(function (v) {
          var rr = v.contentDetails.regionRestriction;
          return isoSeconds(v.contentDetails.duration) >= g.minSeconds &&
            !(rr && rr.blocked && rr.blocked.indexOf('IN') >= 0) &&
            !(rr && rr.allowed && rr.allowed.indexOf('IN') < 0);
        }).map(function (v) { return v.id; });
      });
    })
    .then(function (fresh) {
      pools[genreKey] = pools[genreKey].concat(shuffle(fresh));
      save('pools', pools);
      refilling[genreKey] = null;
      return fresh.length;
    }, function (err) { refilling[genreKey] = null; throw err; });
  return refilling[genreKey];
}

// Up to QUEUE_SIZE unwatched videos of the genre, searching again (at most 3 times) if the pool is short.
function takeVideos(genreKey, attempts) {
  attempts = attempts || 0;
  pools[genreKey] = pools[genreKey].filter(function (id) { return watched.indexOf(id) < 0; });
  var pool = pools[genreKey];
  if (pool.length >= QUEUE_SIZE || (attempts >= 3 && pool.length)) {
    var ids = pool.splice(0, QUEUE_SIZE);
    save('pools', pools);
    if (pool.length < POOL_LOW) refill(genreKey).catch(function () {});
    return Promise.resolve(ids);
  }
  if (attempts >= 3) return Promise.reject(new Error('no videos'));
  return refill(genreKey).then(function () { return takeVideos(genreKey, attempts + 1); });
}

// ---------- open the TV's YouTube app ----------
function youTubeRunning() {
  return new Promise(function (resolve) {
    tizen.application.getAppsContext(function (apps) {
      resolve(apps.some(function (a) { return a.appId === YOUTUBE_APP; }));
    }, function () { resolve(false); });
  });
}

// Brings YouTube to the front, starting it if needed. Resolves once it can take a cast.
function openYouTube() {
  return youTubeRunning().then(function (wasRunning) {
    return new Promise(function (resolve, reject) {
      tizen.application.launch(YOUTUBE_APP, resolve, reject);
    }).then(function () {
      if (wasRunning) return wait(1500);
      var start = Date.now();
      function poll() {
        return youTubeRunning().then(function (running) {
          if (running) return wait(8000);
          if (Date.now() - start > 40000) throw new Error('YouTube did not start');
          return wait(1000).then(poll);
        });
      }
      return poll();
    });
  });
}

// ---------- Lounge: cast the queue ----------
function cast(ids) {
  return request('POST', 'https://www.youtube.com/api/lounge/pairing/get_lounge_token_batch', form({ screen_ids: SCREEN_ID }))
    .then(function (t) {
      var token = JSON.parse(t).screens[0].loungeToken;
      var common = {
        device: 'REMOTE_CONTROL', app: 'youtube-desktop', name: 'Ammamma TV',
        id: 'ammamma-' + Math.random().toString(36).slice(2), loungeIdToken: token, VER: 8, CVER: 1, v: 2
      };
      var base = 'https://www.youtube.com/api/lounge/bc/bind?';
      var first = Object.assign({ RID: 1, zx: Math.random().toString(36).slice(2), t: 1 }, common);
      return request('POST', base + form(first), 'count=0').then(function (res) {
        var sid = (/\["c","([^"]+)"/.exec(res) || [])[1];
        var gs = (/\["S","([^"]+)"\]/.exec(res) || [])[1] || '';
        if (!sid) throw new Error('lounge bind failed');
        var second = Object.assign({ RID: 2, SID: sid, gsessionid: gs, AID: 0 }, common);
        return request('POST', base + form(second), form({
          count: 1, ofs: 0,
          req0__sc: 'setPlaylist', req0_videoId: ids[0], req0_currentTime: 0,
          req0_currentIndex: 0, req0_videoIds: ids.join(','), req0_listId: ''
        }));
      });
    });
}

// ---------- UI ----------
function show(id, on) { document.getElementById(id).classList.toggle('hidden', !on); }

function setStatus(text) {
  var el = document.getElementById('status');
  el.textContent = text || '';
  show('status', !!text);
}

function showLoading(genreKey) {
  var tile = document.querySelectorAll('#menu .tile')[GENRES[genreKey].tile];
  var c = document.getElementById('loadingCircle');
  c.className = 'circle ' + genreKey;
  c.innerHTML = tile.querySelector('svg').outerHTML;
  document.getElementById('loadingText').textContent = GENRES[genreKey].label;
  show('menu', false);
  show('loading', true);
}

function showMenu() {
  show('loading', false);
  show('menu', true);
}

function playGenre(genreKey) {
  if (busy) return;
  busy = true;
  setStatus('');
  showLoading(genreKey);
  var ids;
  takeVideos(genreKey)
    .then(function (v) { ids = v; return openYouTube(); })
    .then(function () { return cast(ids); })
    // One retry: a freshly started YouTube sometimes needs a little longer before it accepts a cast.
    .catch(function () { return wait(5000).then(function () { return cast(ids); }); })
    .then(function () {
      markWatched(ids);
      busy = false;
      // YouTube is in front now; have the menu ready for when she comes back.
      setTimeout(showMenu, 3000);
    }, function () {
      if (ids) pools[genreKey] = ids.concat(pools[genreKey]);
      busy = false;
      showMenu();
      setStatus('ఇంకోసారి నొక్కండి');   // "press again"
    });
}

// ---------- remote ----------
function registerKeys() {
  ['ColorF0Red', 'ColorF1Green', 'ColorF2Yellow', 'ColorF3Blue'].forEach(function (k) {
    try { tizen.tvinputdevice.registerKey(k); } catch (e) {}
  });
}

document.addEventListener('keydown', function (e) {
  switch (e.keyCode) {
    case KEY.RED: playGenre('red'); break;
    case KEY.GREEN: playGenre('green'); break;
    case KEY.YELLOW: playGenre('yellow'); break;
    case KEY.BLUE: playGenre('blue'); break;
    case KEY.BACK:
      try { tizen.application.getCurrentApplication().exit(); } catch (err) {}
      break;
    default: return;
  }
  e.preventDefault();
});

registerKeys();
// Warm up all four pools so the first press starts quickly.
Object.keys(GENRES).forEach(function (k) { if (pools[k].length < POOL_LOW) refill(k).catch(function () {}); });
