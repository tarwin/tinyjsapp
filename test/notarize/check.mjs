// CLI config → notarytool arguments, without Apple tools or credentials.
// Any OS, Node 22+: node test/notarize/check.mjs
// Only the txiki runtime boundary is substituted; cli.js runs unchanged.
import assert from 'node:assert/strict';
import { mkdir, mkdtemp, readFile, rm, stat, writeFile } from 'node:fs/promises';
import { resolve } from 'node:path';

const cli = new URL('../../cli.js', import.meta.url);
const base = { name: 'notarize-check', signIdentity: 'Developer ID Application: Test',
               notarize: { profile: 'test-profile' } };
const cases = [
  ['no keychain', {}, {}, null],
  ['null keychain', { notarize: { keychain: null } }, {}, null],
  ['empty keychain', { notarize: { keychain: '' } }, {}, null],
  ['relative path with spaces', { notarize: { keychain: 'keys/ci keychain-db' } }, {}, 'keys/ci keychain-db'],
  ['absolute path', { notarize: { keychain: '/ci/keys/notary.keychain-db' } }, {}, '/ci/keys/notary.keychain-db'],
  ['macOS override', { notarize: { keychain: 'root-db' }, macos: { notarize: { keychain: 'mac-db' } } }, {}, 'mac-db'],
  ['macOS clears root', { notarize: { keychain: 'root-db' }, macos: { notarize: { keychain: null } } }, {}, null],
  ['environment wins', { notarize: { keychain: 'root-db' }, macos: { notarize: { keychain: 'mac-db' } } },
    { TINYJS_NOTARY_KEYCHAIN: '/ci/keys/環境 keychain-db' }, '/ci/keys/環境 keychain-db'],
  ['environment only', { notarize: null },
    { TINYJS_NOTARY_PROFILE: 'test-profile', TINYJS_NOTARY_KEYCHAIN: 'env-db' }, 'env-db'],
  ['empty environment keeps config', { notarize: { keychain: 'config-db' } }, { TINYJS_NOTARY_KEYCHAIN: '' }, 'config-db'],
];

const cwd = process.cwd();
await mkdir('.tmp', { recursive: true });
const project = await mkdtemp(resolve('.tmp/notarize-'));
await mkdir(resolve(project, 'dist/notarize-check.app'), { recursive: true });
// The CLI detects macOS by excluding Windows and Linux.
Object.defineProperty(globalThis, 'navigator', { configurable: true, value: { platform: 'MacIntel' } });
process.chdir(project);
try {
  for (const [i, [label, extra, env, keychain]] of cases.entries()) {
    const config = { ...base, ...extra,
      notarize: extra.notarize === null ? null : { ...base.notarize, ...extra.notarize } };
    await writeFile('tinyjs.json', JSON.stringify(config));
    const commands = [];
    globalThis.tjs = {
      args: ['tjs', 'run', cli.pathname, 'notarize'], env,
      readFile, stat, lstat: stat,
      exit(code) { throw new Error('CLI exited with ' + code); },
      spawn(argv) {
        commands.push(argv);
        assert.ok(['sh', 'ditto', 'xcrun'].includes(argv[0]), 'unexpected command: ' + argv);
        return {
          stdout: new ReadableStream({ start(controller) {
            if (argv[0] === 'sh') controller.enqueue(new TextEncoder().encode('Authority=Developer ID Application: Test\n'));
            controller.close();
          } }),
          wait: async () => ({ exit_status: 0, term_signal: null }),
        };
      },
    };
    await import(cli.href + '?case=' + i);
    const submit = commands.filter(argv => argv[1] === 'notarytool');
    assert.equal(submit.length, 1, label);
    assert.deepEqual(submit[0], ['xcrun', 'notarytool', 'submit', '.build/notarize.zip',
      '--keychain-profile', 'test-profile', ...(keychain === null ? [] : ['--keychain', keychain]), '--wait'], label);
    assert.deepEqual(commands.map(argv => argv.slice(0, 3)), [
      ['sh', '-c', 'codesign -dvvv "$1" 2>&1'], ['ditto', '-c', '-k'],
      ['xcrun', 'notarytool', 'submit'], ['xcrun', 'stapler', 'staple'],
    ], label + ': signing check → zip → submit → staple');
    console.log('PASS ' + label);
  }
} finally {
  process.chdir(cwd);
  await rm(project, { recursive: true });
}
