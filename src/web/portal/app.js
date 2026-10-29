"use strict";

const app = document.getElementById("app");

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
  reason: "",
  ip: "",
  status: null,
  // The screen the status screen goes back to.
  before: "networks",
};

// How long the page keeps asking for the result while the phone may be off the access point.
const RESULT_TIMEOUT_MS = 90000;

// --- Talking to the device ---

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

// Sends the network, then follows the test until the device has a result. Testing can move the
// access point to the router's channel, so the phone may drop off for a while; requests that
// fail meanwhile are retried until RESULT_TIMEOUT_MS.
async function submit(saveAnyway) {
  const fields = { ssid: S.ssid, password: S.secured || S.hidden ? S.password : "" };
  if (saveAnyway) fields.saveAnyway = "1";
  let answer;
  try {
    answer = await postForm("/api/connect", fields);
  } catch (error) {
    S.error = STRINGS.errors.unreachable;
    go("password");
    return;
  }
  if (!answer.ok) {
    S.error = STRINGS.errors[answer.data.error] || STRINGS.errors.other;
    go("password");
    return;
  }
  showResult(answer.data);
  const deadline = Date.now() + RESULT_TIMEOUT_MS;
  while (S.screen === "connecting") {
    await new Promise((resolve) => setTimeout(resolve, 1500));
    if (Date.now() > deadline) {
      go("lost");
      return;
    }
    try {
      showResult(await getJson("/api/result"));
    } catch (error) {
      // Still off the access point; ask again.
    }
  }
}

function showResult(result) {
  if (result.state === "testing") {
    if (S.screen !== "connecting") go("connecting");
  } else if (result.state === "connected") {
    S.ip = result.ip;
    go("done");
  } else if (result.state === "saved") {
    go("saved");
  } else if (result.state === "failed") {
    S.reason = result.reason;
    go("failed");
  }
}

// --- Screens ---

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

  connecting() {
    return [3, `<div class="center"><div class="spinner"></div>
      <h2>${esc(STRINGS.connecting)}<br>${esc(S.ssid)}</h2>
      <p class="lead">${esc(STRINGS.connectingLead)}</p></div>
      <div class="note">${esc(STRINGS.channelWarning)}</div>`];
  },

  failed() {
    return [3, `<div class="center"><div class="bigicon bad">✕</div>
      <h2>${esc(STRINGS.failedTitle)}</h2>
      <p class="lead">${esc(STRINGS.reasons[S.reason] || STRINGS.reasons.other)}</p></div>
      <div class="foot"><button class="btn block" data-act="retry">${esc(STRINGS.tryAgain)}</button>
        <button class="btn secondary block" data-act="saveAnyway">${esc(STRINGS.saveAnyway)}</button>
        <p class="muted center-text">${esc(STRINGS.saveAnywayHint)}</p></div>`];
  },

  done() {
    return [4, `<div class="center"><div class="bigicon ok">✓</div>
      <h2>${esc(STRINGS.doneTitle)}</h2><p class="lead">${esc(STRINGS.doneLead)}</p></div>
      <div class="panel"><dl class="kv"><dt>${esc(STRINGS.wifi)}</dt><dd>${esc(S.ssid)}</dd>
        <dt>${esc(STRINGS.ipAddress)}</dt><dd>${esc(S.ip)}</dd></dl></div>`];
  },

  saved() {
    return [4, `<div class="center"><div class="bigicon ok">✓</div>
      <h2>${esc(STRINGS.savedTitle)}</h2><p class="lead">${esc(STRINGS.savedLead)}</p></div>`];
  },

  status() {
    const body = S.status ? `<div class="panel">${statusList(S.status)}</div>`
      : `<p class="muted">${esc(STRINGS.loading)}</p>`;
    return [0, `<h2>${esc(STRINGS.status)}</h2>${body}
      <div class="foot"><button class="btn link" data-act="closeStatus">← ${esc(STRINGS.back)}</button></div>`];
  },

  lost() {
    return [3, `<div class="center"><div class="bigicon">?</div>
      <h2>${esc(STRINGS.lostTitle)}</h2><p class="lead">${esc(STRINGS.lostLead)}</p></div>
      <div class="foot"><button class="btn block" data-act="reload">${esc(STRINGS.reload)}</button></div>`];
  },
};

// Shows the current screen; its step (1 to 4) lights up the step indicator, 0 hides it.
function render() {
  const [step, body] = screens[S.screen]();
  const steps = step ? `<div class="steps">${[1, 2, 3, 4].map((i) =>
    `<i class="${i <= step ? "on" : ""}"></i>`).join("")}</div>` : "";
  // The status is one tap away, except while a test runs.
  const link = S.screen === "status" || S.screen === "connecting" ? ""
    : `<button class="btn link" data-act="openStatus">${esc(STRINGS.status)}</button>`;
  app.innerHTML = `<header><h1>${esc(STRINGS.title)}</h1>${link}</header>${steps}${body}`;
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
    S.error = check();
    if (S.error) {
      go("password");
      return;
    }
    submit(false);
  },
  retry() {
    S.error = "";
    go("password");
  },
  saveAnyway() {
    submit(true);
  },
  reload() {
    location.reload();
  },
  openStatus() {
    readFields();
    S.before = S.screen;
    go("status");
    loadStatus();
  },
  closeStatus() {
    go(S.before);
  },
};

// Keeps the status screen up to date while it's shown.
async function loadStatus() {
  while (S.screen === "status") {
    try {
      S.status = await getJson("/api/status");
      if (S.screen === "status") render();
    } catch (error) {
      // Ask again.
    }
    await new Promise((resolve) => setTimeout(resolve, 5000));
  }
}

// The checks the device makes too, so most mistakes show without a round trip.
function check() {
  if (!S.ssid) return STRINGS.errors["ssid-empty"];
  if (new TextEncoder().encode(S.ssid).length > 32) return STRINGS.errors["ssid-too-long"];
  if (!S.secured && !S.hidden) return "";
  const password = S.password;
  if (password === "") return S.hidden ? "" : STRINGS.errors["password-too-short"];
  if (password.length < 8) return STRINGS.errors["password-too-short"];
  if (password.length > 64) return STRINGS.errors["password-too-long"];
  if (!/^[\x20-\x7e]*$/.test(password)) return STRINGS.errors["password-invalid"];
  if (password.length === 64 && !/^[0-9a-fA-F]+$/.test(password)) return STRINGS.errors["password-invalid"];
  return "";
}

app.addEventListener("click", (event) => {
  const pick = event.target.closest("[data-pick]");
  if (pick) {
    const network = S.networks[Number(pick.dataset.pick)];
    Object.assign(S, { ssid: network.ssid, secured: network.secured, hidden: false, password: "", error: "" });
    // An open network has nothing to enter.
    if (network.secured) go("password");
    else submit(false);
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
