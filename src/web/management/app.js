"use strict";

const app = document.getElementById("app");

// What the page shows. Everything comes from the device; a reload starts over.
const S = {
  tab: "wifi",
  status: null,
  networks: null,
  error: false,
  // The Device tab's action waiting for confirmation, and where it stands: "confirm", "busy",
  // "done" or "failed".
  action: null,
  actionState: "",
};

// What the Device tab can do: [request, strings key, whether it erases something].
const ACTIONS = {
  restart: ["/api/restart", "restart", false],
  networkReset: ["/api/network-reset", "networkReset", true],
  factoryReset: ["/api/factory-reset", "factoryReset", true],
};

// --- Talking to the device ---

// Keeps the status up to date: every tab shows part of it.
async function loadStatus() {
  for (;;) {
    try {
      S.status = await getJson("/api/status");
      S.error = false;
    } catch (error) {
      S.error = true;
    }
    render();
    await new Promise((resolve) => setTimeout(resolve, 5000));
  }
}

// The header tells the device the request comes from this page, not from another site.
async function runAction(key) {
  S.actionState = "busy";
  render();
  try {
    const response = await fetch(ACTIONS[key][0], {
      method: "POST", headers: { "X-Requested-With": "EspManager" },
    });
    S.actionState = response.ok ? "done" : "failed";
  } catch (error) {
    S.actionState = "failed";
  }
  render();
}

async function loadNetworks() {
  try {
    S.networks = (await getJson("/api/networks")).networks;
  } catch (error) {
    S.error = true;
  }
  render();
}

// --- Tabs ---

function tile(label, value, html) {
  return `<div class="tile"><div class="k">${esc(label)}</div>
    <div class="v">${html ? value : esc(value)}</div></div>`;
}

const tabs = {
  wifi(s) {
    const connected = s.connection === "Connected";
    const hero = `<div class="hero ${connected ? "" : "idle"}">
      <div class="sub">${esc(connected ? STRINGS.connectedTo : STRINGS.states[s.connection] || s.connection)}</div>
      <div class="big">${esc(s.ssid || "–")}</div>
      <div class="sub">${connected ? `${esc(s.ip)} · ${bars(s.rssi)} ${s.rssi} dBm` : ""}</div></div>`;
    const list = S.networks === null ? `<div class="row muted">${esc(STRINGS.loading)}</div>`
      : S.networks.length === 0 ? `<div class="row muted">${esc(STRINGS.noSavedNetworks)}</div>`
        : S.networks.map((network, i) => `<div class="row"><span class="muted">${i + 1}</span>
          <span class="name">${esc(network.ssid)}</span></div>`).join("");
    return `${hero}<h3>${esc(STRINGS.savedNetworks)}</h3><div class="rows">${list}</div>`;
  },

  status(s) {
    const connected = s.connection === "Connected";
    return `<div class="tiles">
      ${tile(STRINGS.wifi, s.ssid || "–")}
      ${tile(STRINGS.signal, connected ? `${bars(s.rssi)} ${s.rssi}` : "–", true)}
      ${tile(STRINGS.ipAddress, connected ? s.ip : "–")}
      ${tile(STRINGS.uptime, formatUptime(s.uptimeMs))}</div>
      <h3>${esc(STRINGS.details)}</h3><div class="rows">${statusList(s)}</div>`;
  },

  device(s) {
    const rows = [
      [STRINGS.hostname, s.hostname],
      [STRINGS.mac, s.mac],
      [STRINGS.chip, s.chip],
      [STRINGS.firmware, s.firmware || "–"],
      [STRINGS.library, `EspManager ${s.library}`],
    ];
    const actions = Object.entries(ACTIONS).map(([key, [, name, erases]]) =>
      `<button class="row${erases ? " bad" : ""}" data-action="${key}"><span class="name">${esc(STRINGS.actions[name].title)}</span>›</button>`).join("");
    return `<h3>${esc(STRINGS.device)}</h3><div class="rows"><dl class="kv">${rows.map(([label, value]) =>
      `<dt>${esc(label)}</dt><dd>${esc(value)}</dd>`).join("")}</dl></div>
      <h3>${esc(STRINGS.maintenance)}</h3><div class="rows">${actions}</div>`;
  },
};

const TABS = [["wifi", "📶", STRINGS.wifi], ["status", "ⓘ", STRINGS.status], ["device", "⚙", STRINGS.device]];

// The bottom sheet for the Device tab's action: confirm first, then what happened.
function sheet() {
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
    body = `<div class="center"><div class="bigicon ok">✓</div><h2>${esc(text.title)}</h2>
      <p class="lead">${esc(text.done)}</p></div>`;
  } else {
    body = `<div class="center"><div class="bigicon bad">✕</div><h2>${esc(text.title)}</h2>
      <p class="lead">${esc(STRINGS.errors.actionFailed)}</p></div>
      <div class="foot"><button class="btn secondary block" data-sheet="cancel">${esc(STRINGS.close)}</button></div>`;
  }
  return `<div class="scrim" data-sheet="${S.actionState === "confirm" || S.actionState === "failed" ? "cancel" : ""}">
    <div class="sheet" role="dialog" aria-modal="true"><div class="grab"></div>${body}</div></div>`;
}

function render() {
  const s = S.status;
  const name = s ? s.hostname : "";
  const error = S.error ? `<div class="note">${esc(STRINGS.errors.unreachableManagement)}</div>` : "";
  const content = s ? tabs[S.tab](s) : `<p class="muted">${esc(STRINGS.loading)}</p>`;
  const nav = TABS.map(([key, icon, label]) =>
    `<button class="${S.tab === key ? "on" : ""}" data-tab="${key}"><b>${icon}</b>${esc(label)}</button>`).join("");
  app.innerHTML = `<header class="top"><h1>${esc(STRINGS.manageTitle)}</h1>
    ${name ? `<span class="pill">${esc(name)}</span>` : ""}</header>
    <div class="content">${error}${content}</div><nav class="tabs">${nav}</nav>${sheet()}`;
  document.title = name ? `${name} · ${STRINGS.manageTitle}` : STRINGS.manageTitle;
}

app.addEventListener("click", (event) => {
  const control = event.target.closest("[data-sheet]");
  // A tap on the dimmed page outside the sheet counts only for the scrim itself.
  if (control && (control.classList.contains("scrim") ? event.target === control : true)) {
    if (control.dataset.sheet === "run") runAction(S.action);
    else if (control.dataset.sheet === "cancel") {
      S.action = null;
      render();
    }
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
  S.tab = tab.dataset.tab;
  if (S.tab === "wifi") loadNetworks();
  render();
  window.scrollTo(0, 0);
});

render();
loadStatus();
loadNetworks();
