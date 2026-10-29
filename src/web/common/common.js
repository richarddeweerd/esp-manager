"use strict";

// Shared by every page. The page text is filled in from src/web/strings.en.json by
// scripts/build-web.py.
const STRINGS = /*STRINGS*/ {};

const esc = (text) => String(text).replace(/[&<>"']/g,
  (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" }[c]));

async function getJson(path) {
  const response = await fetch(path, { cache: "no-store" });
  if (!response.ok) throw new Error(`${path}: ${response.status}`);
  return response.json();
}

async function postForm(path, fields) {
  const response = await fetch(path, { method: "POST", body: new URLSearchParams(fields) });
  return { ok: response.ok, data: await response.json() };
}

function bars(rssi) {
  const level = rssi >= -55 ? 4 : rssi >= -65 ? 3 : rssi >= -75 ? 2 : 1;
  return `<span class="bars" title="${rssi} dBm">${[1, 2, 3, 4].map((i) =>
    `<i class="${i <= level ? "on" : ""}"></i>`).join("")}</span>`;
}

function formatUptime(ms) {
  const minutes = Math.floor(ms / 60000);
  const days = Math.floor(minutes / 1440);
  const hours = Math.floor((minutes % 1440) / 60);
  const clock = `${String(hours).padStart(2, "0")}:${String(minutes % 60).padStart(2, "0")}`;
  return days ? `${days} ${STRINGS.days} ${clock}` : clock;
}

// The device's status (GET /api/status) as a list of labels and values.
function statusList(s) {
  const connected = s.connection === "Connected";
  const rows = [
    [STRINGS.connection, STRINGS.states[s.connection] || s.connection],
    [STRINGS.wifi, s.ssid || "–"],
    [STRINGS.ipAddress, connected ? s.ip : "–"],
    [STRINGS.gateway, connected ? s.gateway : "–"],
    [STRINGS.signal, connected ? `${bars(s.rssi)} ${s.rssi} dBm` : "–", true],
    [STRINGS.channel, connected ? s.channel : "–"],
    [STRINGS.hostname, s.hostname],
    [STRINGS.mac, s.mac],
    [STRINGS.uptime, formatUptime(s.uptimeMs)],
    [STRINGS.memory, `${Math.round(s.freeHeap / 1024)} ${STRINGS.kbFree}`],
    [STRINGS.chip, s.chip],
    [STRINGS.firmware, s.firmware || "–"],
    [STRINGS.library, `EspManager ${s.library}`],
  ];
  return `<dl class="kv">${rows.map(([label, value, html]) =>
    `<dt>${esc(label)}</dt><dd>${html ? value : esc(value)}</dd>`).join("")}</dl>`;
}
