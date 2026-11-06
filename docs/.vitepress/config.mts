import { defineConfig } from 'vitepress'
import type { DefaultTheme } from 'vitepress'
import markdownExtras from './markdown'
import { writeLlms } from './llms'

// The landing page (public/index.html) and the installers are plain files
// VitePress copies as-is. Links to them carry target: '_self' so the client
// router does a real page load instead of looking for a markdown page.
const self = { target: '_self' } as const

// also the order and grouping of /llms.txt (llms.ts)
const sidebar: DefaultTheme.SidebarItem[] = [
  {
    text: 'start',
    items: [
      { text: 'introduction', link: '/docs/' },
      { text: 'install', link: '/docs/install' },
      { text: 'your first app', link: '/docs/first-app' },
      { text: 'cli', link: '/docs/cli' },
      { text: 'doctor', link: '/docs/doctor' },
      { text: 'tinyjs.json', link: '/docs/config' },
      { text: 'editor schema', link: '/docs/config-schema' },
      { text: 'shared code', link: '/docs/shared-code' },
      { text: 'resources', link: '/docs/resources' },
      { text: 'executables', link: '/docs/executables' },
      { text: 'assets', link: '/docs/assets' },
      { text: 'extras', link: '/docs/extras' },
    ],
  },
  {
    text: 'backend',
    items: [
      { text: 'general', link: '/docs/backend' },
      { text: 'txiki.js', link: '/docs/txiki' },
      { text: 'sqlite', link: '/docs/sqlite' },
      { text: 'cookies', link: '/docs/cookies' },
    ],
  },
  {
    // topic names, not namespaces: the group title already says tiny
    text: 'tiny api',
    items: [
      { text: 'calling the backend', link: '/docs/api' },
      { text: 'fetch', link: '/docs/fetch' },
      { text: 'window', link: '/docs/window' },
      { text: 'multiple windows', link: '/docs/multiple-windows' },
      { text: 'window fx', link: '/docs/window-fx' },
      { text: 'dialogs', link: '/docs/dialogs' },
      { text: 'notifications', link: '/docs/notifications' },
      { text: 'menus', link: '/docs/menus' },
      { text: 'tray', link: '/docs/tray' },
      { text: 'hotkeys', link: '/docs/hotkeys' },
      { text: 'keystrokes', link: '/docs/keystrokes' },
      { text: 'clipboard', link: '/docs/clipboard' },
      { text: 'store', link: '/docs/store' },
      { text: 'secrets & auth', link: '/docs/secrets' },
      { text: 'permissions', link: '/docs/permissions' },
      { text: 'audio filters', link: '/docs/audio' },
      { text: 'sound effects', link: '/docs/sampler' },
      { text: 'audio tap', link: '/docs/audiotap' },
      { text: 'media', link: '/docs/media' },
      { text: 'embeds', link: '/docs/embeds' },
      { text: 'shell & app surface', link: '/docs/shell' },
      { text: 'system services', link: '/docs/services' },
      { text: 'system', link: '/docs/system' },
      { text: 'macOS only', link: '/docs/macos-only' },
    ],
  },
  {
    text: 'ship',
    items: [
      { text: 'site wrappers', link: '/docs/site-wrappers' },
      { text: 'deep links & files', link: '/docs/deep-links' },
      { text: 'auto-update', link: '/docs/auto-update' },
      { text: 'signed updates', link: '/docs/signed-updates' },
    ],
  },
  {
    text: 'good to know',
    items: [
      { text: 'how it works', link: '/docs/how-it-works' },
      { text: 'portability', link: '/docs/portability' },
      { text: 'gotchas', link: '/docs/gotchas' },
    ],
  },
]

export default defineConfig({
  title: 'tinyjs',
  description: 'Tiny macOS, Windows, and Linux desktop apps in JavaScript.',
  lang: 'en-US',
  cleanUrls: true,
  // ::: tabs / pagelink / download / carousel, image captions + zoom
  markdown: { config: markdownExtras },
  // /llms.txt, /llms-full.txt and a .md copy of every docs page
  buildEnd: (site) => writeLlms(site, sidebar),
  head: [
    ['link', { rel: 'icon', type: 'image/svg+xml', href: '/images/rakali-badge.svg' }],
    ['link', { rel: 'icon', type: 'image/png', sizes: '32x32', href: '/images/rakali-badge-tiny.png' }],
    ['link', { rel: 'apple-touch-icon', sizes: '512x512', href: '/images/rakali-badge-small.webp' }],
    ['link', { rel: 'manifest', href: '/site.webmanifest' }],
    ['link', { rel: 'alternate', type: 'text/markdown', title: 'llms.txt', href: '/llms.txt' }],
    ['meta', { property: 'og:site_name', content: 'tinyjs' }],
    ['meta', { property: 'og:image', content: 'https://tinyjs.app/images/rakali-og.jpg' }],
    ['meta', { name: 'twitter:card', content: 'summary_large_image' }],
    ['meta', { name: 'twitter:site', content: '@tinyjsapp' }],
  ],
  themeConfig: {
    logo: { src: '/images/rakali-badge.svg', alt: 'Rakali, the tinyjs mascot' },
    logoLink: { link: '/', ...self },
    nav: [
      { text: 'docs', link: '/docs/', activeMatch: '^/docs/' },
      { text: 'changelog', link: '/changelog' },
      { text: 'examples', link: 'https://github.com/tarwin/tinyjsapp-examples' },
    ],
    socialLinks: [
      { icon: 'github', link: 'https://github.com/tarwin/tinyjsapp' },
      { icon: 'discord', link: 'https://discord.com/invite/NGsFQhEUD' },
    ],
    search: { provider: 'local' },
    outline: [2, 3],
    editLink: {
      pattern: 'https://github.com/tarwin/tinyjsapp/edit/main/docs/:path',
    },
    sidebar: { '/docs/': sidebar },
    footer: {
      message: 'MIT licensed · by <a href="https://tarwin.art/">tarwin</a>',
    },
  },
})
