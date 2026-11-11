// Tray titleColor / titleFont hand check (#114). A ticking mm:ss clock in the
// tray; the window picks the variant and shows what the launcher drew
// (debug:tray: font, and the colour resolved under a light and a dark bar).
// Switch Light/Dark (System Settings → Appearance) and watch both.

const VARIANTS = {
  pair: { titleColor: { light: '#b00020', dark: '#ff8a80' }, titleFont: 'monospacedDigit' },
  single: { titleColor: '#0a84ff', titleFont: 'monospacedDigit' },
  digits: { titleFont: 'monospacedDigit' },
  mono: { titleColor: { light: '#1b7f1b', dark: '#7cf07c' }, titleFont: 'monospaced' },
  plain: {},
};

let variant = 'pair';
const t0 = Date.now();

function clock() {
  const s = Math.floor((Date.now() - t0) / 1000) + 11 * 60 + 11; // start at 11:11
  return String(Math.floor(s / 60) % 100).padStart(2, '0') + ':' + String(s % 60).padStart(2, '0');
}

function apply(app) {
  app.tray.set({ icon: 'sf:timer', title: clock(), ...VARIANTS[variant] });
}

async function report(app, why) {
  const tray = await app.debug('debug:tray');
  const theme = await app.theme.get();
  const r = { why, variant, theme, spec: VARIANTS[variant], tray };
  console.log('traytitle:', JSON.stringify(r));
  app.push('report', r);
  return r;
}

export const api = {
  async setVariant({ name }, app) {
    if (!VARIANTS[name]) throw new Error('unknown variant ' + name);
    variant = name;
    apply(app);
    return report(app, 'variant');
  },
  report: (_p, app) => report(app, 'asked'),
  variants: () => Object.keys(VARIANTS),
};

export function init(app) {
  apply(app);
  setInterval(() => apply(app), 1000);
}

export function onSystem(kind, _value, app) {
  if (kind === 'theme') setTimeout(() => report(app, 'theme changed'), 300);
}
