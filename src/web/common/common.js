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

// The Developer's Branding, from /api/ui. Until it arrives, the theme follows the phone.
const UI = { title: "", logo: false, theme: "auto" };
const darkQuery = window.matchMedia("(prefers-color-scheme: dark)");

// The page's colours hang off data-theme="light|dark" on <html>, which custom CSS can use too.
function applyTheme() {
  const dark = UI.theme === "dark" || (UI.theme === "auto" && darkQuery.matches);
  document.documentElement.dataset.theme = dark ? "dark" : "light";
}
applyTheme();
darkQuery.addEventListener("change", applyTheme);

// Fetches the Branding, then calls `done` to show the page again, also when it couldn't be read.
async function loadUi(done) {
  try {
    Object.assign(UI, await getJson("/api/ui"));
  } catch (error) {
    // The page keeps its built-in look.
  }
  applyTheme();
  if (UI.logo && !document.querySelector("link[rel=icon]")) {
    const icon = document.createElement("link");
    icon.rel = "icon";
    icon.href = "/logo";
    document.head.appendChild(icon);
  }
  done();
}

// The header's left side: the logo, and the Developer's title over `heading`, or just `heading`.
function brandHtml(heading) {
  const logo = UI.logo ? `<img class="logo" src="/logo" alt="">` : "";
  const names = UI.title
    ? `<h1>${esc(UI.title)}</h1><p class="sub">${esc(heading)}</p>`
    : `<h1>${esc(heading)}</h1>`;
  return `<div class="brand">${logo}<div class="names">${names}</div></div>`;
}

// The browser tab's title: the Developer's title first, then the other parts that aren't empty.
function tabTitle(...parts) {
  return [UI.title, ...parts].filter(Boolean).join(" · ");
}

// The header tells the device the request comes from its own page, not from another site.
async function postForm(path, fields) {
  const response = await fetch(path, {
    method: "POST", body: new URLSearchParams(fields), headers: { "X-Requested-With": "EspManager" },
  });
  let data = {};
  try {
    data = await response.json();
  } catch (error) {
    // A refusal may come as plain text.
  }
  return { ok: response.ok, data };
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
    [STRINGS.dns, connected ? s.dns : "–"],
    [STRINGS.signal, connected ? `${bars(s.rssi)} ${s.rssi} dBm` : "–", true],
    [STRINGS.channel, connected ? s.channel : "–"],
    [STRINGS.hostname, s.hostname],
    [STRINGS.mac, s.mac],
    [STRINGS.uptime, formatUptime(s.uptimeMs)],
    [STRINGS.lastStart, STRINGS.resetReasons[s.resetReason] || s.resetReason],
    [STRINGS.memory, `${Math.round(s.freeHeap / 1024)} ${STRINGS.kbFree}`],
    [STRINGS.chip, s.chip],
    [STRINGS.firmware, s.firmware || "–"],
    [STRINGS.library, `EspManager ${s.library}`],
  ];
  return `<dl class="kv">${rows.map(([label, value, html]) =>
    `<dt>${esc(label)}</dt><dd>${html ? value : esc(value)}</dd>`).join("")}</dl>`;
}

// --- The Settings form, on both sites ---
//
// `form` is the page's state: `settings` from GET /api/settings, what the Owner entered in `values`
// (text by key) and `clears` (passwords to remove), `settingErrors` by key, and `showPassword`.

function showSettings(form, data) {
  form.settings = data;
  form.values = {};
  form.clears = {};
  for (const section of data.sections) {
    for (const setting of section.settings) {
      form.values[setting.key] = setting.type === "password" ? "" : setting.value;
    }
  }
  // No zone picked yet: offer the phone's own.
  if (form.values["time-zone"] === "") {
    const phone = Intl.DateTimeFormat().resolvedOptions().timeZone;
    if (ZONES[phone]) form.values["time-zone"] = phone;
  }
}

// --- The Time Section (Network Time) ---
//
// The device stores the zone's name, the daylight saving choice and the POSIX string the page
// works out from ZONES (zones.js), which the device applies.

// A POSIX TZ string's standard part ("CET-1" of "CET-1CEST,M3.5.0,M10.5.0/3") and whether a
// daylight saving part follows it.
function splitPosix(posix) {
  const match = /^(<[^>]+>|[A-Za-z]+)[+-]?\d{1,2}(:\d{2}){0,2}/.exec(posix || "");
  if (!match) return { standard: posix || "", dst: false };
  return { standard: match[0], dst: match[0].length < posix.length };
}

const zoneHasDst = (zone) => splitPosix(ZONES[zone]).dst;

// The POSIX string for the zone and daylight saving choice in `values`, or the stored one if the
// zone isn't in the list.
function zonePosix(values) {
  const posix = ZONES[values["time-zone"]];
  if (!posix) return values["time-posix"] || "";
  return values["time-dst"] === "0" ? splitPosix(posix).standard : posix;
}

const zoneLabel = (zone) => zone.replace(/_/g, " ").replace(/\//g, " / ");

// The zone picker, grouped by region, and the daylight saving checkbox for zones that have it.
function zoneFields(form, section) {
  const zone = form.values["time-zone"];
  const groups = {};
  for (const name of Object.keys(ZONES)) {
    const region = name.includes("/") ? name.split("/")[0] : "";
    (groups[region] = groups[region] || []).push(name);
  }
  if (zone && !ZONES[zone]) groups[""] = [zone, ...(groups[""] || [])];
  const option = (name) => `<option value="${esc(name)}" ${name === zone ? "selected" : ""}>${esc(zoneLabel(name))}</option>`;
  const options = Object.keys(groups).sort().map((region) => region
    ? `<optgroup label="${esc(region)}">${groups[region].map(option).join("")}</optgroup>`
    : groups[region].map(option).join("")).join("");
  const blank = zone ? "" : `<option value="">${esc(STRINGS.noSelection)}</option>`;
  const errors = ["time-zone", "time-posix"].map((key) => form.settingErrors[key])
    .filter(Boolean).map((problem) => `<p class="error">${esc(problem)}</p>`).join("");
  let html = `<div class="field"><label for="setting-time-zone">${esc(STRINGS.timeZone)}</label>
    <select id="setting-time-zone" data-setting="time-zone" data-zone>${blank}${options}</select></div>${errors}`;
  if (section.settings.some((setting) => setting.key === "time-dst")) {
    html += `<div id="time-dst-row" ${zoneHasDst(zone) ? "" : "hidden"}>
      <label class="check setting"><input type="checkbox" data-setting="time-dst"
        ${form.values["time-dst"] !== "0" ? "checked" : ""}> ${esc(STRINGS.daylightSaving)}</label>
      <p class="muted hint">${esc(STRINGS.daylightSavingHint)}</p></div>`;
  }
  return html;
}

// Shows the daylight saving checkbox only for a zone that has daylight saving.
document.addEventListener("change", (event) => {
  if (!event.target.matches("[data-zone]")) return;
  const row = document.getElementById("time-dst-row");
  if (row) row.hidden = !zoneHasDst(event.target.value);
});

// The form fields for the Settings in these Sections.
function settingFormFields(form, list) {
  const fields = {};
  for (const section of list) {
    for (const setting of section.settings) {
      if (form.clears[setting.key]) fields[`clear.${setting.key}`] = "1";
      else if (setting.key === "time-posix") fields["setting.time-posix"] = zonePosix(form.values);
      else fields[`setting.${setting.key}`] = form.values[setting.key];
    }
  }
  return fields;
}

function showSettingErrors(form, errors) {
  form.settingErrors = {};
  for (const problem of errors) {
    form.settingErrors[problem.key] = problem.message || STRINGS.errors[problem.error] || STRINGS.errors.other;
  }
}

// Keeps what the Owner entered when the form is drawn again.
function readSettingFields(form, root) {
  for (const input of root.querySelectorAll("[data-setting]")) {
    if (input.type === "checkbox") form.values[input.dataset.setting] = input.checked ? "1" : "0";
    else form.values[input.dataset.setting] = input.value;
  }
  for (const input of root.querySelectorAll("[data-clear]")) {
    form.clears[input.dataset.clear] = input.checked;
  }
}

// The Time Section's labels come from the page text, not from the device.
const TIME_LABELS = { "time-server": "ntpServer", "time-fallback": "ntpFallback" };

// One Setting's field, with its error if it has one.
function settingField(form, setting) {
  const key = esc(setting.key);
  const id = `setting-${key}`;
  const value = form.values[setting.key];
  const label = esc(TIME_LABELS[setting.key] ? STRINGS[TIME_LABELS[setting.key]] : setting.label);
  const problem = form.settingErrors[setting.key];
  const error = problem ? `<p class="error">${esc(problem)}</p>` : "";
  if (setting.type === "checkbox") {
    return `<label class="check setting"><input type="checkbox" id="${id}" data-setting="${key}"
      ${value === "1" ? "checked" : ""}> ${label}</label>${error}`;
  }
  let control;
  let after = "";
  if (setting.type === "select") {
    const blank = value === "" || !setting.required
      ? `<option value="">${esc(STRINGS.noSelection)}</option>` : "";
    const options = setting.options.map((option) => `<option value="${esc(option.value)}"
      ${option.value === value ? "selected" : ""}>${esc(option.label)}</option>`).join("");
    control = `<select id="${id}" data-setting="${key}">${blank}${options}</select>`;
  } else if (setting.type === "number") {
    control = `<input id="${id}" data-setting="${key}" type="number" inputmode="numeric"
      min="${setting.min}" max="${setting.max}" value="${esc(value)}">`;
  } else if (setting.type === "password") {
    control = `<input id="${id}" data-setting="${key}" type="${form.showPassword ? "text" : "password"}"
      autocomplete="new-password" autocapitalize="none" maxlength="${setting.maxLength}" value="${esc(value)}">`;
    if (setting.set) {
      after = `<p class="muted hint">${esc(STRINGS.keepPassword)}</p>`;
      if (!setting.required) {
        after += `<label class="check"><input type="checkbox" data-clear="${key}"
          ${form.clears[setting.key] ? "checked" : ""}> ${esc(STRINGS.clearPassword)}</label>`;
      }
    }
  } else {
    const placeholder = setting.key === "time-server" || setting.key === "time-fallback"
      ? ` placeholder="${esc(STRINGS.defaultServer)}"` : "";
    control = `<input id="${id}" data-setting="${key}" autocomplete="off" autocapitalize="none"
      maxlength="${setting.maxLength}" value="${esc(value)}"${placeholder}>`;
  }
  // `after` stays out of .field, whose styling is for the text boxes.
  return `<div class="field"><label for="${id}">${label}</label>${control}</div>${after}${error}`;
}

// The fields of a Section, with "Show password" if it has a password.
function settingFields(form, section) {
  const toggle = section.settings.some((setting) => setting.type === "password")
    ? `<label class="check"><input type="checkbox" data-act="toggle" ${form.showPassword ? "checked" : ""}>
        ${esc(STRINGS.showPassword)}</label>` : "";
  const time = section.settings.some((setting) => setting.key === "time-zone");
  const fields = section.settings.filter((setting) => !(time && /^time-(zone|dst|posix)$/.test(setting.key)))
    .map((setting) => settingField(form, setting)).join("");
  return (time ? zoneFields(form, section) : "") + fields + toggle;
}

const isTimeSection = (section) => section.settings.some((setting) => setting.key === "time-zone");
const sectionTitle = (section) => isTimeSection(section) ? STRINGS.time : section.title || STRINGS.general;
