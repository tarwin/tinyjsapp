# tray title colour / font (#114)

Hand check for `tray.set({ titleColor, titleFont })` (macOS only).

    cd test/tray-title/app && tinyjs dev

A ticking `mm:ss` clock appears in the menu bar. The window has a button per
variant (pair, single, digits, mono, plain) and shows what the launcher drew,
read back via `debug:tray`: the colour for a light and a dark menu bar, with
the current one marked, and the font.

Switch System Settings → Appearance between Light and Dark:

- **pair / mono**: the title switches to the colour marked "now".
- **single**: the same blue on both.
- **plain**: the system's black/white. The digits shift sideways as the clock
  ticks; with any other variant they must stay put.

Each change is also logged to the terminal as `traytitle: {...}`.
