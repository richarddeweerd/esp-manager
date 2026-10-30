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
  // From GET /api/setup: "new" or "keep" when the Owner also chooses a Management Interface
  // password, "off" when there's no Management Interface.
  management: "off",
  user: "",
  managementPassword: "",
  managementRepeat: "",
  error: "",
  reason: "",
  ip: "",
  status: null,
  // The screen the status screen goes back to.
  before: "networks",
  // The Saved Networks (null until loaded), how many fit, the one waiting for "Remove" to be
  // confirmed, and why the last change was refused. With Saved Networks the wizard opens on
  // their list, until the Owner leaves it.
  saved: null,
  max: 0,
  removing: "",
  listError: "",
  leftSaved: false,
};

// How long the page keeps asking for the result while the phone may be off the access point.
const RESULT_TIMEOUT_MS = 90000;

// --- Talking to the device ---

async function loadSetup() {
  try {
    const setup = await getJson("/api/setup");
    S.management = setup.management;
    S.user = setup.user;
    render();
  } catch (error) {
    // Ask again; without the answer the wizard doesn't know its last step.
    setTimeout(loadSetup, 1500);
  }
}

async function loadSaved() {
  try {
    showSaved(await getJson("/api/networks"));
  } catch (error) {
    setTimeout(loadSaved, 1500);
    return;
  }
  if (S.saved.length && S.screen === "networks" && !S.leftSaved) go("list");
}

function showSaved(data) {
  S.saved = data.networks;
  S.max = data.max;
}

// Sends a change to the list and shows the new one, or why it was refused.
async function changeSaved(path, fields) {
  S.removing = "";
  S.listError = "";
  try {
    const answer = await postForm(path, fields);
    if (answer.ok) showSaved(answer.data);
    else {
      S.listError = STRINGS.errors[answer.data.error] || STRINGS.errors.other;
      if (answer.data.error === "not-found") loadSaved();
    }
  } catch (error) {
    S.listError = STRINGS.errors.unreachable;
  }
  if (S.saved.length === 0) {
    S.leftSaved = true;
    go("networks");
  } else render();
}

// 1 when the wizard has the Management Interface step, 0 otherwise.
const extra = () => (S.management === "off" ? 0 : 1);

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
  if (extra()) fields.managementPassword = S.managementPassword;
  if (saveAnyway) fields.saveAnyway = "1";
  // Where the Owner fixes what's wrong.
  const form = extra() ? "management" : "password";
  let answer;
  try {
    answer = await postForm("/api/connect", fields);
  } catch (error) {
    S.error = STRINGS.errors.unreachable;
    go(form);
    return;
  }
  if (!answer.ok) {
    const code = answer.data.error || "";
    if (code === "full") {
      S.listError = STRINGS.errors.full;
      go("list");
      return;
    }
    S.error = STRINGS.errors[code] || STRINGS.errors.other;
    go(code === "management-password-invalid" ? "management" : code.startsWith("ssid") ||
      code.startsWith("password") ? "password" : form);
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
  list() {
    const last = S.saved.length - 1;
    const items = S.saved.map((network, i) => {
      const name = `<span class="name">${esc(network.ssid)}</span>`;
      if (S.removing === network.ssid) {
        return `<div class="net saved">${name}
          <button class="btn danger" data-remove="${i}">${esc(STRINGS.remove)}</button>
          <button class="btn link" data-act="keep">${esc(STRINGS.cancel)}</button></div>`;
      }
      return `<div class="net saved"><span class="muted">${i + 1}</span>${name}
        <button class="icon" data-move="${i}" data-to="${i - 1}" title="${esc(STRINGS.moveUp)}"
          aria-label="${esc(STRINGS.moveUp)}" ${i === 0 ? "disabled" : ""}>↑</button>
        <button class="icon" data-move="${i}" data-to="${i + 1}" title="${esc(STRINGS.moveDown)}"
          aria-label="${esc(STRINGS.moveDown)}" ${i === last ? "disabled" : ""}>↓</button>
        <button class="icon bad" data-ask="${i}" title="${esc(STRINGS.remove)}"
          aria-label="${esc(STRINGS.remove)}">✕</button></div>`;
    }).join("");
    const error = S.listError ? `<p class="error">${esc(S.listError)}</p>` : "";
    const add = S.saved.length >= S.max
      ? `<p class="muted">${esc(STRINGS.listFull.replace("{max}", S.max))}</p>`
      : `<button class="btn block" data-act="add">${esc(STRINGS.addNetwork)}</button>`;
    return [0, `<h2>${esc(STRINGS.savedNetworks)}</h2>
      <p class="lead">${esc(STRINGS.savedNetworksLead)}</p>
      <div class="list">${items}</div>${error}<div class="foot">${add}</div>`];
  },

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
        ${esc(STRINGS.scanAgain)}</button>${S.saved && S.saved.length ? `
        <button class="btn link" data-act="toSaved">← ${esc(STRINGS.savedNetworks)}</button>` : ""}</div>`];
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
      <div class="foot"><button class="btn block" data-act="connect">${esc(extra() ? STRINGS.next : STRINGS.connect)}</button>
        <button class="btn link" data-act="back">← ${esc(STRINGS.back)}</button></div>`];
  },

  management() {
    const type = S.showPassword ? "text" : "password";
    const lead = S.management === "keep" ? STRINGS.managementKeepLead : STRINGS.managementLead;
    const error = S.error ? `<p class="error">${esc(S.error)}</p>` : "";
    return [3, `<h2>${esc(STRINGS.managementTitle)}</h2>
      <p class="lead">${esc(lead)}</p>
      <div class="panel"><dl class="kv"><dt>${esc(STRINGS.userName)}</dt><dd>${esc(S.user)}</dd></dl></div>
      <div class="field"><label for="managementPassword">${esc(STRINGS.password)}</label>
        <input id="managementPassword" type="${type}" autocomplete="new-password" autocapitalize="none"
          maxlength="63" value="${esc(S.managementPassword)}"></div>
      <div class="field"><label for="managementRepeat">${esc(STRINGS.repeatPassword)}</label>
        <input id="managementRepeat" type="${type}" autocomplete="new-password" autocapitalize="none"
          maxlength="63" value="${esc(S.managementRepeat)}"></div>
      <label class="check"><input type="checkbox" data-act="toggle" ${S.showPassword ? "checked" : ""}>
        ${esc(STRINGS.showPassword)}</label>${error}
      <div class="foot"><button class="btn block" data-act="finish">${esc(STRINGS.connect)}</button>
        <button class="btn link" data-act="backToNetwork">← ${esc(STRINGS.back)}</button></div>`];
  },

  connecting() {
    return [3 + extra(), `<div class="center"><div class="spinner"></div>
      <h2>${esc(STRINGS.connecting)}<br>${esc(S.ssid)}</h2>
      <p class="lead">${esc(STRINGS.connectingLead)}</p></div>
      <div class="note">${esc(STRINGS.channelWarning)}</div>`];
  },

  failed() {
    return [3 + extra(), `<div class="center"><div class="bigicon bad">✕</div>
      <h2>${esc(STRINGS.failedTitle)}</h2>
      <p class="lead">${esc(STRINGS.reasons[S.reason] || STRINGS.reasons.other)}</p></div>
      <div class="foot"><button class="btn block" data-act="retry">${esc(STRINGS.tryAgain)}</button>
        <button class="btn secondary block" data-act="saveAnyway">${esc(STRINGS.saveAnyway)}</button>
        <p class="muted center-text">${esc(STRINGS.saveAnywayHint)}</p></div>`];
  },

  done() {
    const later = extra() ? `<div class="note">${esc(STRINGS.managementLater
      .replace("{ip}", S.ip).replace("{user}", S.user))}</div>` : "";
    return [4 + extra(), `<div class="center"><div class="bigicon ok">✓</div>
      <h2>${esc(STRINGS.doneTitle)}</h2><p class="lead">${esc(STRINGS.doneLead)}</p></div>
      <div class="panel"><dl class="kv"><dt>${esc(STRINGS.wifi)}</dt><dd>${esc(S.ssid)}</dd>
        <dt>${esc(STRINGS.ipAddress)}</dt><dd>${esc(S.ip)}</dd></dl></div>${later}`];
  },

  saved() {
    return [4 + extra(), `<div class="center"><div class="bigicon ok">✓</div>
      <h2>${esc(STRINGS.savedTitle)}</h2><p class="lead">${esc(STRINGS.savedLead)}</p></div>`];
  },

  status() {
    const body = S.status ? `<div class="panel">${statusList(S.status)}</div>`
      : `<p class="muted">${esc(STRINGS.loading)}</p>`;
    return [0, `<h2>${esc(STRINGS.status)}</h2>${body}
      <div class="foot"><button class="btn link" data-act="closeStatus">← ${esc(STRINGS.back)}</button></div>`];
  },

  lost() {
    return [3 + extra(), `<div class="center"><div class="bigicon">?</div>
      <h2>${esc(STRINGS.lostTitle)}</h2><p class="lead">${esc(STRINGS.lostLead)}</p></div>
      <div class="foot"><button class="btn block" data-act="reload">${esc(STRINGS.reload)}</button></div>`];
  },
};

// Shows the current screen; its step (1 to 4, or 5 with the Management Interface step) lights up
// the step indicator, 0 hides it.
function render() {
  const [step, body] = screens[S.screen]();
  const all = extra() ? [1, 2, 3, 4, 5] : [1, 2, 3, 4];
  const steps = step ? `<div class="steps">${all.map((i) =>
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
  const managementPassword = document.getElementById("managementPassword");
  const managementRepeat = document.getElementById("managementRepeat");
  if (ssid) S.ssid = ssid.value;
  if (password) S.password = password.value;
  if (managementPassword) S.managementPassword = managementPassword.value;
  if (managementRepeat) S.managementRepeat = managementRepeat.value;
}

// The network is complete: on to the Management Interface password, or connect.
function next() {
  S.error = "";
  if (extra()) go("management");
  else submit(false);
}

// --- Actions ---

const actions = {
  add() {
    S.leftSaved = true;
    S.listError = "";
    go("networks");
  },
  toSaved() {
    go("list");
  },
  keep() {
    S.removing = "";
    render();
  },
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
    next();
  },
  finish() {
    readFields();
    S.error = checkManagement();
    if (S.error) {
      go("management");
      return;
    }
    submit(false);
  },
  backToNetwork() {
    readFields();
    S.error = "";
    go(S.secured || S.hidden ? "password" : "networks");
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

function checkManagement() {
  const password = S.managementPassword;
  if (password !== S.managementRepeat) return STRINGS.errors["management-password-mismatch"];
  if (password === "" && S.management === "keep") return "";
  if (password.length < 8 || password.length > 63 || !/^[\x20-\x7e]*$/.test(password)) {
    return STRINGS.errors["management-password-invalid"];
  }
  return "";
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
  const move = event.target.closest("[data-move]");
  if (move) {
    changeSaved("/api/networks/move", {
      ssid: S.saved[Number(move.dataset.move)].ssid, position: move.dataset.to,
    });
    return;
  }
  const ask = event.target.closest("[data-ask]");
  if (ask) {
    S.removing = S.saved[Number(ask.dataset.ask)].ssid;
    S.listError = "";
    render();
    return;
  }
  const remove = event.target.closest("[data-remove]");
  if (remove) {
    changeSaved("/api/networks/remove", { ssid: S.saved[Number(remove.dataset.remove)].ssid });
    return;
  }
  const pick = event.target.closest("[data-pick]");
  if (pick) {
    const network = S.networks[Number(pick.dataset.pick)];
    Object.assign(S, { ssid: network.ssid, secured: network.secured, hidden: false, password: "", error: "" });
    // An open network has nothing to enter.
    if (network.secured) go("password");
    else next();
    return;
  }
  const action = event.target.closest("[data-act]");
  if (action && actions[action.dataset.act]) actions[action.dataset.act]();
});

app.addEventListener("keydown", (event) => {
  if (event.key === "Enter" && event.target.matches("input:not([type=checkbox])")) {
    if (S.screen === "management") actions.finish();
    else actions.connect();
  }
});

document.title = STRINGS.title;
render();
loadSetup();
loadSaved();
loadNetworks(false);
