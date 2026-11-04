// tjs:ffi on Windows, every feature against an answer that doesn't come from FFI
// wherever there is one. Each case runs in its own process, so a native crash fails
// one case only. Windows only, since the calls are Win32 and CRT ones.
//
//   bin\tjs.exe run C:\full\path\to\test\ffi\check.js    (a full backslash path)
//
// Lines after a dash report something a pass doesn't prove, such as what comes back
// past 2^53.
import ffi, {
  dlopen, dlopenCProto, types, read, suffix, errno, strerror, defineStruct, defineEnum, allocStruct,
  StructType, ArrayType, StaticStringType, Pointer, PointerType, JSCallback, ExternalArrayBuffer,
  bufferToString, stringToBuffer, bufferToPointer, createPointer,
} from 'tjs:ffi';

if (tjs.env.OS !== 'Windows_NT') { console.log('skip: Windows only'); tjs.exit(0); }

const eq = (got, want, what = '') => {
  const g = typeof got === 'bigint' ? got.toString() + 'n' : JSON.stringify(got);
  const w = typeof want === 'bigint' ? want.toString() + 'n' : JSON.stringify(want);
  if (g !== w) throw new Error(`${what} got ${g}, wanted ${w}`);
};
const ok = (v, what) => { if (!v) throw new Error(what); };
const throws = (f, re, what) => {
  try { f(); } catch (e) { ok(re.test(String(e?.message ?? e)), `${what}: threw "${e?.message}"`); return String(e.message).slice(0, 70); }
  throw new Error(`${what}: did not throw`);
};
const cstr = (b) => new TextDecoder().decode(b.subarray(0, b.indexOf(0) < 0 ? b.length : b.indexOf(0)));
const gc = () => { for (let i = 0; i < 5; i++) tjs.engine.gc.run(); };

const CASES = {
  'namespace: default export matches named; suffix': () => {
    eq(ffi.dlopen === dlopen, true); eq(suffix, 'dll');
    for (const n of ['Lib', 'CFunction', 'DlSymbol']) eq(n in ffi, false, n);
    return 'old names gone';
  },
  'type sizes are LLP64': () => {
    eq([types.pointer.size, types.size.size, types.ulong.size, types.uint.size, types.ullong.size, types.bool_u32.size, types.bool_u8.size],
      [8, 8, 4, 4, 8, 4, 1]);
  },
  'ints: pid, tick count': () => {
    const k = dlopen('kernel32.dll', { GetCurrentProcessId: { returns: types.uint }, GetTickCount64: { returns: types.uint64 } });
    eq(k.symbols.GetCurrentProcessId(), tjs.pid);
    const a = k.symbols.GetTickCount64();
    ok(Number(a) > 0, 'tick not positive');
    return `GetTickCount64 returns ${typeof a}`;
  },
  'uint32 keeps its high bit, both ways': () => {
    const c = dlopen('ucrtbase.dll', { _byteswap_ulong: { args: [types.uint32], returns: types.uint32 } });
    eq(c.symbols._byteswap_ulong(0x80000000), 0x80);
    eq(c.symbols._byteswap_ulong(0x80), 0x80000000);
    eq(c.symbols._byteswap_ulong(0x80000000 | 1), 0x01000080, 'negative int32 from JS bit-or');
  },
  '64-bit ints, full precision': () => {
    const c = dlopen('ucrtbase.dll', { _abs64: { args: ['i64'], returns: 'i64' }, _byteswap_uint64: { args: ['u64'], returns: 'u64' } });
    const r = c.symbols._abs64(-5000000000);
    eq(Number(r), 5000000000);
    eq(Number(c.symbols._abs64(-(2 ** 52))), 2 ** 52, '2^52');
    const swapped = c.symbols._byteswap_uint64(0x01020304);
    const u64big = c.symbols._byteswap_uint64(0x08);
    return `past 2^53 comes back ${typeof u64big}: ${u64big} (true value 576460752303423488); bigint args rejected, per the d.ts`;
  },
  'doubles and floats, mixed with ints in registers': () => {
    const c = dlopen('ucrtbase.dll', {
      pow: { args: ['f64', 'f64'], returns: 'f64' },
      ldexp: { args: ['f64', 'i32'], returns: 'f64' },
      sqrtf: { args: ['f32'], returns: 'f32' },
    });
    eq(c.symbols.pow(2, 10), 1024); eq(c.symbols.ldexp(1.5, 4), 24); eq(c.symbols.sqrtf(6.25), 2.5);
  },
  'strings in and out, UTF-8': () => {
    const c = dlopen('ucrtbase.dll', {
      strlen: { args: ['string'], returns: 'size_t' },
      strchr: { args: ['string', 'i32'], returns: 'string' },
      strstr: { args: ['string', 'string'], returns: 'pointer' },
    });
    eq(Number(c.symbols.strlen('abc')), 3);
    eq(Number(c.symbols.strlen('ü中😀')), 2 + 3 + 4, 'bytes');
    eq(c.symbols.strchr('hello', 'l'.charCodeAt(0)), 'llo');
    eq(c.symbols.strstr('a', 'z'), null, 'NULL return');
  },
  'buffers: filled by the callee': () => {
    const k = dlopen('kernel32.dll', { GetComputerNameA: { args: ['buffer', 'buffer'], returns: 'bool_u32' } });
    const name = new Uint8Array(256), len = new Uint32Array([256]);
    eq(k.symbols.GetComputerNameA(name, new Uint8Array(len.buffer)), true);
    eq(cstr(name), tjs.env.COMPUTERNAME);
    eq(len[0], tjs.env.COMPUTERNAME.length);
  },
  'wide strings via buffers': () => {
    const k = dlopen('kernel32.dll', { GetEnvironmentVariableW: { args: ['buffer', 'buffer', 'u32'], returns: 'u32' } });
    const key = new Uint8Array(new Uint16Array([...'COMPUTERNAME'].map((c) => c.charCodeAt(0)).concat(0)).buffer);
    const out = new Uint16Array(256);
    const n = k.symbols.GetEnvironmentVariableW(key, new Uint8Array(out.buffer), 256);
    eq(String.fromCharCode(...out.subarray(0, n)), tjs.env.COMPUTERNAME);
  },
  'pointers: read.*, offset, equals, value, createPointer, views': () => {
    const k = dlopen('kernel32.dll', { GetModuleHandleA: { args: ['pointer'], returns: 'pointer' } });
    const c = dlopen('ucrtbase.dll', { malloc: { args: ['size_t'], returns: 'pointer' }, free: { args: ['pointer'] } });
    const exe = k.symbols.GetModuleHandleA(null);
    ok(exe, 'null module');
    eq(read.u16(exe), 0x5a4d, 'MZ header');
    const lfanew = read.u32(exe, 0x3c);
    eq(read.u32(exe, lfanew), 0x4550, 'PE signature');
    eq(createPointer(exe.value).equals(exe), true);
    eq(createPointer(0n), null);
    eq(exe.offset(4).value, exe.value + 4n);
    ok(/^0x/.test(exe.toString()), 'toString');
    const m = c.symbols.malloc(16);
    const view = m.toUint8Array(16);
    view.set([1, 2, 3, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]);
    eq(read.u8(m, 2), 3); eq(read.u32(m), 0x04030201);
    const ab = m.toArrayBuffer(16);
    ok(ab instanceof ExternalArrayBuffer, 'ExternalArrayBuffer');
    ab.detach(); eq(ab.byteLength, 0);
    new Float64Array(m.toArrayBuffer(16))[1] = 2.5; eq(read.f64(m, 8), 2.5);
    c.symbols.free(m);
    const b = new Uint8Array([9, 8, 7]);
    eq(read.u8(bufferToPointer(b), 1), 8);
    eq(bufferToString(stringToBuffer('hé')), 'hé');
  },
  "SetThreadExecutionState flags, matcha's expression, read back from Windows": () => {
    const k32 = dlopen('kernel32.dll', { SetThreadExecutionState: { args: [types.uint32], returns: types.uint32 } });
    const set = k32.symbols.SetThreadExecutionState;
    const ES_CONTINUOUS = 0x80000000, ES_SYSTEM = 0x1, ES_DISPLAY = 0x2;
    set(ES_CONTINUOUS | ES_SYSTEM | ES_DISPLAY);
    eq(set(ES_CONTINUOUS | ES_SYSTEM) >>> 0, 0x80000003, 'system + display');
    eq(set(ES_CONTINUOUS) >>> 0, 0x80000001, 'system only');
    eq(set(ES_CONTINUOUS) >>> 0, 0x80000000, 'cleared');
  },
  'bool_u32 returns': () => {
    const u = dlopen('user32.dll', { IsWindow: { args: ['pointer'], returns: 'bool_u32' }, GetDesktopWindow: { returns: 'pointer' } });
    eq(u.symbols.IsWindow(null), false);
    eq(u.symbols.IsWindow(u.symbols.GetDesktopWindow()), true);
  },
  'defineStruct by pointer: SYSTEMTIME, MEMORYSTATUSEX': () => {
    const k = dlopen('kernel32.dll', { GetSystemTime: { args: ['buffer'] }, GlobalMemoryStatusEx: { args: ['buffer'], returns: 'bool_u32' } });
    const ST = defineStruct(['year', 'month', 'dow', 'day', 'hour', 'min', 'sec', 'ms'].map((n) => [n, 'u16']));
    eq(ST.size, 16);
    const b = new Uint8Array(16);
    k.symbols.GetSystemTime(b);
    const t = ST.unpack(b), d = new Date();
    eq([t.year, t.month, t.day, t.dow], [d.getUTCFullYear(), d.getUTCMonth() + 1, d.getUTCDate(), d.getUTCDay()]);
    const MS = defineStruct([['len', 'u32'], ['load', 'u32'], ['total', 'u64'], ['avail', 'u64'], ['tp', 'u64'], ['ap', 'u64'], ['tv', 'u64'], ['av', 'u64'], ['ext', 'u64']]);
    eq(MS.size, 64);
    eq(MS.describe().find((f) => f.name === 'total').offset, 8);
    const mb = MS.pack({ len: 64, load: 0, total: 0, avail: 0, tp: 0, ap: 0, tv: 0, av: 0, ext: 0 });
    eq(k.symbols.GlobalMemoryStatusEx(mb), true);
    const m = MS.unpack(mb);
    ok(Number(m.total) > 2 ** 30 && Number(m.avail) < Number(m.total) && m.load <= 100, `mem ${m.total} ${m.avail} ${m.load}`);
    eq(typeof m.total, 'bigint', 'defineStruct u64 field');
    return `RAM ${(Number(m.total) / 2 ** 30).toFixed(1)} GB`;
  },
  'struct returned by value, 8 bytes (div), defineStruct and StructType': () => {
    const D = defineStruct([['quot', 'i32'], ['rem', 'i32']]);
    const c = dlopen('ucrtbase.dll', { div: { args: ['i32', 'i32'], returns: D } });
    eq(c.symbols.div(17, 5), { quot: 3, rem: 2 });
    eq(c.symbols.div(-17, 5), { quot: -3, rem: -2 });
    const D2 = new StructType([['quot', types.sint32], ['rem', types.sint32]], 'div_t');
    const c2 = dlopen('ucrtbase.dll', { div: { args: ['i32', 'i32'], returns: D2 } });
    eq(c2.symbols.div(17, 5), { quot: 3, rem: 2 });
  },
  'struct returned by value, 16 bytes (lldiv), the size of CGPoint': () => {
    const D = new StructType([['quot', types.sint64], ['rem', types.sint64]], 'lldiv_t');
    const c = dlopen('ucrtbase.dll', { lldiv: { args: ['i64', 'i64'], returns: D } });
    const r = c.symbols.lldiv(50000000007, 10);
    eq([Number(r.quot), Number(r.rem)], [5000000000, 7]);
    const D2 = defineStruct([['quot', 'i64'], ['rem', 'i64']]);
    const c2 = dlopen('ucrtbase.dll', { lldiv: { args: ['i64', 'i64'], returns: D2 } });
    const r2 = c2.symbols.lldiv(50000000007, 10);
    eq([Number(r2.quot), Number(r2.rem)], [5000000000, 7]);
  },
  'struct passed by value (POINT to PtInRect)': () => {
    const POINT = defineStruct([['x', 'i32'], ['y', 'i32']]);
    const RECT = defineStruct([['l', 'i32'], ['t', 'i32'], ['r', 'i32'], ['b', 'i32']]);
    const u = dlopen('user32.dll', { PtInRect: { args: ['buffer', POINT], returns: 'bool_u32' } });
    const rc = RECT.pack({ l: 10, t: 20, r: 30, b: 40 });
    eq(u.symbols.PtInRect(rc, { x: 15, y: 25 }), true);
    eq(u.symbols.PtInRect(rc, { x: 5, y: 25 }), false);
    eq(u.symbols.PtInRect(rc, { x: 15, y: 45 }), false, 'y comes through too');
  },
  'struct helpers: enum, nested, arrays, strings, allocStruct, lists': () => {
    const E = defineEnum({ A: 0, B: 7 });
    const In = defineStruct([['x', 'u8'], ['y', 'f64']]);
    const S = defineStruct([['e', E], ['in', In], ['cells', new ArrayType(types.sint32, 3, 'i3')], ['name', new StaticStringType(8, 'n8')]]);
    const u = S.unpack(S.pack({ e: 'B', in: { x: 1, y: 2.5 }, cells: [1, 2, 3], name: 'hi' }));
    eq([u.e, u.in, u.cells, u.name], ['B', { x: 1, y: 2.5 }, [1, 2, 3], 'hi']);
    eq(In.describe().find((f) => f.name === 'y').offset, 8, 'padding');
    throws(() => S.pack({ e: 'Z', in: { x: 1, y: 2 }, cells: [1, 2, 3], name: '' }), /./, 'bad enum');
    const L = defineStruct([['count', 'u32', { lengthOf: 'items' }], ['items', ['i32']]]);
    const { bytes, arrays } = allocStruct(L, { lengths: { items: 2 } });
    new Int32Array(arrays.items.buffer, arrays.items.byteOffset, 2).set([5, 6]);
    eq(L.unpack(bytes), { count: 2, items: [5, 6] });
    const P = defineStruct([['a', 'u16']]);
    const lb = new Uint8Array(4);
    P.packListInto([{ a: 1 }, { a: 2 }], lb);
    eq(P.unpackList(lb, 2), [{ a: 1 }, { a: 2 }]);
  },
  'Pointer.createRef + PointerType through a real call': () => {
    const ST = new StructType(['year', 'month', 'dow', 'day', 'hour', 'min', 'sec', 'ms'].map((n) => [n, types.uint16]), 'SYSTEMTIME');
    const k = dlopen('kernel32.dll', { GetSystemTime: { args: [new PointerType(ST)] } });
    const ref = Pointer.createRef(ST, { year: 0, month: 0, dow: 0, day: 0, hour: 0, min: 0, sec: 0, ms: 0 });
    k.symbols.GetSystemTime(ref);
    eq(ref.deref().year, new Date().getUTCFullYear());
    eq(ref.isNull, false); eq(ref.level, 1);
  },
  'callback: EnumWindows stops when asked, arg survives': () => {
    const u = dlopen('user32.dll', { EnumWindows: { args: ['pointer', 'pointer'], returns: 'bool_u32' }, IsWindow: { args: ['pointer'], returns: 'bool_u32' } });
    let n = 0, bad = 0, seenArg = null;
    const cb = new JSCallback(types.bool_u32, [types.pointer, types.pointer], (h, arg) => {
      n++; seenArg = arg?.value; if (!u.symbols.IsWindow(h)) bad++; return n < 3;
    });
    u.symbols.EnumWindows(cb.addr, createPointer(0x1234n));
    eq(n, 3, 'callback count'); eq(bad, 0); eq(seenArg, 0x1234n);
  },
  'callback: qsort comparator, many calls': () => {
    const c = dlopen('ucrtbase.dll', { qsort: { args: ['buffer', 'size_t', 'size_t', 'pointer'] } });
    let calls = 0;
    const cmp = new JSCallback(types.sint, [types.pointer, types.pointer], (a, b) => { calls++; return read.i32(a) - read.i32(b); });
    const arr = Int32Array.from({ length: 2000 }, (_, i) => (i * 7919) % 2003);
    const want = [...arr].sort((x, y) => x - y);
    c.symbols.qsort(new Uint8Array(arr.buffer), arr.length, 4, cmp.addr);
    eq([...arr], want);
    return `${calls} callbacks`;
  },
  'variadic with `fixed`, and `name` binding one symbol twice': () => {
    const c = dlopen('msvcrt.dll', {
      fmtD: { name: 'sprintf', args: ['buffer', 'string', 'i32'], returns: 'i32', fixed: 2 },
      fmtF: { name: 'sprintf', args: ['buffer', 'string', 'f64', 'string'], returns: 'i32', fixed: 2 },
    });
    const b = new Uint8Array(64);
    c.symbols.fmtD(b, 'n=%d', 42); eq(cstr(b), 'n=42');
    c.symbols.fmtF(b, '%.2f %s', 2.5, 'ok'); eq(cstr(b), '2.50 ok', 'a double in a variadic slot');
  },
  'data symbol': () => {
    const c = dlopen('msvcrt.dll', { _sys_nerr: { type: 'i32' } });
    const v = c.symbols._sys_nerr.deref();
    ok(v > 10 && v < 1000, `_sys_nerr ${v}`);
    return `_sys_nerr ${v}`;
  },
  "dlopen('c') and dlopen('m')": () => {
    const c = dlopen('c', { strlen: { args: ['string'], returns: 'size_t' } });
    const m = dlopen('m', { pow: { args: ['f64', 'f64'], returns: 'f64' } });
    eq(Number(c.symbols.strlen('abc')), 3); eq(m.symbols.pow(2, 3), 8);
  },
  'dlopenCProto, the documented forms': () => {
    const k = dlopenCProto('kernel32.dll', 'unsigned int GetCurrentProcessId(); unsigned long long GetTickCount64();');
    eq(k.symbols.GetCurrentProcessId(), tjs.pid);
    ok(Number(k.symbols.GetTickCount64()) > 0, 'tick');
    eq(dlopenCProto('ucrtbase.dll', 'double pow(double a, double b);').symbols.pow(2, 5), 32);
    const s = dlopenCProto('kernel32.dll', 'struct st { unsigned short y; unsigned short m; unsigned short dow; unsigned short d; unsigned short h; unsigned short mi; unsigned short s; unsigned short ms; }; void GetSystemTime(struct st* t);');
    const r = Pointer.createRef(s.types.get('struct st'), { y: 0, m: 0, dow: 0, d: 0, h: 0, mi: 0, s: 0, ms: 0 });
    s.symbols.GetSystemTime(r);
    eq(r.deref().y, new Date().getUTCFullYear());
    const lim = (f) => { try { f(); return 'works'; } catch (e) { return `throws "${e.message.trim()}"`; } };
    return `(void) params ${lim(() => dlopenCProto('kernel32.dll', 'int GetCurrentProcessId(void);'))}; struct by value ${lim(() => dlopenCProto('ucrtbase.dll', 'struct p { int q; int r; }; struct p div(int a, int b);').symbols.div(1, 1))}`;
  },
  'errors are catchable: missing library, missing symbol, optional': () => {
    const a = throws(() => dlopen('no-such-lib-xyz.dll', { f: {} }), /./, 'missing lib');
    const b = throws(() => dlopen('kernel32.dll', { NoSuchFn: {} }), /./, 'missing symbol');
    const k = dlopen('kernel32.dll', { NoSuchFn: { optional: true }, GetCurrentProcessId: { returns: 'u32' } });
    eq('NoSuchFn' in k.symbols, false); eq(k.symbols.GetCurrentProcessId(), tjs.pid);
    throws(() => dlopen('kernel32.dll', { X: { type: 'i32', returns: 'i32' } }), /./, 'type + returns');
    return `${a} | ${b}`;
  },
  'close() and using': () => {
    const k = dlopen('kernel32.dll', { GetCurrentProcessId: { returns: 'u32' } });
    eq(k.symbols.GetCurrentProcessId(), tjs.pid); k.close();
    { using u = dlopen('kernel32.dll', { GetCurrentProcessId: { returns: 'u32' } }); eq(u.symbols.GetCurrentProcessId(), tjs.pid); }
    const { symbols, close } = dlopen('kernel32.dll', { GetCurrentProcessId: { returns: 'u32' } });
    eq(symbols.GetCurrentProcessId(), tjs.pid);
    close();
  },
  'kept result survives GC, called later from a timer (how the apps use it)': async () => {
    const k32 = dlopen('kernel32.dll', { SetThreadExecutionState: { args: [types.uint32], returns: types.uint32 }, GetCurrentProcessId: { returns: 'u32' } });
    const fn = () => k32.symbols.GetCurrentProcessId();
    gc();
    await new Promise((r) => setTimeout(r, 300));
    gc();
    eq(fn(), tjs.pid);
  },
  'symbols spread and called detached (kitchen-sink does this)': () => {
    const k = dlopen('kernel32.dll', { GetCurrentProcessId: { returns: 'u32' } });
    const f = { libs: [k], ...k.symbols };
    const g = k.symbols.GetCurrentProcessId;
    gc();
    eq(f.GetCurrentProcessId(), tjs.pid); eq(g(), tjs.pid);
  },
  'report: only symbols kept, result dropped, then GC': () => {
    const { symbols } = dlopen('kernel32.dll', { GetCurrentProcessId: { returns: 'u32' } });
    gc();
    eq(symbols.GetCurrentProcessId(), tjs.pid);
    return 'kernel32 stays mapped anyway, so this says little';
  },
  'non-ASCII DLL path': async () => {
    const dir = await tjs.makeTempDir(tjs.tmpDir + '\\ffi-XXXXXX');
    const sub = dir + '\\ünï-中文-😀';
    await tjs.makeDir(sub);
    const bytes = await tjs.readFile('C:\\Windows\\System32\\version.dll');
    await tjs.writeFile(sub + '\\v.dll', bytes);
    const v = dlopen(sub + '\\v.dll', { GetFileVersionInfoSizeA: { args: ['string', 'pointer'], returns: 'u32' } });
    const n = v.symbols.GetFileVersionInfoSizeA('C:\\Windows\\System32\\kernel32.dll', null);
    ok(n > 0, 'size 0');
    v.close();
    await tjs.remove(dir, { recursive: true }).catch(() => {});
    return `${n} bytes`;
  },
  'errno and strerror': () => {
    const s = strerror(2);
    ok(typeof s === 'string' && s.length > 0, 'strerror');
    const c = dlopen('ucrtbase.dll', { _open: { args: ['string', 'i32'], returns: 'i32' } });
    eq(c.symbols._open('C:\\no\\such\\file', 0), -1);
    return `strerror(2)="${s}", errno() after failed _open = ${errno()}`;
  },
  'throw inside a callback': () => {
    const c = dlopen('ucrtbase.dll', { qsort: { args: ['buffer', 'size_t', 'size_t', 'pointer'] } });
    const cmp = new JSCallback(types.sint, [types.pointer, types.pointer], () => { throw new Error('boom'); });
    const arr = new Int32Array([3, 1, 2]);
    try { c.symbols.qsort(new Uint8Array(arr.buffer), 3, 4, cmp.addr); } catch (e) { return `surfaced: ${e.message}`; }
    return 'swallowed, process alive';
  },
  'soak: 100k calls, 10k struct returns, 10k callbacks, 10k strings, twice; memory stays put': () => {
    const c = dlopen('ucrtbase.dll', { abs: { args: ['i32'], returns: 'i32' }, strlen: { args: ['string'], returns: 'size_t' }, div: { args: ['i32', 'i32'], returns: defineStruct([['quot', 'i32'], ['rem', 'i32']]) }, qsort: { args: ['buffer', 'size_t', 'size_t', 'pointer'] } });
    const k = dlopen('kernel32.dll', { K32GetProcessMemoryInfo: { args: ['pointer', 'buffer', 'u32'], returns: 'bool_u32' } });
    const self = createPointer(0xffffffffffffffffn); // GetCurrentProcess()'s pseudo-handle
    const ws = () => { // PROCESS_MEMORY_COUNTERS.WorkingSetSize
      const b = new Uint8Array(72);
      new DataView(b.buffer).setUint32(0, 72, true);
      eq(k.symbols.K32GetProcessMemoryInfo(self, b, 72), true, 'memory info');
      return Number(new DataView(b.buffer).getBigUint64(16, true));
    };
    let n = 0;
    const cmp = new JSCallback(types.sint, [types.pointer, types.pointer], (a, b) => { n++; return read.i32(a) - read.i32(b); });
    const batch = () => {
      for (let i = 0; i < 100000; i++) if (c.symbols.abs(-i) !== i) throw new Error('abs ' + i);
      for (let i = 0; i < 10000; i++) if (c.symbols.div(i, 7).rem !== i % 7) throw new Error('div ' + i);
      for (let i = 0; i < 10000; i++) c.symbols.strlen('some string ' + i);
      n = 0;
      while (n < 10000) c.symbols.qsort(new Uint8Array(Int32Array.from({ length: 500 }, (_, i) => (i * 37) % 501).buffer), 500, 4, cmp.addr);
      gc();
    };
    batch(); batch();
    const before = ws();
    batch(); batch();
    const after = ws();
    const grew = (after - before) / 1e6;
    ok(grew < 10, `working set grew ${grew.toFixed(1)} MB over 2 batches`);
    return `working set ${(before / 1e6).toFixed(1)} -> ${(after / 1e6).toFixed(1)} MB`;
  },
};

const names = Object.keys(CASES);
const at = tjs.args.indexOf('--case');
if (at >= 0) {
  const name = names[+tjs.args[at + 1]];
  try {
    const note = await CASES[name]();
    console.log(`OK ${note ?? ''}`);
    tjs.exit(0);
  } catch (e) {
    console.log(`FAIL ${e?.message ?? e}`);
    tjs.exit(1);
  }
}

const script = tjs.args[tjs.args.length - 1];
const dec = new TextDecoder();
const all = async (s) => { const r = s.getReader(); let o = ''; for (;;) { const { value, done } = await r.read(); if (done) return o; o += dec.decode(value); } };
console.log(`tjs ${tjs.version}, ${tjs.exePath}`);
let bad = 0;
for (let i = 0; i < names.length; i++) {
  const p = tjs.spawn([tjs.exePath, 'run', script, '--case', String(i)], { stdin: 'ignore', stdout: 'pipe', stderr: 'pipe' });
  let killed = false;
  const timer = setTimeout(() => { killed = true; try { p.kill(); } catch {} }, 60000);
  const [o, e] = await Promise.all([all(p.stdout), all(p.stderr)]);
  const st = await p.wait();
  clearTimeout(timer);
  const out = o.trim();
  const pass = st.exit_status === 0 && out.startsWith('OK');
  if (!pass) bad++;
  const hex = st.exit_status ? ` [exit ${st.exit_status} / 0x${(st.exit_status >>> 0).toString(16)}${st.term_signal ? ` sig ${st.term_signal}` : ''}${killed ? ' TIMEOUT' : ''}]` : '';
  console.log(`${pass ? 'ok  ' : 'FAIL'} ${names[i]}${out.replace(/^(OK|FAIL) ?/, '') ? ' — ' + out.replace(/^(OK|FAIL) ?/, '') : ''}${hex}${!pass && e.trim() ? '\n     ' + e.trim().slice(0, 300) : ''}`);
}
console.log(bad ? `\n${bad} of ${names.length} FAILED` : `\nall ${names.length} passed`);
tjs.exit(bad ? 1 : 0);
