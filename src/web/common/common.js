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
}

// The form fields for the Settings in these Sections.
function settingFormFields(form, list) {
  const fields = {};
  for (const section of list) {
    for (const setting of section.settings) {
      if (form.clears[setting.key]) fields[`clear.${setting.key}`] = "1";
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

// One Setting's field, with its error if it has one.
function settingField(form, setting) {
  const key = esc(setting.key);
  const id = `setting-${key}`;
  const value = form.values[setting.key];
  const label = esc(setting.label);
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
    control = `<input id="${id}" data-setting="${key}" autocomplete="off" autocapitalize="none"
      maxlength="${setting.maxLength}" value="${esc(value)}">`;
  }
  // `after` stays out of .field, whose styling is for the text boxes.
  return `<div class="field"><label for="${id}">${label}</label>${control}</div>${after}${error}`;
}

// The fields of a Section, with "Show password" if it has a password.
function settingFields(form, section) {
  const toggle = section.settings.some((setting) => setting.type === "password")
    ? `<label class="check"><input type="checkbox" data-act="toggle" ${form.showPassword ? "checked" : ""}>
        ${esc(STRINGS.showPassword)}</label>` : "";
  return section.settings.map((setting) => settingField(form, setting)).join("") + toggle;
}

const sectionTitle = (section) => section.title || STRINGS.general;
