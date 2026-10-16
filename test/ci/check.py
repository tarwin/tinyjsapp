#!/usr/bin/env python3
"""Bounded, dependency-free checks of a source checkout after setup.sh/setup.ps1."""
import argparse
import ctypes
import json
import os
from pathlib import Path
import re
import signal
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[2]
SMOKE_KEYS = {'ping', 'sysinfo', 'listDir', 'errorPath', 'setTitle', 'setSize', 'menuSet', 'push'}


class CheckError(RuntimeError):
    pass


class WindowsJob:
    """Kill descendants even when the command exits before its children do."""
    def __init__(self):
        from ctypes import wintypes

        class BasicLimit(ctypes.Structure):
            _fields_ = [('PerProcessUserTimeLimit', ctypes.c_int64),
                        ('PerJobUserTimeLimit', ctypes.c_int64),
                        ('LimitFlags', wintypes.DWORD),
                        ('MinimumWorkingSetSize', ctypes.c_size_t),
                        ('MaximumWorkingSetSize', ctypes.c_size_t),
                        ('ActiveProcessLimit', wintypes.DWORD),
                        ('Affinity', ctypes.c_size_t),
                        ('PriorityClass', wintypes.DWORD),
                        ('SchedulingClass', wintypes.DWORD)]

        class IO(ctypes.Structure):
            _fields_ = [(name, ctypes.c_uint64) for name in
                        ('ReadOperationCount', 'WriteOperationCount', 'OtherOperationCount',
                         'ReadTransferCount', 'WriteTransferCount', 'OtherTransferCount')]

        class ExtendedLimit(ctypes.Structure):
            _fields_ = [('BasicLimitInformation', BasicLimit), ('IoInfo', IO),
                        ('ProcessMemoryLimit', ctypes.c_size_t),
                        ('JobMemoryLimit', ctypes.c_size_t),
                        ('PeakProcessMemoryUsed', ctypes.c_size_t),
                        ('PeakJobMemoryUsed', ctypes.c_size_t)]

        self.api = ctypes.WinDLL('kernel32', use_last_error=True)
        self.api.CreateJobObjectW.argtypes = [ctypes.c_void_p, wintypes.LPCWSTR]
        self.api.CreateJobObjectW.restype = wintypes.HANDLE
        self.api.SetInformationJobObject.argtypes = [wintypes.HANDLE, ctypes.c_int,
                                                     ctypes.c_void_p, wintypes.DWORD]
        self.api.AssignProcessToJobObject.argtypes = [wintypes.HANDLE, wintypes.HANDLE]
        self.api.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
        self.api.OpenProcess.restype = wintypes.HANDLE
        self.api.CloseHandle.argtypes = [wintypes.HANDLE]
        self.handle = self.api.CreateJobObjectW(None, None)
        if not self.handle:
            raise ctypes.WinError(ctypes.get_last_error())
        limits = ExtendedLimit()
        limits.BasicLimitInformation.LimitFlags = 0x2000  # KILL_ON_JOB_CLOSE
        if not self.api.SetInformationJobObject(self.handle, 9, ctypes.byref(limits),
                                                ctypes.sizeof(limits)):
            error = ctypes.get_last_error()
            self.close()
            raise ctypes.WinError(error)

    def assign(self, process):
        handle = self.api.OpenProcess(0x0100 | 0x0001, False, process.pid)
        if not handle:
            raise ctypes.WinError(ctypes.get_last_error())
        try:
            if not self.api.AssignProcessToJobObject(self.handle, handle):
                raise ctypes.WinError(ctypes.get_last_error())
        finally:
            self.api.CloseHandle(handle)

    def close(self):
        if self.handle:
            self.api.CloseHandle(self.handle)
            self.handle = None


def run(command, cwd, timeout=120, env=None):
    """Capture to a file so surviving descendants cannot keep a stdout pipe open."""
    print('+ ' + ' '.join(map(str, command)), flush=True)
    job = WindowsJob() if os.name == 'nt' else None
    with tempfile.TemporaryFile() as log:
        process = None
        timed_out = False
        try:
            actual = command
            if job:
                # Wait until attached to the job before creating any descendants.
                gate = ('import subprocess, sys; sys.stdin.buffer.read(1); '
                        'sys.exit(subprocess.call(sys.argv[1:]))')
                actual = [sys.executable, '-c', gate, *command]
            process = subprocess.Popen(actual, cwd=cwd, env=env, stdout=log,
                                       stderr=subprocess.STDOUT,
                                       stdin=subprocess.PIPE if job else subprocess.DEVNULL,
                                       start_new_session=os.name != 'nt')
            if job:
                job.assign(process)
                process.stdin.write(b'1')
                process.stdin.close()
            try:
                process.wait(timeout=timeout)
            except subprocess.TimeoutExpired:
                timed_out = True
        finally:
            if job:
                job.close()
            elif process:
                try:
                    os.killpg(process.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
            if process:
                if process.poll() is None:
                    process.kill()
                process.wait(timeout=10)
            log.seek(0)
            output = log.read().decode('utf-8', errors='replace')
            print(output, end='' if output.endswith('\n') else '\n', flush=True)
    if timed_out:
        raise CheckError(f'command timed out after {timeout}s')
    if process.returncode != 0:
        raise CheckError(f'command exited with {process.returncode}')
    return output


def validate_smoke(output):
    if re.search(r'SMOKE FATAL|\bFAIL\b', output):
        raise CheckError('GUI smoke reported a failure')
    reports = re.findall(r'SMOKE RESULTS (.*)', output)
    if len(reports) != 1:
        raise CheckError('expected exactly one SMOKE RESULTS report')
    try:
        results, end = json.JSONDecoder().raw_decode(reports[0])
    except ValueError as error:
        raise CheckError('invalid smoke JSON') from error
    if reports[0][end:].strip() or not isinstance(results, dict) or set(results) != SMOKE_KEYS:
        raise CheckError('incomplete or unexpected smoke report')
    if not all(isinstance(value, str) and value for value in results.values()):
        raise CheckError('empty or invalid smoke result')
    if results['ping'] != 'pong':
        raise CheckError('unexpected ping reply')
    for key in ('setTitle', 'setSize', 'menuSet'):
        if results[key] != 'ok':
            raise CheckError(f'{key} did not succeed')
    if not results['errorPath'].startswith('rejects ok:'):
        raise CheckError('unknown method did not reject')
    if not results['push'].startswith('tick received:'):
        raise CheckError('no backend push received')


def smoke():
    # Invoke the runtime directly: no shell quoting, .cmd execution, or PATH mutation.
    runtime = ROOT / 'bin' / ('tjs.exe' if os.name == 'nt' else 'tjs')
    cli = [str(runtime), 'run', str(ROOT / 'cli.js')]
    version = run(cli + ['version'], ROOT)
    if not re.search(r'^tinyjs (?:dev|v?\d+\.\d+\.\d+) \(txiki\.js ', version, re.MULTILINE):
        raise CheckError('version returned no tinyjs version')
    with tempfile.TemporaryDirectory(prefix='tinyjs-ci-') as temp:
        work = Path(temp)
        # Unique ID avoids forwarding the GUI invocation to another running test app.
        name = work.name
        run(cli + ['new', name], work)
        app = work / name
        run(cli + ['build'], app, timeout=300)
        dist = app / 'dist'
        names = [name + '.exe', 'launcher.exe'] if os.name == 'nt' else [name, 'launcher']
        for filename in names:
            artifact = dist / filename
            if not artifact.is_file() or artifact.stat().st_size == 0:
                raise CheckError(f'missing or empty artifact: {artifact}')
            if os.name != 'nt' and not os.access(artifact, os.X_OK):
                raise CheckError(f'artifact is not executable: {artifact}')
        if sys.platform == 'darwin':
            run(['codesign', '--verify', '--strict', '--deep',
                 str(dist / (name + '.app'))], app)
        env = dict(os.environ, TINYJS_HTML=str(ROOT / 'test' / 'smoke.html'))
        validate_smoke(run(cli + ['dev'], app, timeout=90, env=env))
    print('version + new + build + GUI smoke OK', flush=True)


def self_test():
    good = dict(ping='pong', sysinfo='tjs on host (cpu)', listDir='1 entries',
                errorPath='rejects ok: unknown method', setTitle='ok', setSize='ok',
                menuSet='ok', push='tick received: {}')
    report = 'SMOKE RESULTS ' + json.dumps(good) + '\n'
    validate_smoke(report)
    invalid = ['', 'SMOKE RESULTS nope', report + report, report + 'SMOKE FATAL boom',
               report.replace('"ok"', '"FAIL"', 1),
               'SMOKE RESULTS ' + json.dumps({'ping': 'pong'}),
               report.replace('tick received:', 'missing:'),
               report.replace('rejects ok:', 'resolved:'),
               report.replace('"pong"', 'false'), report.replace('"pong"', '"wrong"'),
               report.rstrip() + 'garbage']
    for fixture in invalid:
        try:
            validate_smoke(fixture)
        except CheckError:
            continue
        raise CheckError(f'accepted invalid report: {fixture!r}')
    with tempfile.TemporaryDirectory(prefix='tinyjs-ci-self-') as temp:
        # A success marker must never hide a later crash or a hanging process.
        for code, timeout in [(f'print({report!r}); raise SystemExit(3)', 10),
                              ('import time; time.sleep(30)', 0.2)]:
            try:
                run([sys.executable, '-c', code], temp, timeout=timeout)
            except CheckError:
                pass
            else:
                raise CheckError('accepted nonzero exit or timeout')
        # Check descendants are killed on both timeout and normal parent exit.
        for parent_sleep in (0, 30):
            marker = Path(temp) / f'leaked-{parent_sleep}'
            child = f'import time; from pathlib import Path; time.sleep(2); Path({str(marker)!r}).touch()'
            code = ('import subprocess, sys, time; '
                    f'subprocess.Popen([sys.executable, "-c", {child!r}]); '
                    f'time.sleep({parent_sleep})')
            try:
                run([sys.executable, '-c', code], temp, timeout=0.5 if parent_sleep else 10)
            except CheckError:
                if not parent_sleep:
                    raise
            else:
                if parent_sleep:
                    raise CheckError('expected the parent to time out')
            time.sleep(2.5)
            if marker.exists():
                raise CheckError('a descendant survived cleanup')
    print('harness self-tests OK', flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument('--self-test', action='store_true')
    mode.add_argument('--smoke', action='store_true')
    args = parser.parse_args()
    try:
        self_test() if args.self_test else smoke()
    except (CheckError, OSError) as error:
        print(f'ERROR: {error}', file=sys.stderr)
        sys.exit(1)
