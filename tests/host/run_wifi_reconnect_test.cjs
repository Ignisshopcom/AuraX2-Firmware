const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const cp = require('node:child_process');
const assert = require('node:assert/strict');
const root = path.resolve(__dirname, '../..');
const source = fs.readFileSync(path.join(root, 'src/wifi_control.cpp'), 'utf8');
const header = fs.readFileSync(path.join(root, 'src/wifi_control.h'), 'utf8');
function section(text, start, end) {
    const a = text.indexOf(start);
    const b = text.indexOf(end, a + start.length);
    assert(a >= 0 && b > a, `Missing source anchors: ${start}`);
    return text.slice(a, b);
}
const constants = [...header.matchAll(/^static constexpr uint32_t STA_.*;$/gm)].map(x => x[0]).join('\n');
const events = section(source, 'struct WifiEventSnapshot {', 'static void ensureWifiEventLogging()');
const retry = section(source, 'void WifiControl::enableManagedReconnect(', 'bool WifiControl::startSoftApRadio()');
const maintain = section(source, 'void WifiControl::maintainWifi()', 'void WifiControl::handle()');
// Optional check against the user-tested source, without depending on it to run tests.
if (process.env.AURAX_IPHONE_REFERENCE) {
    // Only the documented AUTH_EXPIRE fix may differ from the tested candidate.
    const reference = fs.readFileSync(process.env.AURAX_IPHONE_REFERENCE, 'utf8').replace(/\r/g, '')
        .replace('    if (_apActive || WiFi.status() == WL_CONNECTED) return;\n\n    WifiEventSnapshot snapshot = wifiEventSnapshot();',
            '    WifiEventSnapshot snapshot = wifiEventSnapshot();\n    // Arduino 2.0.6 can retain WL_CONNECTED after an AUTH_EXPIRE event.\n    if (_apActive || (WiFi.status() == WL_CONNECTED && snapshot.staAssociated)) return;')
        .replace('    if (WiFi.status() == WL_CONNECTED) {\n        _staDisconnectedSinceMs = 0;',
            '    if (WiFi.status() == WL_CONNECTED && wifiEventSnapshot().staAssociated) {\n        _staDisconnectedSinceMs = 0;');
    for (const [start, end] of [
        ['struct WifiEventSnapshot {', 'static void ensureWifiEventLogging()'],
        ['bool WifiControl::connectSta(', 'bool WifiControl::startSoftApRadio()'],
        ['void WifiControl::maintainWifi()', 'void WifiControl::handle()']
    ]) assert.equal(section(source, start, end).replace(/\r/g, ''), section(reference, start, end).replace(/\r/g, ''));
    console.log('PASS: reconnect matches iPhone source plus the documented AUTH_EXPIRE fix');
}
const temp = fs.mkdtempSync(path.join(os.tmpdir(), 'aurax-wifi-test-'));
try {
    fs.writeFileSync(path.join(temp, 'wifi_reconnect_source.inc'), constants + '\n' + events + retry + maintain);
    const exe = path.join(temp, process.platform === 'win32' ? 'wifi_test.exe' : 'wifi_test');
    const compiler = process.env.CXX || 'c++';
    const args = /zig(?:\.exe)?$/i.test(compiler) ? ['c++'] : [];
    args.push('-std=c++11', '-O0', '-I', temp, path.join(__dirname, 'wifi_reconnect_test.cpp'), '-o', exe);
    cp.execFileSync(compiler, args, {stdio: 'inherit'});
    cp.execFileSync(exe, [], {stdio: 'inherit'});
} finally {
    fs.rmSync(temp, {recursive: true, force: true});
}
