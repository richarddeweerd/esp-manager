"use strict";

// The page text, filled in from src/web/strings.en.json by scripts/build-web.py.
const STRINGS = /*STRINGS*/ {};

const app = document.getElementById("app");
const esc = (text) => String(text).replace(/[&<>"']/g,
  (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" }[c]));

// Where the wizard stands. The device keeps nothing for the page; a reload starts over.
const S = {
  screen: "networks",
  networks: [],
  scanning: true,
  ssid: "",
  secured: true,
  hidden: false,
  password: "",
  showPassword: false,
  error: "",
};

// --- Talking to the device ---

async function getJson(path) {
  const response = await fetch(path, { cache: "no-store" });
  if (!response.ok) throw new Error(`${path}: ${response.status}`);
  return response.json();
}

// Asks for the network list until the device's scan has finished.
let scanTimer = 0;
async function loadNetworks(refresh) {
  clearTimeout(scanTimer);
  S.scanning = true;
  if (S.screen === "networks") render();
  try {
    const data = await getJson(refresh ? "/api/scan?refresh=1" : "/api/scan");
    S.networks = data.networks;
    S.scanning = data.scanning;
  } catch (error) {
    // The phone may drop off the access point for a moment while the device scans.
  }
  if (S.screen === "networks") render();
  if (S.scanning) scanTimer = setTimeout(() => loadNetworks(false), 1500);
}

// --- Screens ---

function bars(rssi) {
  const level = rssi >= -55 ? 4 : rssi >= -65 ? 3 : rssi >= -75 ? 2 : 1;
  return `<span class="bars" title="${rssi} dBm">${[1, 2, 3, 4].map((i) =>
    `<i class="${i <= level ? "on" : ""}"></i>`).join("")}</span>`;
}

function lock(secured) {
  return secured ? `<span class="lock" title="${esc(STRINGS.secured)}">🔒</span>`
    : `<span class="lock muted">${esc(STRINGS.open)}</span>`;
}

const screens = {
  networks() {
    const items = S.networks.map((network, i) => `<button class="net" data-pick="${i}">
      <span class="name">${esc(network.ssid)}</span>${lock(network.secured)} ${bars(network.rssi)}
      </button>`).join("");
    const status = S.scanning ? `<p class="muted">${esc(STRINGS.scanning)}</p>`
      : S.networks.length ? "" : `<p class="muted">${esc(STRINGS.noNetworks)}</p>`;
    return [1, `<h2>${esc(STRINGS.chooseNetwork)}</h2>
      <p class="lead">${esc(STRINGS.chooseLead)}</p>
      <div class="list">${items}
        <button class="net" data-act="other"><span class="name muted">${esc(STRINGS.otherNetwork)}</span></button>
      </div>${status}
      <div class="foot"><button class="btn link" data-act="rescan" ${S.scanning ? "disabled" : ""}>
        ${esc(STRINGS.scanAgain)}</button></div>`];
  },

  password() {
    const lead = S.hidden ? STRINGS.hiddenLead : S.secured ? STRINGS.passwordLead : STRINGS.openLead;
    const ssidField = S.hidden ? `<div class="field"><label for="ssid">${esc(STRINGS.networkName)}</label>
      <input id="ssid" autocomplete="off" autocapitalize="none" maxlength="32" value="${esc(S.ssid)}"></div>` : "";
    const passwordField = S.secured || S.hidden ? `<div class="field">
      <label for="password">${esc(STRINGS.password)}</label>
      <input id="password" type="${S.showPassword ? "text" : "password"}" autocomplete="off"
        autocapitalize="none" maxlength="64" value="${esc(S.password)}"></div>
      <label class="check"><input type="checkbox" data-act="toggle" ${S.showPassword ? "checked" : ""}>
        ${esc(STRINGS.showPassword)}</label>` : "";
    const error = S.error ? `<p class="error">${esc(S.error)}</p>` : "";
    return [2, `<h2>${esc(S.hidden ? STRINGS.otherNetwork : S.ssid)}</h2>
      <p class="lead">${esc(lead)}</p>${ssidField}${passwordField}${error}
      <div class="foot"><button class="btn block" data-act="connect">${esc(STRINGS.connect)}</button>
        <button class="btn link" data-act="back">← ${esc(STRINGS.back)}</button></div>`];
  },
};

// Shows the current screen; its step (1 to 4) lights up the step indicator.
function render() {
  const [step, body] = screens[S.screen]();
  const steps = `<div class="steps">${[1, 2, 3, 4].map((i) =>
    `<i class="${i <= step ? "on" : ""}"></i>`).join("")}</div>`;
  app.innerHTML = `<header><h1>${esc(STRINGS.title)}</h1></header>${steps}${body}`;
}

function go(screen) {
  S.screen = screen;
  render();
  const first = app.querySelector("input:not([type=checkbox])");
  if (first) first.focus();
}

// Keeps what the Owner typed when the screen is drawn again.
function readFields() {
  const ssid = document.getElementById("ssid");
  const password = document.getElementById("password");
  if (ssid) S.ssid = ssid.value;
  if (password) S.password = password.value;
}

// --- Actions ---

const actions = {
  other() {
    Object.assign(S, { ssid: "", secured: true, hidden: true, password: "", error: "" });
    go("password");
  },
  rescan() {
    loadNetworks(true);
  },
  toggle() {
    readFields();
    S.showPassword = !S.showPassword;
    render();
  },
  back() {
    S.error = "";
    go("networks");
  },
  connect() {
    readFields();
  },
};

app.addEventListener("click", (event) => {
  const pick = event.target.closest("[data-pick]");
  if (pick) {
    const network = S.networks[Number(pick.dataset.pick)];
    Object.assign(S, { ssid: network.ssid, secured: network.secured, hidden: false, password: "", error: "" });
    go("password");
    return;
  }
  const action = event.target.closest("[data-act]");
  if (action && actions[action.dataset.act]) actions[action.dataset.act]();
});

app.addEventListener("keydown", (event) => {
  if (event.key === "Enter" && event.target.matches("input:not([type=checkbox])")) actions.connect();
});

document.title = STRINGS.title;
render();
loadNetworks(false);
