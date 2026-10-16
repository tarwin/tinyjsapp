#!/usr/bin/env python3
"""Actual txiki checks; macOS also exercises publish + packaged app.update.
Requires bin/tjs and native/launcher-macos from setup.sh on macOS.
No public network, uploads, persistent keys, or installed apps are used.
"""
import functools
import hashlib
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import threading
import time

TOOL = Path(__file__).resolve().parents[2]
TJS = TOOL / 'bin' / ('tjs.exe' if os.name == 'nt' else 'tjs')


def run(argv, cwd=TOOL, env=None, ok=True):
    p = subprocess.run([str(x) for x in argv], cwd=cwd, env=env,
                       text=True, capture_output=True, timeout=180)
    # txiki currently reports uncaught JS exceptions with exit status 0.
    if ok and (p.returncode or 'Error:' in p.stdout + p.stderr):
        raise AssertionError(p.stdout + p.stderr)
    return p


with tempfile.TemporaryDirectory(prefix='tinyjs-signed-updates-') as scratch:
    root = Path(scratch)
    keys = root / 'external-keys'
    keys.mkdir()
    p = run([TJS, 'run', TOOL / 'test/signed-updates/crypto-probe.js', keys])
    assert 'PASS RFC 8032' in p.stdout, p.stdout + p.stderr
    print(p.stdout.strip())
    p = run([TJS, 'run', TOOL / 'test/signed-updates/runtime-check.js'])
    assert 'PASS 38' in p.stdout, p.stdout + p.stderr
    print(p.stdout.strip())
    run([TJS, 'run', TOOL / 'cli.js', 'version'])
    project = root / 'project'
    project.mkdir()
    run([TJS, 'run', TOOL / 'cli.js', 'new', 'scaffold'], root)
    assert (root / 'scaffold/tinyjs.json').is_file()
    public = (keys / 'public.txt').read_text()
    private = keys / 'private.der'
    env = dict(os.environ, TINYJS_UPDATE_PRIVATE_KEY_FILE=str(private))
    clean_env = dict(os.environ)
    clean_env.pop('TINYJS_UPDATE_PRIVATE_KEY_FILE', None)
    config = {'name': 'signed-update-check', 'title': 'Signed Update Check',
              'id': 'org.tinyjs.signedtest.' + str(os.getpid()), 'version': '2.0.0',
              'mainWindow': False, 'activation': 'accessory',
              'update': {'url': 'http://127.0.0.1:1/manifest.json', 'publicKey': public}}

    def save_config():
        (project / 'tinyjs.json').write_text(json.dumps(config))

    def cli(*args, key_env=env, ok=True):
        return run([TJS, 'run', TOOL / 'cli.js', *args], project, key_env, ok)

    save_config()
    (project / 'dist').mkdir()
    marker = project / 'dist/sentinel'
    marker.write_text('must survive rejected publishing')
    p = cli('publish', key_env=clean_env, ok=False)
    assert p.returncode and marker.read_text() == 'must survive rejected publishing'
    # Fresh wrong key is never stored in the project or fixtures.
    other = keys / 'other'
    other.mkdir()
    run([TJS, 'run', TOOL / 'test/signed-updates/crypto-probe.js', other])
    wrong_env = dict(env, TINYJS_UPDATE_PRIVATE_KEY_FILE=str(other / 'private.der'))
    p = cli('publish', key_env=wrong_env, ok=False)
    assert p.returncode and 'signature verification failed' in p.stdout + p.stderr and marker.exists()
    merged = root / 'merged.json'
    plain = {'version': '2.0.0', 'notes': 'merged release', 'win': {'url': 'https://host/w.zip', 'sha256': 'a' * 64},
             'linux': {'arm64': {'url': 'https://host/l.tar.gz', 'sha256': 'b' * 64}}}
    merged.write_text(json.dumps(plain))
    original = merged.read_bytes()
    p = cli('publish', '--sign-manifest', merged, key_env=wrong_env, ok=False)
    assert p.returncode
    assert merged.read_bytes() == original, 'failed signing changed original input'
    cli('publish', '--sign-manifest', merged)
    envelope = json.loads(merged.read_text())
    assert json.loads(envelope['payload']) == plain
    # Verify actual command output independently in the runtime.
    verifier = root / 'verify.js'
    verifier.write_text('import { verifyManifest } from ' + json.dumps(str(TOOL / 'runtime/update-signing.js')) + ';\n'
                        'const m=JSON.parse(new TextDecoder().decode(await tjs.readFile(tjs.args[3])));\n'
                        'await verifyManifest(m, ' + json.dumps(public) + '); console.log("VERIFIED");\n')
    assert 'VERIFIED' in run([TJS, 'run', verifier, merged]).stdout
    merged.write_text(json.dumps({**envelope, 'payload': envelope['payload'].replace('merged release', 'modified merge')}))
    p = run([TJS, 'run', verifier, merged], ok=False)
    assert 'signature verification failed' in p.stdout + p.stderr
    print('PASS CLI version/new, fail-closed publish, final multi-platform signing, stale signature rejection')
    if sys.platform != 'darwin':
        print('SKIP packaged-app integration: this fixture currently targets macOS')
        sys.exit(0)

    hosted = root / 'hosted'
    hosted.mkdir()
    state = {'mode': 'valid', 'requests': 0}

    class Handler(SimpleHTTPRequestHandler):
        def log_message(self, *_):
            pass

        def do_GET(self):
            if self.path == '/manifest.json':
                state['requests'] += 1
                data = json.loads((hosted / 'manifest.json').read_text())
                if state['mode'] == 'legacy' or (state['mode'] == 'unsigned-after-check' and state['requests'] > 1):
                    data = json.loads(data['payload'])
                elif state['mode'] == 'tamper':
                    data['payload'] = data['payload'].replace('2.0.0', '9.0.0')
                body = json.dumps(data).encode()
                self.send_response(200)
                self.send_header('Content-Length', str(len(body)))
                self.end_headers()
                self.wfile.write(body)
            else:
                super().do_GET()

    server = ThreadingHTTPServer(('127.0.0.1', 0), functools.partial(Handler, directory=str(hosted)))
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    config['update']['url'] = f'http://127.0.0.1:{server.server_port}/manifest.json'
    save_config()
    src = project / 'src'
    src.mkdir()
    report = root / 'report.json'
    src.joinpath('main.js').write_text('''
const report = REPORT;
const write = x => tjs.writeFile(report, new TextEncoder().encode(JSON.stringify(x)));
export async function init(app) {
  try {
    if (app.info.version === '2.0.0') {
      await write({ installed: app.info.version });
      app.quit(); return;
    }
    const checked = await app.update.check();
    if (!checked.available) throw new Error('expected update');
    await app.update.install();
  } catch (e) {
    await write({ error: String(e), version: app.info.version });
    app.quit();
  }
}
'''.replace('REPORT', json.dumps(str(report))))
    cli('publish', '--notes', 'signed integration release')
    for entry in (project / 'dist/publish').iterdir():
        shutil.copy2(entry, hosted / entry.name)
    output = json.loads((hosted / 'manifest.json').read_text())
    assert output['format'] == 'tinyjs-update-v1'
    assert json.loads(output['payload'])['notes'] == 'signed integration release'
    config['version'] = '1.0.0'
    save_config()
    cli('build', key_env=clean_env)
    bundle = project / 'dist/Signed Update Check.app'
    exe = bundle / 'Contents/MacOS/signed-update-check'
    before = hashlib.sha256(exe.read_bytes()).digest()
    assert private.read_bytes() not in exe.read_bytes(), 'private key leaked into compiled binary'
    assert public in (project / '.build/app/entry.js').read_text(), 'public pin missing in generated app'
    assert (project / '.build/app/update-signing.js').is_file()

    def launch(mode):
        state.update(mode=mode, requests=0)
        report.unlink(missing_ok=True)
        log = root / (mode + '.log')
        with log.open('w') as output_log:
            proc = subprocess.Popen([str(exe)], cwd=project, env=clean_env, stdout=output_log, stderr=output_log)
            try:
                for _ in range(300):
                    if report.exists():
                        result = json.loads(report.read_text())
                        proc.wait(timeout=10)
                        return result
                    time.sleep(.1)
                raise AssertionError('packaged app timed out: ' + log.read_text())
            finally:
                if proc.poll() is None:
                    proc.terminate()
                    proc.wait(timeout=10)

    try:
        result = launch('tamper')
        assert 'signature verification failed' in result.get('error', ''), result
        assert hashlib.sha256(exe.read_bytes()).digest() == before
        result = launch('unsigned-after-check')
        assert 'signed Ed25519' in result.get('error', ''), result
        assert state['requests'] == 2 and hashlib.sha256(exe.read_bytes()).digest() == before
        # A valid signature must not bypass the archive checksum.
        valid_envelope = (hosted / 'manifest.json').read_text()
        bad_hash = json.loads(json.loads(valid_envelope)['payload'])
        bad_hash['sha256'] = '0' * 64
        merged.write_text(json.dumps(bad_hash))
        cli('publish', '--sign-manifest', merged)
        shutil.copy2(merged, hosted / 'manifest.json')
        result = launch('valid')
        assert 'checksum mismatch' in result.get('error', ''), result
        assert hashlib.sha256(exe.read_bytes()).digest() == before
        (hosted / 'manifest.json').write_text(valid_envelope)
        result = launch('valid')
        assert result == {'installed': '2.0.0'}, result
        assert hashlib.sha256(exe.read_bytes()).digest() != before
        assert not Path(str(bundle) + '.update-backup').exists()
        print('PASS actual signed publish -> baked-pin app.check/install -> verified bundle swap -> relaunched 2.0.0')
        print('PASS packaged tamper + check/install substitution + checksum rejection; original bundle unchanged')
        del config['update']['publicKey']
        save_config()
        cli('build', key_env=clean_env)
        result = launch('legacy')
        assert result == {'installed': '2.0.0'}, result
        print('PASS legacy unsigned packaged check/install remains functional')
    finally:
        server.shutdown()
        server.server_close()
