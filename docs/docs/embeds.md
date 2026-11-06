---
title: "embeds"
---

# embeds

<p class="tagline">tiny.embedURL — YouTube and other players in an iframe</p>

A YouTube iframe in an app page doesn't play. On macOS it shows **Error 153** ("Video player configuration error"), and on Windows the frame shows a browser error page instead. YouTube needs a `Referer` to run, and the webview sends none from a `file://` page. `referrerpolicy` can't change that.

`tiny.embedURL()` gives you a URL that works instead:

```html
<iframe id="player" width="640" height="360"
        allow="autoplay; encrypted-media; fullscreen; picture-in-picture"
        allowfullscreen></iframe>
```

```js
const iframe = document.getElementById('player');
iframe.src = await tiny.embedURL('https://www.youtube.com/embed/tgbNymZ7vqY');
```

Use the player's embed URL, not its watch page. Keep the `allow` attribute on your iframe: it decides what the player may do, including autoplay and fullscreen.

From the backend, `app.embedURL(url)` returns the same URL without the `await`.

## how it works {#how}

The backend serves a small page on `127.0.0.1` that frames the player, and your iframe loads that page. The player sees a `Referer` of `http://127.0.0.1:<port>/`. Your own page stays on `file://` and keeps its full `tiny` access.

- The server starts on the first call, on a free port the OS picks.
- It only listens on loopback, so other machines can't reach it.
- Each URL has a random id, so it only serves what your app asked for.

The port changes every launch, so call `embedURL` each time and don't save the result.

## allowed hosts {#hosts}

`embedURL` takes plain `https` URLs on `www.youtube.com`, `www.youtube-nocookie.com` or `player.vimeo.com`, and rejects anything else. Add other players with [`embedHosts`](./config#embedHosts):

```json
{ "embedHosts": ["open.spotify.com"] }
```

## limits {#limits}

- **It relies on the player accepting a loopback `Referer`.** YouTube does today, but that's its decision and it could change.
- **A strict CSP on your page has to allow the frame**: `frame-src http://127.0.0.1:*`.
- **The call goes through the [`api`](./config#api) gate.** If your `tinyjs.json` has an `api` block, add `embed.url` to its `enable` list. The `"wrapper"` preset doesn't include it.
- **A hosted page doesn't need this.** A site wrapper's page has a web origin of its own, so it can frame the player directly.
