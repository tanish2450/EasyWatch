# EasyWatch

**One button on the TV remote, and my grandmother's favourite videos just start playing.**

No reading. No scrolling through YouTube. No picking an account. No typing a search.
She presses a colour, and Telugu comedy (or devotional songs, or old movies) fills the screen.

---

## The story

It started because I wanted to *clean* my TV.

I'd seen the [CleanYourTV](https://github.com/Gathin23/cleanyourtv) project — strip the ads and
bloatware off a smart TV and drop a clean launcher on it. I turned on developer mode on our
Samsung 55" (a 2019 RU7100), added my PC, and tried to run it.

It doesn't work on Samsung. Those TVs run Tizen, not Android, so there's no debug bridge to connect
to, no way to disable the built-in apps, and no way to replace the Samsung home screen. Dead end.

But while I was poking at it, the real problem came back to me — the one that actually mattered.

**My grandmother can't read, and she can't navigate the remote.** When she's alone she gets bored,
and turning on something she likes means someone else has to open YouTube, choose a profile, search,
and pick a video for her. She can't do that chain of steps by herself.

So I changed the goal. Forget debloating. **Could I make it so one button plays her kind of videos?**

### What I learned the hard way

- Samsung's "developer mode" is a sandbox for installing your own test apps. That turned out to be
  exactly enough. I installed Samsung's Tizen tools, made a signing certificate tied to our TV, and
  built a little full-screen app.
- My first version embedded YouTube's player right inside the app. It refused to run — this 2019 TV's
  browser is too old (`queueMicrotask is not defined`), and the embedded player just errored out.
- So I switched approaches completely. Instead of *playing* video myself, the app **casts** to the
  TV's own YouTube app — the same thing your phone does when you tap the cast icon. That path (the
  YouTube "lounge" API) works perfectly, even starting from a cold, closed YouTube.

### How it works now

1. Grandma opens EasyWatch and presses a colour button on the remote.
2. The app asks the YouTube Data API for a **random** batch of videos in that genre (it shuffles,
   skips anything she's seen recently, and filters for length and region).
3. It opens the TV's real YouTube app and **casts a queue of ~25 videos** to it.
4. They play full-screen, one after another, with no menus and no account picker.

| Button | Plays |
|--------|-------|
| 🔴 Red | Telugu comedy scenes |
| 🟢 Green | South Indian comedy |
| 🟡 Yellow | Full movies |
| 🔵 Blue | Devotional songs |

Because every batch is a fresh random search, she doesn't get the same loop of videos over and over.

---

## Set it up on your own TV

You need a Samsung Tizen TV, a computer on the same Wi-Fi, and about an hour.

1. **Turn on Developer Mode** on the TV (Apps → type `12345` → turn it on, enter your PC's IP).
2. **Install the Tizen tools** (Samsung's free SDK) and create a **Samsung signing certificate** that
   includes your TV's device id.
3. **Get a YouTube Data API key** — free, from the Google Cloud Console (enable *YouTube Data API v3*).
4. **Pair with your TV's YouTube**: on the TV, YouTube → Settings → *Link with TV code*, and use the
   12-digit code with the pairing helper to get your `SCREEN_ID`.
5. `cp config.example.js config.js` and fill in your API key and screen id.
6. Package, sign and install:
   ```sh
   tizen package -t wgt -s <your-cert-profile> -- .
   tizen install -n EasyWatch.wgt -s <tv-ip>:26101
   ```

> **Note on secrets:** `config.js` holds your API key and pairing id and is **gitignored on purpose**.
> Never commit it. Only `config.example.js` (placeholders) is in the repo.

---

## What's next

The colour buttons work, but a TV remote still has a hundred buttons on it. The next step is a little
box with **four big physical buttons** she can't get wrong — built on an **ESP32** with an infrared
receiver, reusing an old set-top-box remote. The casting already works over Wi-Fi, so any device on
the network can trigger it, including a $5 microcontroller. That's the version I actually want in her
hands.

---

## Files

| File | What it is |
|------|------------|
| `index.html` | The four-circle picture menu |
| `style.css` | Layout and the genre colours |
| `main.js` | Search, shuffle, open YouTube, cast the queue |
| `config.example.js` | Template for your keys (copy to `config.js`) |
| `config.xml` | Tizen app manifest |

---

Built for my grandmother. If it helps someone else's, that's even better.
