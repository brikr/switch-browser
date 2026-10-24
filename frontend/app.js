// Browser UI for wkc-loader --serve. Talks to the engine host over a WebSocket (see
// loader/src/host/interactive.h for the protocol).
"use strict";

const $ = (id) => document.getElementById(id);
const canvas = $("screen");
const ctx = canvas.getContext("2d");
const overlay = $("overlay");
const urlInput = $("url");
const logEl = $("log");

let ws = null;
let viewWidth = 1280;
let viewHeight = 720;
let loading = false;

// --- connection ---

function connect() {
  const scheme = location.protocol === "https:" ? "wss" : "ws";
  ws = new WebSocket(`${scheme}://${location.host}/ws`);
  ws.binaryType = "arraybuffer";
  ws.onopen = () => {
    setStatus("connected", "connected");
    overlay.hidden = true;
  };
  ws.onclose = () => {
    setStatus("disconnected — retrying", "");
    overlay.textContent = "Disconnected from the engine. Retrying…";
    overlay.hidden = false;
    setTimeout(connect, 1000);
  };
  ws.onmessage = (e) => (typeof e.data === "string" ? onText(JSON.parse(e.data)) : onFrame(e.data));
}

function send(msg) {
  if (ws && ws.readyState === WebSocket.OPEN) ws.send(JSON.stringify(msg));
}

function onFrame(buf) {
  const h = new Uint32Array(buf, 0, 4);
  const [x, y, w, hgt] = h;
  if (!w || !hgt) return;
  ctx.putImageData(new ImageData(new Uint8ClampedArray(buf, 16, w * hgt * 4), w, hgt), x, y);
}

function onText(m) {
  switch (m.t) {
    case "state":
      viewWidth = m.width;
      viewHeight = m.height;
      canvas.width = viewWidth;
      canvas.height = viewHeight;
      if (m.url) urlInput.value = m.url;
      setTitle(m.title);
      setLoading(m.loading);
      $("engine").textContent = `core_${m.core} · ${m.ua}`;
      $("engine").title = m.ua;
      break;
    case "url":
      if (document.activeElement !== urlInput && m.v) urlInput.value = m.v;
      break;
    case "title":
      setTitle(m.v);
      break;
    case "loading":
      setLoading(m.v);
      break;
    case "console":
      log(m.msg, m.level >= 3 ? "error" : m.level === 2 ? "warn" : "", m.src ? `${m.src}:${m.line}` : "");
      break;
    case "alert":
      log(`alert: ${m.msg}`, "warn");
      break;
    case "eval":
      log(`> ${m.js}`, "result");
      log(m.result, m.threw ? "error" : "");
      break;
  }
}

// --- UI state ---

function setStatus(text, cls) {
  $("status").textContent = text;
  $("status-dot").className = "dot " + cls;
}
function setTitle(t) {
  document.title = t ? `${t} — Switch Browser` : "Switch Browser";
}
function setLoading(v) {
  loading = v;
  setStatus(v ? "loading…" : "connected", v ? "loading" : "connected");
  $("reload").innerHTML = v ? "&#x2715;" : "&#x21bb;";
  $("reload").title = v ? "Stop" : "Reload";
}
function log(text, cls = "", src = "") {
  const div = document.createElement("div");
  div.className = "entry " + cls;
  if (src) {
    const s = document.createElement("span");
    s.className = "src";
    s.textContent = src;
    div.appendChild(s);
  }
  div.appendChild(document.createTextNode(text));
  logEl.appendChild(div);
  while (logEl.childNodes.length > 500) logEl.removeChild(logEl.firstChild);
  logEl.scrollTop = logEl.scrollHeight;
}

// --- toolbar ---

$("nav").addEventListener("submit", (e) => {
  e.preventDefault();
  const url = urlInput.value.trim();
  if (url) send({ t: "nav", url });
  canvas.focus();
});
$("back").onclick = () => send({ t: "back" });
$("fwd").onclick = () => send({ t: "fwd" });
$("reload").onclick = () => send({ t: loading ? "stop" : "reload" });
for (const [id, handheld] of [["mode-console", false], ["mode-handheld", true]]) {
  $(id).onclick = () => {
    send({ t: "mode", handheld });
    $("mode-console").classList.toggle("active", !handheld);
    $("mode-handheld").classList.toggle("active", handheld);
  };
}
$("shot").onclick = () => {
  const a = document.createElement("a");
  a.download = `switch-${new Date().toISOString().replace(/[:.]/g, "-")}.png`;
  a.href = canvas.toDataURL("image/png");
  a.click();
};
$("toggle-console").onclick = () => ($("console").hidden = !$("console").hidden);
$("eval").addEventListener("submit", (e) => {
  e.preventDefault();
  const js = $("js").value;
  if (js.trim()) send({ t: "eval", js });
  $("js").value = "";
});

// --- input forwarding ---

function mods(e) {
  return (e.ctrlKey ? 1 : 0) | (e.shiftKey ? 2 : 0) | (e.altKey ? 4 : 0);
}
function viewPos(e) {
  const r = canvas.getBoundingClientRect();
  return {
    x: Math.round(((e.clientX - r.left) / r.width) * viewWidth),
    y: Math.round(((e.clientY - r.top) / r.height) * viewHeight),
  };
}

let pendingMove = null;
canvas.addEventListener("mousemove", (e) => {
  if (!pendingMove) requestAnimationFrame(() => {
    send(pendingMove);
    pendingMove = null;
  });
  pendingMove = { t: "mouse", k: "move", ...viewPos(e), b: 0, m: mods(e) };
});
canvas.addEventListener("mousedown", (e) => {
  canvas.focus();
  send({ t: "mouse", k: "down", ...viewPos(e), b: e.button, m: mods(e) });
  e.preventDefault();
});
canvas.addEventListener("mouseup", (e) => send({ t: "mouse", k: "up", ...viewPos(e), b: e.button, m: mods(e) }));
canvas.addEventListener("dblclick", (e) => send({ t: "mouse", k: "dbl", ...viewPos(e), b: e.button, m: mods(e) }));
canvas.addEventListener("contextmenu", (e) => e.preventDefault());
canvas.addEventListener(
  "wheel",
  (e) => {
    e.preventDefault();
    const scale = e.deltaMode === 1 ? 40 : e.deltaMode === 2 ? viewHeight : 1;
    // WebKit wheel deltas are positive towards the top/left (the opposite of DOM wheel events).
    send({ t: "wheel", ...viewPos(e), dx: -e.deltaX * scale, dy: -e.deltaY * scale, m: mods(e) });
  },
  { passive: false },
);

canvas.addEventListener("keydown", (e) => {
  if (e.altKey && (e.key === "ArrowLeft" || e.key === "ArrowRight")) {
    send({ t: e.key === "ArrowLeft" ? "back" : "fwd" });
    e.preventDefault();
    return;
  }
  send({ t: "key", k: "down", code: e.keyCode, m: mods(e), r: e.repeat });
  // Characters (and Enter) are sent as text input too, like a platform keypress/char event.
  if (!e.ctrlKey && !e.altKey && !e.metaKey) {
    if (e.key.length === 1 || e.key.length === 2) send({ t: "text", c: e.key.codePointAt(0) });
    else if (e.key === "Enter") send({ t: "text", c: 13 });
  }
  e.preventDefault();
});
canvas.addEventListener("keyup", (e) => {
  send({ t: "key", k: "up", code: e.keyCode, m: mods(e), r: false });
  e.preventDefault();
});
canvas.addEventListener("paste", (e) => {
  const text = e.clipboardData.getData("text/plain");
  for (const ch of text) send({ t: "text", c: ch.codePointAt(0) });
  e.preventDefault();
});

connect();
