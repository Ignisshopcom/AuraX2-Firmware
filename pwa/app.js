const STORAGE_KEY = "aurax-finder-devices-v1";
const DEFAULT_TARGETS = ["aurax.local", "aurax", "192.168.4.1", "4.3.2.1"];

const devices = new Map();
let deferredInstallPrompt = null;

const statusEl = document.querySelector("#status");
const hintEl = document.querySelector("#hint");
const listEl = document.querySelector("#deviceList");
const template = document.querySelector("#deviceTemplate");
const scanBtn = document.querySelector("#scanBtn");
const installBtn = document.querySelector("#installBtn");
const manualForm = document.querySelector("#manualForm");
const addressInput = document.querySelector("#addressInput");

loadDevices();
renderDevices();
setStatus(navigator.onLine ? "Ready" : "Offline");
setHint("");

if ("serviceWorker" in navigator) {
  navigator.serviceWorker.register("./sw.js").catch(() => {});
}

window.addEventListener("online", () => setStatus("Online"));
window.addEventListener("offline", () => setStatus("Offline"));

window.addEventListener("beforeinstallprompt", (event) => {
  event.preventDefault();
  deferredInstallPrompt = event;
  installBtn.hidden = false;
});

installBtn.addEventListener("click", async () => {
  if (!deferredInstallPrompt) return;
  deferredInstallPrompt.prompt();
  await deferredInstallPrompt.userChoice.catch(() => null);
  deferredInstallPrompt = null;
  installBtn.hidden = true;
});

scanBtn.addEventListener("click", () => scanDevices());

manualForm.addEventListener("submit", async (event) => {
  event.preventDefault();
  const host = normalizeAddress(addressInput.value);
  if (!host) return;
  addressInput.value = "";
  setStatus(`Checking ${host}...`);
  const found = await probeDevice(host);
  if (!found) {
    addDevice({ hostname: displayName(host), ip: host, source: "manual" });
    setStatus(`Added ${displayName(host)}`);
  }
});

document.querySelectorAll("[data-open]").forEach((button) => {
  button.addEventListener("click", () => openAddress(button.dataset.open));
});

async function scanDevices() {
  setStatus("Scanning...");
  setHint("");
  const targets = scanTargets();
  let found = 0;
  await Promise.all(targets.map(async (target) => {
    if (await probeDevice(target)) found++;
  }));
  if (found > 0) {
    setStatus(deviceCountText());
  } else if (devices.size > 0) {
    setStatus("No new devices found");
    setHint(scanBlockedHint());
  } else {
    addFallbackDevices();
    setStatus("Direct scan blocked");
    setHint(scanBlockedHint());
  }
}

function scanTargets() {
  const saved = Array.from(devices.values()).flatMap((device) => [device.ip, device.hostname]);
  return Array.from(new Set([...DEFAULT_TARGETS, ...saved].map(normalizeAddress).filter(Boolean)));
}

function addFallbackDevices() {
  [
    { hostname: "Try aurax.local", ip: "aurax.local" },
    { hostname: "Try setup AP", ip: "192.168.4.1" },
    { hostname: "Try captive portal", ip: "4.3.2.1" },
  ].forEach((device) => addDevice({ ...device, source: "fallback" }));
}

async function probeDevice(host) {
  try {
    const response = await fetchWithTimeout(`http://${host}/status`, 1400);
    if (!response.ok) return false;
    const json = await response.json();
    addDevice({
      hostname: json.device_name || json.hostname || host,
      ip: json.ip || host,
      battery: json.battery_pct,
      sync: json.sync_channel,
      rssi: json.rssi,
      lastSeen: Date.now(),
      source: "status",
    });
    return true;
  } catch {
  }

  if (await probeReachable(host)) {
    addDevice({
      hostname: displayName(host),
      ip: host,
      source: "reachable",
    });
    return true;
  }

  return false;
}

async function probeReachable(host) {
  try {
    await fetchWithTimeout(`http://${host}/`, 1400, { mode: "no-cors" });
    return true;
  } catch {
    return false;
  }
}

async function fetchWithTimeout(url, timeoutMs, options = {}) {
  const controller = new AbortController();
  const timer = window.setTimeout(() => controller.abort(), timeoutMs);
  try {
    return await fetch(url, {
      cache: "no-store",
      mode: "cors",
      ...options,
      signal: controller.signal,
    });
  } finally {
    window.clearTimeout(timer);
  }
}

function addDevice(device) {
  const ip = normalizeAddress(device.ip || device.hostname);
  if (!ip) return;
  const hostname = normalizeAddress(device.hostname) || ip;
  devices.set(ip, {
    hostname,
    ip,
    battery: cleanValue(device.battery),
    sync: cleanValue(device.sync),
    rssi: cleanValue(device.rssi),
    lastSeen: device.lastSeen || Date.now(),
    source: device.source || "manual",
  });
  saveDevices();
  renderDevices();
}

function renderDevices() {
  listEl.replaceChildren();
  if (devices.size === 0) {
    const empty = document.createElement("p");
    empty.className = "empty";
    empty.textContent = "No devices found";
    listEl.append(empty);
    return;
  }

  Array.from(devices.values())
    .sort((a, b) => displayName(a.hostname).localeCompare(displayName(b.hostname)))
    .forEach((device) => {
      const node = template.content.firstElementChild.cloneNode(true);
      node.querySelector(".device-name").textContent = displayName(device.hostname);
      node.querySelector(".battery").textContent = `Battery ${valueOrDash(device.battery, "%")}`;
      node.querySelector(".sync").textContent = syncText(device.sync);
      node.querySelector(".wifi").textContent = wifiText(device.rssi);
      node.querySelector(".device-main").addEventListener("click", () => openAddress(device.ip));
      node.querySelector(".refresh").addEventListener("click", () => refreshDevice(device));
      node.querySelector(".remove").addEventListener("click", () => removeDevice(device.ip));
      listEl.append(node);
    });
}

async function refreshDevice(device) {
  setStatus(`Checking ${displayName(device.hostname)}...`);
  if (await probeDevice(device.ip)) setStatus(deviceCountText());
  else setStatus(`Could not refresh ${displayName(device.hostname)}`);
}

function removeDevice(ip) {
  devices.delete(ip);
  saveDevices();
  renderDevices();
  setStatus(devices.size ? deviceCountText() : "No devices found");
  if (!devices.size) setHint("");
}

function openAddress(host) {
  const address = normalizeAddress(host);
  if (!address) return;
  window.location.href = `http://${address}/`;
}

function loadDevices() {
  try {
    const stored = JSON.parse(localStorage.getItem(STORAGE_KEY) || "[]");
    if (!Array.isArray(stored)) return;
    stored.forEach(addDevice);
  } catch {
    localStorage.removeItem(STORAGE_KEY);
  }
}

function saveDevices() {
  localStorage.setItem(STORAGE_KEY, JSON.stringify(Array.from(devices.values())));
}

function normalizeAddress(value) {
  if (!value) return "";
  let out = String(value).trim();
  out = out.replace(/^https?:\/\//i, "");
  out = out.replace(/\/.*$/, "");
  return out.toLowerCase();
}

function displayName(host) {
  const name = normalizeAddress(host) || "aurax";
  return name.endsWith(".local") ? name.slice(0, -6) : name;
}

function cleanValue(value) {
  if (value === undefined || value === null || value === "") return "";
  return String(value);
}

function valueOrDash(value, suffix) {
  return value === "" ? "--" : `${value}${suffix}`;
}

function syncText(value) {
  return value === "" || value === "0" ? "Sync off" : `Sync ${value}`;
}

function wifiText(value) {
  return value === "" || value === "0" ? "Wi-Fi --" : `Wi-Fi ${value} dBm`;
}

function deviceCountText() {
  return devices.size === 1 ? "1 device found" : `${devices.size} devices found`;
}

function setStatus(text) {
  statusEl.textContent = text;
}

function setHint(text) {
  hintEl.textContent = text;
  hintEl.hidden = !text;
}

function scanBlockedHint() {
  return "Mobile browsers often block HTTPS pages from scanning local HTTP devices. Tap a device card to open it, or enter the IP address shown by your router/hotspot.";
}
