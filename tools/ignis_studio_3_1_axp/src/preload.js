const { contextBridge, ipcRenderer } = require('electron');
const childProcess = require('child_process');
const dgram = require('dgram');
const fs = require('fs');
const http = require('http');
const md5 = require('md5');
const NodeZip = require('node-zip');
const os = require('os');
const path = require('path');

function cloneStats(stats) {
  return {
    size: stats.size,
    mtimeMs: stats.mtimeMs,
    isFile: () => stats.isFile(),
    isDirectory: () => stats.isDirectory(),
  };
}

function getDriveRoots() {
  if (process.platform === 'win32') {
    const drives = [];

    for (let code = 65; code <= 90; code++) {
      const letter = String.fromCharCode(code);
      const root = `${letter}:\\`;

      try {
        if (fs.existsSync(root)) {
          drives.push({
            description: `Drive ${letter}:`,
            mountpoints: [{ path: root }],
          });
        }
      } catch (error) {
        // Some drives can exist but be temporarily unreadable.
      }
    }

    return drives;
  }

  if (process.platform === 'darwin') {
    const roots = [{ description: 'System', mountpoints: [{ path: '/' }] }];
    try {
      fs.readdirSync('/Volumes').forEach((name) => {
        roots.push({
          description: name,
          mountpoints: [{ path: path.join('/Volumes', name) }],
        });
      });
    } catch (error) {
      // /Volumes may be inaccessible in restricted environments.
    }
    return roots;
  }

  return [{ description: 'System', mountpoints: [{ path: '/' }] }];
}

function normalizeHost(host) {
  return String(host || '')
    .trim()
    .replace(/^https?:\/\//i, '')
    .replace(/\/.*$/, '')
    .replace(/:80$/, '');
}

function isPrivateIPv4(ip) {
  if (!/^\d+\.\d+\.\d+\.\d+$/.test(ip || '')) return false;
  const p = ip.split('.').map((v) => parseInt(v, 10));
  if (p.some((v) => Number.isNaN(v) || v < 0 || v > 255)) return false;
  if (p[0] === 10) return true;
  if (p[0] === 172 && p[1] >= 16 && p[1] <= 31) return true;
  if (p[0] === 192 && p[1] === 168) return true;
  return false;
}

function ipv4ToInt(ip) {
  return ip.split('.').reduce((acc, part) => ((acc << 8) + parseInt(part, 10)) >>> 0, 0);
}

function intToIPv4(value) {
  return [
    (value >>> 24) & 255,
    (value >>> 16) & 255,
    (value >>> 8) & 255,
    value & 255,
  ].join('.');
}

function getLocalAuraXSubnets() {
  const subnets = new Map();

  function add(address, netmask) {
    if (!isPrivateIPv4(address)) return;
    const parts = address.split('.');
    const prefix = parts.slice(0, 3).join('.');
    let broadcast = `${prefix}.255`;

    if (netmask && /^\d+\.\d+\.\d+\.\d+$/.test(netmask)) {
      try {
        const addr = ipv4ToInt(address);
        const mask = ipv4ToInt(netmask);
        broadcast = intToIPv4(((addr & mask) | (~mask >>> 0)) >>> 0);
      } catch (error) {
        broadcast = `${prefix}.255`;
      }
    }

    subnets.set(prefix, { prefix, broadcast });
  }

  const nets = os.networkInterfaces();
  for (const name of Object.keys(nets)) {
    for (const iface of nets[name] || []) {
      const family = iface.family === 4 || iface.family === 'IPv4';
      if (!family || iface.internal) continue;
      add(iface.address, iface.netmask);
    }
  }

  add('192.168.4.2', '255.255.255.0');
  return Array.from(subnets.values());
}

function httpRequestText(host, route, options) {
  const opts = options || {};
  const body = opts.body || null;
  const timeoutMs = opts.timeoutMs || 800;
  const hostname = normalizeHost(host);

  return new Promise((resolve, reject) => {
    if (!hostname) {
      reject(new Error('Missing host'));
      return;
    }

    const req = http.request({
      hostname,
      port: 80,
      path: route,
      method: opts.method || 'GET',
      headers: opts.headers || {},
      timeout: timeoutMs,
    }, (res) => {
      const chunks = [];
      res.on('data', (chunk) => chunks.push(Buffer.from(chunk)));
      res.on('end', () => {
        resolve({
          statusCode: res.statusCode || 0,
          headers: res.headers || {},
          body: Buffer.concat(chunks).toString('utf8'),
        });
      });
    });

    req.on('timeout', () => req.destroy(new Error('timeout')));
    req.on('error', reject);
    if (body) req.write(body);
    req.end();
  });
}

async function httpRequestJson(host, route, timeoutMs) {
  const res = await httpRequestText(host, route, { timeoutMs });
  if (res.statusCode < 200 || res.statusCode >= 300) {
    throw new Error(`HTTP ${res.statusCode}`);
  }
  return JSON.parse(res.body);
}

function wifiPctFromRssi(rssi) {
  const value = Number(rssi);
  if (!Number.isFinite(value) || value === 0) return null;
  if (value >= -50) return 100;
  if (value <= -100) return 0;
  return Math.max(0, Math.min(100, Math.round((value + 100) * 2)));
}

async function probeAuraXDevice(ip, timeoutMs, allowFallback) {
  let info = null;
  let status = null;
  let config = null;
  let programs = null;

  try { info = await httpRequestJson(ip, '/json/info', timeoutMs); } catch (error) {}

  if (info && !(info.cn === 'AuraX' || info.brand === 'AuraX' || info.product === 'AuraX')) {
    return null;
  }

  if (!info && !allowFallback) {
    return null;
  }

  try { status = await httpRequestJson(ip, '/status', timeoutMs); } catch (error) {}
  try { config = await httpRequestJson(ip, '/config', timeoutMs); } catch (error) {}
  try { programs = await httpRequestJson(ip, '/programs', timeoutMs); } catch (error) {}

  const isAuraX = !!(
    (info && (info.cn === 'AuraX' || info.brand === 'AuraX' || info.product === 'AuraX')) ||
    (status && (status.hostname || status.device_name)) ||
    (config && Number(config.numLeds))
  );

  if (!isAuraX) return null;

  const numLeds = Number(
    (config && config.numLeds) ||
    (info && info.leds && info.leds.count) ||
    0
  );
  const rssi = Number(
    (status && status.rssi) ||
    (info && info.wifi && info.wifi.rssi) ||
    0
  );
  const wifiPct = Number(
    (info && info.wifi && info.wifi.signal) ||
    wifiPctFromRssi(rssi) ||
    0
  );
  const batteryPctRaw = status && status.battery_pct;
  const batteryMvRaw = status && status.battery_mv;
  const fsTotal = Number(
    (programs && programs.total) ||
    (info && info.fs && info.fs.t) ||
    0
  );
  const fsUsed = Number(
    (programs && programs.used) ||
    (info && info.fs && info.fs.u) ||
    0
  );
  const fsFree = Number(
    (programs && programs.free) ||
    (fsTotal > fsUsed ? fsTotal - fsUsed : 0)
  );
  const deviceName = (
    (config && (config.deviceName || config.hostname)) ||
    (status && (status.device_name || status.hostname)) ||
    (info && info.name) ||
    'AuraX'
  );

  return {
    ip: (status && status.ip) || (info && info.ip) || ip,
    host: normalizeHost(ip),
    deviceName,
    hostname: deviceName,
    numLeds,
    batteryPct: Number.isFinite(Number(batteryPctRaw)) ? Number(batteryPctRaw) : null,
    batteryMv: Number.isFinite(Number(batteryMvRaw)) ? Number(batteryMvRaw) : null,
    rssi,
    wifiPct,
    fsFree,
    fsUsed,
    fsTotal,
    programs: programs && Array.isArray(programs.files) ? programs.files : [],
  };
}

function discoverAuraXUdp(timeoutMs) {
  return new Promise((resolve) => {
    const found = new Set();
    const socket = dgram.createSocket({ type: 'udp4', reuseAddr: true });
    let finished = false;

    function finish() {
      if (finished) return;
      finished = true;
      try { socket.close(); } catch (error) {}
      resolve(Array.from(found));
    }

    socket.on('message', (msg, rinfo) => {
      const text = msg.toString('utf8').trim();
      const parts = text.split(/\s+/);
      if (parts[0] !== 'AURAX') return;
      const ip = parts[2] || rinfo.address;
      if (isPrivateIPv4(ip)) found.add(ip);
    });
    socket.on('error', finish);
    socket.bind(4210, () => {
      try { socket.setBroadcast(true); } catch (error) {}
      const packet = Buffer.from('AURAX?');
      const broadcasts = new Set(['255.255.255.255']);
      for (const subnet of getLocalAuraXSubnets()) broadcasts.add(subnet.broadcast);
      for (const broadcast of broadcasts) {
        try { socket.send(packet, 0, packet.length, 4210, broadcast); } catch (error) {}
      }
    });

    setTimeout(finish, timeoutMs || 900);
  });
}

function probeIpList(ips, timeoutMs, concurrency, allowFallback) {
  return new Promise((resolve) => {
    const out = [];
    let index = 0;
    let active = 0;

    function runNext() {
      while (active < concurrency && index < ips.length) {
        const ip = ips[index++];
        active++;
        probeAuraXDevice(ip, timeoutMs, allowFallback)
          .then((device) => { if (device) out.push(device); })
          .catch(() => {})
          .finally(() => {
            active--;
            if (index >= ips.length && active === 0) {
              resolve(out);
            } else {
              runNext();
            }
          });
      }

      if (ips.length === 0) resolve(out);
    }

    runNext();
  });
}

async function scanAuraXDevices(timeoutMs) {
  const scanTimeout = Math.max(800, Math.min(Number(timeoutMs) || 2600, 7000));
  const candidates = new Set(['192.168.4.1', 'aurax.local']);

  for (const subnet of getLocalAuraXSubnets()) {
    for (let host = 1; host <= 254; host++) {
      candidates.add(`${subnet.prefix}.${host}`);
    }
  }

  const udpPromise = discoverAuraXUdp(Math.min(scanTimeout, 1200));
  const scanned = await probeIpList(Array.from(candidates), 650, 64, false);
  const udpIps = await udpPromise;
  const knownHosts = new Set(scanned.map((device) => normalizeHost(device.host || device.ip)));
  const udpOnly = udpIps.filter((ip) => !knownHosts.has(normalizeHost(ip)));
  const udpDevices = await probeIpList(udpOnly, 900, 16, true);

  const devicesByIp = new Map();
  for (const device of scanned.concat(udpDevices)) {
    devicesByIp.set(normalizeHost(device.ip || device.host), device);
  }

  return Array.from(devicesByIp.values()).sort((a, b) => {
    return String(a.deviceName || a.ip).localeCompare(String(b.deviceName || b.ip));
  });
}

function uploadAuraXProgram(host, filename, data) {
  const targetHost = normalizeHost(host);
  const payload = Buffer.isBuffer(data) ? data : Buffer.from(data);
  const safeName = path.basename(String(filename || 'program.axp')).replace(/"/g, '');
  const boundary = `----IgnisAuraX${Date.now().toString(16)}`;
  const head = Buffer.from(
    `--${boundary}\r\n` +
    `Content-Disposition: form-data; name="file"; filename="${safeName}"\r\n` +
    `Content-Type: application/octet-stream\r\n\r\n`,
    'utf8'
  );
  const tail = Buffer.from(`\r\n--${boundary}--\r\n`, 'utf8');
  const body = Buffer.concat([head, payload, tail]);

  return httpRequestText(targetHost, '/program/upload', {
    method: 'POST',
    timeoutMs: 30000,
    body,
    headers: {
      'Content-Type': `multipart/form-data; boundary=${boundary}`,
      'Content-Length': body.length,
    },
  }).then((res) => ({
    ok: res.statusCode >= 200 && res.statusCode < 300,
    statusCode: res.statusCode,
    body: res.body,
  }));
}

function postAuraXJson(host, route, payload, timeoutMs) {
  const body = Buffer.from(JSON.stringify(payload || {}), 'utf8');
  return httpRequestText(normalizeHost(host), route, {
    method: 'POST',
    timeoutMs: timeoutMs || 5000,
    body,
    headers: {
      'Content-Type': 'application/json',
      'Content-Length': body.length,
    },
  }).then((res) => ({
    ok: res.statusCode >= 200 && res.statusCode < 300,
    statusCode: res.statusCode,
    body: res.body,
  }));
}

function refreshAuraXDevice(host, timeoutMs) {
  return probeAuraXDevice(normalizeHost(host), timeoutMs || 1000, true);
}

function identifyAuraXDevice(host) {
  return postAuraXJson(host, '/identify', {}, 2500);
}

function reorderAuraXProgram(host, slot, direction) {
  return postAuraXJson(host, '/program/reorder', {
    slot: Number(slot),
    direction: Number(direction),
  }, 5000);
}

function deleteAuraXProgram(host, file) {
  return postAuraXJson(host, '/program/delete', { file: String(file || '') }, 5000);
}

const api = {
  dialogOpen(params) {
    return ipcRenderer.sendSync('dialog-open', JSON.stringify(params || {}));
  },
  dialogSave(params) {
    return ipcRenderer.sendSync('dialog-save', JSON.stringify(params || {}));
  },
  getAppPath() {
    return ipcRenderer.sendSync('get-app-path');
  },
  help() {
    ipcRenderer.send('help');
  },
  quit() {
    ipcRenderer.send('app-quit');
  },
  minimize() {
    ipcRenderer.send('app-minimize');
  },
  maximize() {
    ipcRenderer.send('app-maximize');
  },
  devtools() {
    ipcRenderer.send('devtools');
  },
  onOpenProject(callback) {
    if (typeof callback !== 'function') return;
    ipcRenderer.on('open-project-file', (event, filePath) => {
      callback(filePath);
    });
  },
  getPendingProjectFile() {
    return ipcRenderer.sendSync('get-pending-project-file');
  },
  setWindowPosition(position) {
    ipcRenderer.send('win-set-position', position);
  },
  platform: process.platform,
  resourcesPath: process.resourcesPath,
  appDataPath: process.env.APPDATA || '',
  homeDir: os.homedir(),
  md5(value) {
    return md5(value);
  },
  listDrives() {
    return Promise.resolve(getDriveRoots());
  },
  readProjectZip(zipData) {
    const zip = new NodeZip().load(zipData);
    return {
      projectText: zip.file('project.json').asText(),
      image(hash) {
        return zip.folder('img').file(hash).asBinary();
      },
      audio(hash) {
        return zip.folder('audio').file(hash).asBinary();
      },
    };
  },
  createProjectZip(projectText, imageFiles, audioFile) {
    const zip = new NodeZip();
    zip.file('project.json', projectText);

    for (const hash in (imageFiles || {})) {
      zip.folder('img').file(hash, imageFiles[hash], { binary: true });
    }

    if (audioFile && audioFile.hash && audioFile.data) {
      zip.folder('audio').file(audioFile.hash, audioFile.data, { binary: true });
    }

    return zip.generate({ base64: false, compression: 'DEFLATE' });
  },
  execFile(command, args) {
    return new Promise((resolve) => {
      childProcess.execFile(command, args || [], (error, stdout, stderr) => {
        resolve({
          error: error ? {
            message: error.message,
            code: error.code,
            signal: error.signal,
          } : null,
          stdout: stdout,
          stderr: stderr,
        });
      });
    });
  },
  scanAuraXDevices(timeoutMs) {
    return scanAuraXDevices(timeoutMs);
  },
  uploadAuraXProgram(host, filename, data) {
    return uploadAuraXProgram(host, filename, data);
  },
  refreshAuraXDevice(host, timeoutMs) {
    return refreshAuraXDevice(host, timeoutMs);
  },
  identifyAuraXDevice(host) {
    return identifyAuraXDevice(host);
  },
  reorderAuraXProgram(host, slot, direction) {
    return reorderAuraXProgram(host, slot, direction);
  },
  deleteAuraXProgram(host, file) {
    return deleteAuraXProgram(host, file);
  },
  fs: {
    existsSync(filePath) {
      return fs.existsSync(filePath);
    },
    mkdirSync(dirPath, options) {
      return fs.mkdirSync(dirPath, options);
    },
    readdirSync(dirPath) {
      return fs.readdirSync(dirPath);
    },
    readFileSync(filePath, encoding) {
      return fs.readFileSync(filePath, encoding);
    },
    writeFileSync(filePath, data, encoding) {
      return fs.writeFileSync(filePath, data, encoding);
    },
    writeFile(filePath, data, callback) {
      fs.writeFile(filePath, data, function (error) {
        if (callback) callback(error || null);
      });
    },
    unlinkSync(filePath) {
      return fs.unlinkSync(filePath);
    },
    renameSync(from, to) {
      return fs.renameSync(from, to);
    },
    copyFileSync(from, to) {
      return fs.copyFileSync(from, to);
    },
    statSync(filePath) {
      return cloneStats(fs.statSync(filePath));
    },
  },
  path: {
    sep: path.sep,
    basename(filePath) {
      return path.basename(filePath);
    },
    extname(filePath) {
      return path.extname(filePath);
    },
  },
};

if (process.contextIsolated) {
  contextBridge.exposeInMainWorld('ignisElectron', api);
} else {
  window.ignisElectron = api;
  window.Buffer = Buffer;
}
