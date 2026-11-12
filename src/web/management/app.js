"use strict";

const app = document.getElementById("app");

// What the page shows. Everything comes from the device; a reload starts over.
const S = {
  tab: "wifi",
  status: null,
  // GET /api/history, loaded with the status while the Status tab is open.
  history: null,
  // The Saved Networks in the order they're tried, how many fit, and the one in use.
  networks: null,
  max: 0,
  connected: "",
  // Why the last change to the list was refused.
  listError: "",
  error: false,
  // The Device tab's action waiting for confirmation, and where it stands: "confirm", "busy",
  // "done" or "failed".
  action: null,
  actionState: "",
  // The Wi-Fi tab's sheet: { kind: "add", ssid, password, show, error, busy } or
  // { kind: "remove", ssid, error, busy }.
  netSheet: null,
  // The Settings this site shows (null until loaded), for the common form code. settingsLoaded is
  // false while the Settings tab waits for fresh values, settingsError holds why a save failed,
  // and settingsNote says that one worked.
  settings: null,
  values: {},
  clears: {},
  settingErrors: {},
  showPassword: false,
  settingsLoaded: false,
  settingsError: "",
  settingsNote: "",
  // Synchronise now: "", "busy", "started" or "failed".
  syncState: "",
};

// What the Device tab can do: [request, strings key, whether it erases something].
const ACTIONS = {
  restart: ["/api/restart", "restart", false],
  settingsReset: ["/api/settings-reset", "settingsReset", true],
  networkReset: ["/api/network-reset", "networkReset", true],
  factoryReset: ["/api/factory-reset", "factoryReset", true],
};

// --- Talking to the device ---

// Keeps the status up to date: every tab shows part of it.
async function loadStatus() {
  for (;;) {
    const first = !S.status;
    try {
      S.status = await getJson("/api/status");
      if (S.tab === "status") S.history = await getJson("/api/history");
      S.error = false;
    } catch (error) {
      S.error = true;
    }
    // Drawing again would throw away what the Owner is typing.
    if (first || !typing()) render();
    await new Promise((resolve) => setTimeout(resolve, 5000));
  }
}

async function loadHistory() {
  try {
    S.history = await getJson("/api/history");
  } catch (error) {
    S.error = true;
  }
  if (S.tab === "status") render();
}

async function runAction(key) {
  S.actionState = "busy";
  render();
  try {
    S.actionState = (await postForm(ACTIONS[key][0], {})).ok ? "done" : "failed";
  } catch (error) {
    S.actionState = "failed";
  }
  if (key === "settingsReset") loadSettings();
  render();
}

const typing = () => S.netSheet || S.tab === "settings";

async function syncNow() {
  S.syncState = "busy";
  render();
  try {
    S.syncState = (await postForm("/api/time/sync", {})).ok ? "started" : "failed";
  } catch (error) {
    S.syncState = "failed";
  }
  render();
}

async function loadSettings() {
  try {
    showSettings(S, await getJson("/api/settings"));
    S.settingsLoaded = true;
  } catch (error) {
    setTimeout(loadSettings, 1500);
    return;
  }
  if (!S.netSheet) render();
}

async function saveSettings() {
  readSettingFields(S, app);
  S.settingErrors = {};
  S.settingsError = "";
  S.settingsNote = "";
  try {
    const answer = await postForm("/api/settings", settingFormFields(S, S.settings.sections));
    if (answer.ok) {
      showSettings(S, answer.data);
      S.settingsNote = STRINGS.settingsSaved;
    } else if (answer.data.errors) showSettingErrors(S, answer.data.errors);
    else S.settingsError = STRINGS.errors.actionFailed;
  } catch (error) {
    S.settingsError = STRINGS.errors.unreachableManagement;
  }
  render();
}

function showNetworks(data) {
  S.networks = data.networks;
  S.max = data.max;
  S.connected = data.connected;
}

async function loadNetworks() {
  try {
    showNetworks(await getJson("/api/networks"));
  } catch (error) {
    S.error = true;
  }
  if (!typing()) render();
}

// Sends a change to the list. Every answer that isn't a refusal is the new list. Returns the
// refusal's text, or "" when it worked.
async function changeNetworks(path, fields) {
  let answer;
  try {
    answer = await postForm(path, fields);
  } catch (error) {
    return STRINGS.errors.unreachableManagement;
  }
  if (answer.ok) {
    showNetworks(answer.data);
    return "";
  }
  const code = answer.data.error || "";
  if (code === "not-found") loadNetworks();
  return STRINGS.errors[code] || STRINGS.errors.actionFailed;
}

async function moveNetwork(from, to) {
  S.listError = await changeNetworks("/api/networks/move", { ssid: S.networks[from].ssid, position: to });
  render();
}

async function saveNetwork() {
  const sheet = S.netSheet;
  readSheet();
  if (!sheet.ssid) {
    sheet.error = STRINGS.errors["ssid-empty"];
    render();
    return;
  }
  sheet.busy = true;
  render();
  sheet.error = await changeNetworks("/api/networks/add", { ssid: sheet.ssid, password: sheet.password });
  sheet.busy = false;
  if (!sheet.error) S.netSheet = null;
  render();
}

async function removeNetwork() {
  const sheet = S.netSheet;
  sheet.busy = true;
  render();
  sheet.error = await changeNetworks("/api/networks/remove", { ssid: sheet.ssid });
  sheet.busy = false;
  if (!sheet.error) S.netSheet = null;
  render();
}

// Keeps what the Owner typed in the add sheet when it's drawn again.
function readSheet() {
  const ssid = document.getElementById("ssid");
  const password = document.getElementById("password");
  if (ssid) S.netSheet.ssid = ssid.value;
  if (password) S.netSheet.password = password.value;
}

// --- Tabs ---

function tile(label, value, html) {
  return `<div class="tile"><div class="k">${esc(label)}</div>
    <div class="v">${html ? value : esc(value)}</div></div>`;
}

// --- The Connection History, newest first ---

function formatAgo(ms) {
  const minutes = Math.floor(ms / 60000);
  if (minutes < 1) return STRINGS.justNow;
  const days = Math.floor(minutes / 1440);
  const hours = Math.floor((minutes % 1440) / 60);
  const parts = days ? [`${days} ${STRINGS.days}`, `${hours} ${STRINGS.hours}`]
    : hours ? [`${hours} ${STRINGS.hours}`, `${minutes % 60} ${STRINGS.minutes}`]
      : [`${minutes} ${STRINGS.minutes}`];
  return STRINGS.ago.replace("{time}", parts.join(" "));
}

function eventText(event) {
  const texts = STRINGS.historyEvents;
  const reason = STRINGS.failureReasons[event.reason] || event.reason;
  const text = event.kind === "disconnected" && event.reason === "none" ? texts.left
    : texts[event.kind] || event.kind;
  return text.replace("{ssid}", event.ssid || "").replace("{reason}", reason || "");
}

// A moment as the device's local time, in the Owner's zone when there is one, else the phone's.
function formatMoment(epochMs, withDate) {
  const time = S.status && S.status.time;
  const options = withDate ? { dateStyle: "medium", timeStyle: "short" } : { timeStyle: "short" };
  try {
    if (time && time.zone && time.dst) options.timeZone = time.zone;
    return new Date(epochMs).toLocaleString(undefined, options);
  } catch (error) {
    return new Date(epochMs).toLocaleString();
  }
}

function historyRows() {
  const h = S.history;
  if (!h) return `<div class="row muted">${esc(STRINGS.loading)}</div>`;
  if (!h.events.length) return `<div class="row muted">${esc(STRINGS.noEvents)}</div>`;
  // Once the clock is synchronised, events show when they happened instead of how long ago.
  const time = S.status && S.status.time;
  const clock = time && time.synchronised ? time.epoch * 1000 - h.uptimeMs : null;
  return h.events.slice().reverse().map((event) => {
    const bad = event.kind === "connect-failed" || (event.kind === "disconnected" && event.reason !== "none");
    const signal = event.kind === "connected" ? `&nbsp;${bars(event.rssi)}` : "";
    const when = clock === null ? formatAgo(h.uptimeMs - event.uptimeMs)
      : formatMoment(clock + event.uptimeMs, h.uptimeMs - event.uptimeMs > 86400000);
    return `<div class="row${bad ? " bad" : ""}"><span class="name">${esc(eventText(event))}${signal}</span>
      <span class="muted">${esc(when)}</span></div>`;
  }).join("");
}

// The Time group: Network Time's state, and Synchronise now.
function timeRows(t) {
  const local = t.localTime ? t.localTime.replace("T", " ").replace(/([+-]\d{2}:\d{2}|Z)$/, " (UTC$1)") : "–";
  const zone = t.zone ? (t.dst ? zoneLabel(t.zone) : STRINGS.noDst.replace("{zone}", zoneLabel(t.zone))) : t.posix;
  const rows = [
    [STRINGS.synchronised, t.synchronised ? STRINGS.yes : STRINGS.no],
    [STRINGS.localTime, local],
    [STRINGS.timeZone, zone],
    [STRINGS.lastSync, t.lastSync && t.synchronised ? formatAgo((t.epoch - t.lastSync) * 1000) : "–"],
    [STRINGS.timeServer, t.server || "–"],
  ];
  const note = S.syncState === "started" ? `<p class="muted hint">${esc(STRINGS.syncStarted)}</p>`
    : S.syncState === "failed" ? `<p class="error">${esc(STRINGS.syncFailed)}</p>` : "";
  return `<div class="rows"><dl class="kv">${rows.map(([label, value]) =>
    `<dt>${esc(label)}</dt><dd>${esc(value)}</dd>`).join("")}</dl></div>
    <button class="btn secondary block" data-sync ${S.syncState === "busy" ? "disabled" : ""}>${esc(STRINGS.syncNow)}</button>${note}`;
}

const tabs = {
  wifi(s) {
    const connected = s.connection === "Connected";
    const hero = `<div class="hero ${connected ? "" : "idle"}">
      <div class="sub">${esc(connected ? STRINGS.connectedTo : STRINGS.states[s.connection] || s.connection)}</div>
      <div class="big">${esc(s.ssid || "–")}</div>
      <div class="sub">${connected ? `${esc(s.ip)} · ${bars(s.rssi)} ${s.rssi} dBm` : ""}</div></div>`;
    if (S.networks === null) {
      return `${hero}<h3>${esc(STRINGS.savedNetworks)}</h3>
        <div class="rows"><div class="row muted">${esc(STRINGS.loading)}</div></div>`;
    }
    const last = S.networks.length - 1;
    const rows = S.networks.map((network, i) => `<div class="row"><span class="muted">${i + 1}</span>
      <span class="name">${esc(network.ssid)}${network.ssid === S.connected
        ? ` <span class="badge">${esc(STRINGS.inUse)}</span>` : ""}</span>
      <button class="icon" data-move="${i}" data-to="${i - 1}" title="${esc(STRINGS.moveUp)}"
        aria-label="${esc(STRINGS.moveUp)}" ${i === 0 ? "disabled" : ""}>↑</button>
      <button class="icon" data-move="${i}" data-to="${i + 1}" title="${esc(STRINGS.moveDown)}"
        aria-label="${esc(STRINGS.moveDown)}" ${i === last ? "disabled" : ""}>↓</button>
      <button class="icon bad" data-remove="${i}" title="${esc(STRINGS.remove)}"
        aria-label="${esc(STRINGS.remove)}" ${last === 0 ? "disabled" : ""}>✕</button></div>`);
    const full = S.networks.length >= S.max;
    rows.push(full ? `<div class="row muted">${esc(STRINGS.listFull.replace("{max}", S.max))}</div>`
      : `<button class="row" data-add="1"><span class="name">＋ ${esc(STRINGS.addNetwork)}</span>›</button>`);
    const error = S.listError ? `<p class="error">${esc(S.listError)}</p>` : "";
    return `${hero}<h3>${esc(STRINGS.savedNetworks)}</h3><div class="rows">${rows.join("")}</div>
      ${error}<p class="hint muted">${esc(STRINGS.orderHint)}</p>`;
  },

  status(s) {
    const connected = s.connection === "Connected";
    return `<div class="tiles">
      ${tile(STRINGS.wifi, s.ssid || "–")}
      ${tile(STRINGS.signal, connected ? `${bars(s.rssi)} ${s.rssi}` : "–", true)}
      ${tile(STRINGS.ipAddress, connected ? s.ip : "–")}
      ${tile(STRINGS.uptime, formatUptime(s.uptimeMs))}</div>
      <h3>${esc(STRINGS.recentEvents)}</h3><div class="rows">${historyRows()}</div>
      ${s.time && s.time.enabled ? `<h3>${esc(STRINGS.time)}</h3>${timeRows(s.time)}` : ""}
      <h3>${esc(STRINGS.details)}</h3><div class="rows">${statusList(s)}</div>`;
  },

  settings() {
    if (!S.settingsLoaded) return `<p class="muted">${esc(STRINGS.loading)}</p>`;
    const badge = S.settings.complete ? ""
      : ` <span class="badge warn">${esc(STRINGS.needsAttention)}</span>`;
    const body = S.settings.sections.map((section) =>
      `<h3>${esc(sectionTitle(section))}</h3>${settingFields(S, section)}`).join("");
    const error = S.settingsError ? `<p class="error">${esc(S.settingsError)}</p>` : "";
    const note = S.settingsNote ? `<p class="muted">${esc(S.settingsNote)}</p>` : "";
    return `<p class="lead">${esc(STRINGS.settingsLead)}${badge}</p>${body}${error}${note}
      <div class="foot"><button class="btn block" data-save-settings="1">${esc(STRINGS.save)}</button></div>`;
  },

  device(s) {
    const rows = [
      [STRINGS.hostname, s.hostname],
      [STRINGS.mac, s.mac],
      [STRINGS.chip, s.chip],
      [STRINGS.firmware, s.firmware || "–"],
      [STRINGS.library, `EspManager ${s.library}`],
    ];
    const offered = Object.entries(ACTIONS).filter(([key]) =>
      key !== "settingsReset" || (S.settings && S.settings.hasSettings));
    const actions = offered.map(([key, [, name, erases]]) =>
      `<button class="row${erases ? " bad" : ""}" data-action="${key}"><span class="name">${esc(STRINGS.actions[name].title)}</span>›</button>`).join("");
    return `<h3>${esc(STRINGS.device)}</h3><div class="rows"><dl class="kv">${rows.map(([label, value]) =>
      `<dt>${esc(label)}</dt><dd>${esc(value)}</dd>`).join("")}</dl></div>
      <h3>${esc(STRINGS.maintenance)}</h3><div class="rows">${actions}</div>`;
  },
};

// The Settings tab is there only when the Developer marked a Setting for this site.
function visibleTabs() {
  const settings = S.settings && S.settings.sections.length ? [["settings", "☰", STRINGS.settings]] : [];
  return [["wifi", "📶", STRINGS.wifi], ["status", "ⓘ", STRINGS.status], ...settings,
    ["device", "⚙", STRINGS.device]];
}

// The Wi-Fi tab's sheets: adding a network, and confirming a removal.
function networkSheet() {
  const sheet = S.netSheet;
  const error = sheet.error ? `<p class="error">${esc(sheet.error)}</p>` : "";
  const disabled = sheet.busy ? "disabled" : "";
  let body;
  if (sheet.kind === "add") {
    body = `<h2>${esc(STRINGS.addNetwork)}</h2><p class="lead">${esc(STRINGS.addNetworkLead)}</p>
      <div class="field"><label for="ssid">${esc(STRINGS.networkName)}</label>
        <input id="ssid" autocomplete="off" autocapitalize="none" maxlength="32" value="${esc(sheet.ssid)}"></div>
      <div class="field"><label for="password">${esc(STRINGS.password)}</label>
        <input id="password" type="${sheet.show ? "text" : "password"}" autocomplete="off"
          autocapitalize="none" maxlength="64" value="${esc(sheet.password)}"></div>
      <label class="check"><input type="checkbox" data-sheet="toggle" ${sheet.show ? "checked" : ""}>
        ${esc(STRINGS.showPassword)}</label>${error}
      <div class="foot"><button class="btn block" data-sheet="save" ${disabled}>${esc(STRINGS.save)}</button>
        <button class="btn secondary block" data-sheet="cancel">${esc(STRINGS.cancel)}</button></div>`;
  } else {
    body = `<h2>${esc(STRINGS.removeTitle.replace("{ssid}", sheet.ssid))}</h2>
      <p class="lead">${esc(STRINGS.removeLead)}</p>${error}
      <div class="foot"><button class="btn block danger" data-sheet="remove" ${disabled}>${esc(STRINGS.remove)}</button>
        <button class="btn secondary block" data-sheet="cancel">${esc(STRINGS.cancel)}</button></div>`;
  }
  return `<div class="scrim" data-sheet="cancel">
    <div class="sheet" role="dialog" aria-modal="true"><div class="grab"></div>${body}</div></div>`;
}

// The bottom sheet for the Device tab's action: confirm first, then what happened.
function sheet() {
  if (S.netSheet) return networkSheet();
  if (!S.action) return "";
  const [, name, erases] = ACTIONS[S.action];
  const text = STRINGS.actions[name];
  let body;
  if (S.actionState === "confirm") {
    body = `<h2>${esc(text.title)}</h2><p class="lead">${esc(text.confirm)}</p>
      <div class="foot"><button class="btn block${erases ? " danger" : ""}" data-sheet="run">${esc(text.button)}</button>
        <button class="btn secondary block" data-sheet="cancel">${esc(STRINGS.cancel)}</button></div>`;
  } else if (S.actionState === "busy") {
    body = `<div class="center"><div class="spinner"></div><h2>${esc(text.title)}</h2></div>`;
  } else if (S.actionState === "done") {
    // Only a Settings Reset leaves the device as it was, with the page still of use.
    const close = S.action === "settingsReset"
      ? `<div class="foot"><button class="btn secondary block" data-sheet="cancel">${esc(STRINGS.close)}</button></div>` : "";
    body = `<div class="center"><div class="bigicon ok">✓</div><h2>${esc(text.title)}</h2>
      <p class="lead">${esc(text.done)}</p></div>${close}`;
  } else {
    body = `<div class="center"><div class="bigicon bad">✕</div><h2>${esc(text.title)}</h2>
      <p class="lead">${esc(STRINGS.errors.actionFailed)}</p></div>
      <div class="foot"><button class="btn secondary block" data-sheet="cancel">${esc(STRINGS.close)}</button></div>`;
  }
  return `<div class="scrim" data-sheet="${S.actionState === "confirm" || S.actionState === "failed" ||
    (S.actionState === "done" && S.action === "settingsReset") ? "cancel" : ""}">
    <div class="sheet" role="dialog" aria-modal="true"><div class="grab"></div>${body}</div></div>`;
}

function render() {
  const s = S.status;
  const name = s ? s.hostname : "";
  const error = S.error ? `<div class="note">${esc(STRINGS.errors.unreachableManagement)}</div>` : "";
  const content = s ? tabs[S.tab](s) : `<p class="muted">${esc(STRINGS.loading)}</p>`;
  const nav = visibleTabs().map(([key, icon, label]) => {
    const dot = key === "settings" && !S.settings.complete
      ? `<i class="dot" title="${esc(STRINGS.needsAttention)}" aria-label="${esc(STRINGS.needsAttention)}"></i>` : "";
    return `<button class="${S.tab === key ? "on" : ""}" data-tab="${key}"><b>${icon}${dot}</b>${esc(label)}</button>`;
  }).join("");
  app.innerHTML = `<header class="top">${brandHtml(STRINGS.manageTitle)}
    ${name ? `<span class="pill">${esc(name)}</span>` : ""}</header>
    <div class="content">${error}${content}</div><nav class="tabs">${nav}</nav>${sheet()}`;
  // With a title, the hostname tells devices apart; without one, the heading says what this is.
  document.title = UI.title ? tabTitle(name) : tabTitle(name, STRINGS.manageTitle);
}

app.addEventListener("click", (event) => {
  const control = event.target.closest("[data-sheet]");
  // A tap on the dimmed page outside the sheet counts only for the scrim itself.
  if (control && (control.classList.contains("scrim") ? event.target === control : true)) {
    const what = control.dataset.sheet;
    if (what === "run") runAction(S.action);
    else if (what === "save") saveNetwork();
    else if (what === "remove") removeNetwork();
    else if (what === "toggle") {
      readSheet();
      S.netSheet.show = !S.netSheet.show;
      render();
    } else if (what === "cancel") {
      S.action = null;
      S.netSheet = null;
      render();
    }
    return;
  }
  if (event.target.closest("[data-act=toggle]")) {
    readSettingFields(S, app);
    S.showPassword = !S.showPassword;
    render();
    return;
  }
  if (event.target.closest("[data-save-settings]")) {
    saveSettings();
    return;
  }
  if (event.target.closest("[data-sync]")) {
    syncNow();
    return;
  }
  const move = event.target.closest("[data-move]");
  if (move) {
    moveNetwork(Number(move.dataset.move), Number(move.dataset.to));
    return;
  }
  const remove = event.target.closest("[data-remove]");
  if (remove) {
    S.netSheet = { kind: "remove", ssid: S.networks[Number(remove.dataset.remove)].ssid, error: "", busy: false };
    S.listError = "";
    render();
    return;
  }
  if (event.target.closest("[data-add]")) {
    S.netSheet = { kind: "add", ssid: "", password: "", show: false, error: "", busy: false };
    S.listError = "";
    render();
    document.getElementById("ssid").focus();
    return;
  }
  const action = event.target.closest("[data-action]");
  if (action) {
    S.action = action.dataset.action;
    S.actionState = "confirm";
    render();
    return;
  }
  const tab = event.target.closest("[data-tab]");
  if (!tab) return;
  // Another tap on the Settings tab mustn't throw away what the Owner entered.
  if (tab.dataset.tab === S.tab && S.tab === "settings") return;
  S.tab = tab.dataset.tab;
  if (S.tab === "wifi") loadNetworks();
  if (S.tab === "status") loadHistory();
  if (S.tab === "settings") {
    // Fresh values, and anything not saved earlier is dropped.
    S.settingsLoaded = false;
    S.settingErrors = {};
    S.settingsError = "";
    S.settingsNote = "";
    loadSettings();
  }
  render();
  window.scrollTo(0, 0);
});

app.addEventListener("keydown", (event) => {
  if (event.key === "Enter" && S.tab === "settings" && !S.netSheet && !S.action &&
      event.target.matches("input:not([type=checkbox])")) {
    saveSettings();
    return;
  }
  if (event.key === "Enter" && S.netSheet && S.netSheet.kind === "add" &&
      event.target.matches("input:not([type=checkbox])")) {
    saveNetwork();
  }
});

render();
loadUi(render);
loadStatus();
loadNetworks();
loadSettings();
