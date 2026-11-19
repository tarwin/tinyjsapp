---
title: "system"
---

# system

<p class="tagline"><code>tiny.system.*</code> · <code>tiny.theme.*</code> · <code>tiny.app.power</code> — the machine your app runs on</p>

Facts about the computer rather than things your app does: which OS and desktop it is, light or dark mode, when it sleeps and wakes, how long the user has been away, the battery, Wi-Fi, and the user's language and time zone — plus keeping the computer awake while your app works.

The backend has the same calls under the same names — `app.system.idleTime()`, `app.theme.get()`, `app.power.preventSleep()` — and gets changes through its [`onSystem` and `onLocale` hooks](#hooks) instead of page listeners. Asking what this machine supports (`capabilities()`), the CPU (`architecture()`) and missing Linux packages (`requirements()`, `missing()`, `promptMissing()`) are on [portability](./portability).

## which machine {#info}

```js
tiny.system.os();         // 'macos' | 'windows' | 'linux' — synchronous
tiny.system.isMacOS();    // also isWindows(), isLinux()

await tiny.system.info();
// { os: 'linux', arch: 'x86_64', processArch: 'x86_64', session: 'wayland', desktop: 'ubuntu:GNOME' }
```

`os()` and the `is…()` checks answer immediately, so they're safe while the page is setting up. `info()` also has the CPU (`'arm64'` or `'x86_64'`) twice: `arch` is the machine's chip and `processArch` is what the app runs as. They differ only when an x86_64 build is emulated (Rosetta, or Windows on ARM); see [portability](./portability). It also has, on Linux only, the display server (`session`: `'x11'` or `'wayland'`) and the desktop (`desktop`, from `XDG_CURRENT_DESKTOP`). Both are `null` on macOS and Windows.

## light and dark {#theme}

```js
const t = await tiny.theme.get();   // { dark: true } | null
const off = tiny.theme.on((dark) => document.body.classList.toggle('dark', dark));
```

`get()` is the system's current appearance — `null` only in the moment before the launcher first reports it at startup. `on(fn)` calls `fn(dark)` each time the user switches, and returns a function that stops listening. All three OSes report it: the appearance setting on macOS, the app theme setting on Windows, and the desktop's colour-scheme preference on Linux.

## sleep and wake {#sleep-wake}

```js
tiny.api.on('sleep', () => pauseSync());
tiny.api.on('wake', () => resumeSync());
```

The page gets `'sleep'` as the computer goes to sleep and `'wake'` when it comes back, on every OS. There's no `tiny.theme`-style helper; listen with [`tiny.api.on`](./api). Use `wake` to reconnect sockets and refresh anything time-based: timers don't run while the computer sleeps.

## keeping the computer awake {#power}

```js
await tiny.app.power.preventSleep('Exporting video');             // the system stays awake
await tiny.app.power.preventSleep('Presenting', { display: true }); // the screen stays on too
await tiny.app.power.allowSleep();
```

Stops the computer from going to sleep because nobody is using it, while a long export, download or playback runs. Both calls resolve `true` if the OS accepted the request and `false` if it didn't; they don't reject. Each `preventSleep()` replaces the previous one, so there's only ever one request, and `allowSleep()` releases it.

The request ends with the app, even if it crashes, so a crashed app can't leave the computer awake — unlike spawning `caffeinate`. The reason string is shown where the OS lists what's keeping it awake: `pmset -g assertions` on macOS, `systemd-inhibit --list` on Linux.

| | how |
|---|---|
| macOS | a power assertion against idle sleep (idle display sleep with `display: true`) |
| Windows | `SetThreadExecutionState`, system (and display) required |
| Linux | a logind inhibitor, which blocks suspend altogether, not only idle sleep; `display: true` also inhibits the screensaver |

## idle time {#idle}

```js
if (await tiny.system.idleTime() > 300) pausePolling();   // seconds since the last input
```

Seconds since the user last touched the keyboard or mouse, anywhere on the computer. On Linux it's read from GNOME's idle monitor, so on other desktops it's always `0`.

## battery {#battery}

```js
const b = await tiny.system.battery();
// { percent: 82, charging: false, plugged: false, minutesRemaining: 214 } | null
```

`null` on a computer without a battery. `minutesRemaining` is the time to full while charging and to empty otherwise, and `null` while the OS is still estimating — and always `null` on Linux. `percent` can be `null` on Windows and Linux when the OS doesn't report it.

## Wi-Fi {#wifi}

```js
const w = await tiny.system.wifi();
// { ssid, bssid, rssi, noise, txRate } | null
```

The current Wi-Fi connection: signal (`rssi`) and `noise` in dBm, and `txRate` in Mbps. It's `null` when Wi-Fi is off. macOS only for now — Windows and Linux always answer `null`, and `capabilities().wifi` is `false` there. On macOS 14 and later, `ssid` and `bssid` are `null` unless the app has the Location permission. To get it, declare why in tinyjs.json and ask once — the prompt only appears in a packaged app:

```json
{ "permissions": { "location": "To show which Wi-Fi network you're on" } }
```

```js
if (await tiny.app.permissions.request('location') === 'granted') {
  const ssid = (await tiny.system.wifi())?.ssid;   // wifi() is null with Wi-Fi off
}
```

See [permissions](./permissions#names).

## language and region {#locale}

```js
const loc = await tiny.system.locale();
// { language: 'en-AU', languages: ['en-AU', 'en'], system: ['en-AU', 'fr-FR'],
//   region: 'AU', timeZone: 'Australia/Melbourne' } | null

tiny.api.on('locale', (loc) => relabel(loc.language));   // the user changed it
```

Read from the OS settings, not from environment variables like `LANG`.

- `language` is the best match for what the app should show, and `languages` the user's preferences in order — limited to the languages the app itself declares.
- `system` is the user's full list, whether or not the app speaks those languages. The two differ for an English-only app on a French Mac: `languages` is `['en']`, `system` starts with `'fr-FR'`.
- `region` is the country code, or `null` if none is set; `timeZone` an IANA name.

A page often doesn't need this: `navigator.language`, `Intl` and the window's `languagechange` event work in the webview. It's for the backend, which has no `Intl`, and for when you want the user's own list rather than the app's.

macOS only for now: Windows and Linux answer `null`, and `capabilities().locale` is `false` there.

## backend hooks {#hooks}

The backend hears the same changes through exports of its module (see [backend hooks](./backend#hooks)):

```js
export function onSystem(kind, value, app) {
  if (kind === 'theme') console.log('dark mode:', value === 'dark');
  if (kind === 'wake') reconnect();
}

export function onLocale(info, app) {
  // info has the same shape as tiny.system.locale()
}
```

| `kind` | `value` | when |
|---|---|---|
| `'theme'` | `'dark'` or `'light'` | the appearance changes |
| `'sleep'` | `null` | the computer is going to sleep |
| `'wake'` | `null` | it woke up |
| `'screens'` | `null` | a screen was connected, removed or rearranged — read the new list with `app.screens()` (see [window](./window)) |

`onLocale` fires on macOS only.
