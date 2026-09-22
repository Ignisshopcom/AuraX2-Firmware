const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const cp = require('node:child_process');
const assert = require('node:assert/strict');
const root = path.resolve(__dirname, '../..');
const source = fs.readFileSync(path.join(root, 'src/wifi_control.cpp'), 'utf8');
const start = source.indexOf('void WifiControl::sendPhotonProgramCommand(');
const end = source.indexOf('void WifiControl::fanoutStop()', start);
assert(start >= 0 && end > start);
// Outgoing Photon commands must remain confined to explicit relay fanout.
const calls = [...source.matchAll(/sendPhotonProgramCommand\((true|false)[^;]*;/g)].map(x => x[0]);
assert.deepEqual(calls, ['sendPhotonProgramCommand(false);', 'sendPhotonProgramCommand(true, slot);']);
assert.match(source, /void WifiControl::fanoutStop\(\)\s*\{\s*sendPhotonProgramCommand\(false\);/);
assert.match(source, /void WifiControl::fanoutProgramStart\([^]*?if \(slot == 0\) return;\s*sendPhotonProgramCommand\(true, slot\);/);
const sync = fs.readFileSync(path.join(root, 'src/sync_control.cpp'), 'utf8');
assert(!sync.includes('PhotonProtocol') && !sync.includes('sendPhotonProgramCommand'));
const temp = fs.mkdtempSync(path.join(os.tmpdir(), 'aurax-photon-test-'));
try {
    fs.writeFileSync(path.join(temp, 'photon_send_source.inc'), source.slice(start, end));
    const exe = path.join(temp, process.platform === 'win32' ? 'photon_test.exe' : 'photon_test');
    const compiler = process.env.CXX || 'c++';
    const args = /zig(?:\.exe)?$/i.test(compiler) ? ['c++'] : [];
    args.push('-std=c++11', '-O0', '-I', temp, '-I', path.join(root, 'src'), path.join(__dirname, 'photon_udp_test.cpp'), '-o', exe);
    cp.execFileSync(compiler, args, {stdio: 'inherit'});
    cp.execFileSync(exe, [], {stdio: 'inherit'});
    console.log('PASS: Photon sends only in explicit program START/STOP fanout, not ESP-NOW reception');
} finally {
    fs.rmSync(temp, {recursive: true, force: true});
}
