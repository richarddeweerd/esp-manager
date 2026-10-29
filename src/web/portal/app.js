"use strict";

// The page text, filled in from src/web/strings.en.json by scripts/build-web.py.
const STRINGS = /*STRINGS*/ {};

const app = document.getElementById("app");
const esc = (text) => String(text).replace(/[&<>"']/g,
  (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" }[c]));

// Shows one screen. `step` (1 to 4) lights up the step indicator; 0 hides it.
function show(step, body) {
  const steps = step ? `<div class="steps">${[1, 2, 3, 4].map((i) =>
    `<i class="${i <= step ? "on" : ""}"></i>`).join("")}</div>` : "";
  app.innerHTML = `<header><h1>${esc(STRINGS.title)}</h1></header>${steps}${body}`;
}

document.title = STRINGS.title;
show(0, `<h2>${esc(STRINGS.welcomeTitle)}</h2><p class="lead">${esc(STRINGS.welcomeLead)}</p>`);
