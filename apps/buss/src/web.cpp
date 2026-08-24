#include "web.h"
#include "config.h"
#include "settings.h"

#include <web_core.h>   // shared WebServer + mDNS + /restart + /wifi handlers
#include <ArduinoJson.h>

static bool changed = false;

// The whole settings UI. Served by the device, but all SL API traffic
// (station typeahead over the full sites list, departure previews) runs in the
// browser — the device never downloads the 1.3MB station list.
static const char PAGE[] PROGMEM = R"html(<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>buss-display</title>
<style>
  :root{--bg:#0b0e14;--card:#141926;--fg:#e6e9f0;--dim:#8b93a7;--acc:#ffd23c;
        --line:#232a3d}
  *{box-sizing:border-box}
  body{margin:0;background:var(--bg);color:var(--fg);
       font:16px/1.5 -apple-system,system-ui,sans-serif;padding:1.2rem}
  main{max-width:26rem;margin:0 auto}
  h1{font-size:1.2rem;color:var(--acc);margin:.2rem 0 1rem}
  .card{background:var(--card);border:1px solid var(--line);border-radius:.7rem;
        padding:1rem;margin-bottom:1rem}
  label{display:block;color:var(--dim);font-size:.8rem;margin-bottom:.3rem;
        text-transform:uppercase;letter-spacing:.05em}
  input[type=text]{width:100%;padding:.6rem;border-radius:.5rem;
        border:1px solid var(--line);background:#0e1220;color:var(--fg);font-size:1rem}
  #hits{margin:.3rem 0 0;padding:0;list-style:none}
  #hits li{padding:.45rem .6rem;border-radius:.4rem;cursor:pointer}
  #hits li:hover{background:#1c2438}
  #hits small{color:var(--dim)}
  .dir{display:flex;flex-direction:column;gap:.45rem}
  .dir label{display:flex;gap:.5rem;align-items:baseline;text-transform:none;
        font-size:.95rem;color:var(--fg);cursor:pointer;letter-spacing:0}
  .dir small{color:var(--dim)}
  button{width:100%;padding:.7rem;border:0;border-radius:.5rem;background:var(--acc);
        color:#141414;font-size:1rem;font-weight:600;cursor:pointer}
  button:disabled{opacity:.4}
  #msg{text-align:center;color:var(--dim);min-height:1.4rem;margin-top:.5rem}
  table{width:100%;border-collapse:collapse;font-variant-numeric:tabular-nums}
  td{padding:.15rem 0;border:0}
  td:first-child{color:var(--acc);width:3.2rem;font-weight:600}
  td:last-child{text-align:right;color:#7ee787}
  td.blue{color:#6cb0ff}
</style></head><body><main>
<h1>buss-display</h1>

<div class="card">
  <label for="q">Station</label>
  <input type="text" id="q" placeholder="Type station name..." autocomplete="off">
  <ul id="hits"></ul>
  <div id="chosen" style="margin-top:.4rem;color:var(--dim)"></div>
</div>

<div class="card">
  <label>Direction</label>
  <div class="dir">
    <label><input type="radio" name="dir" value="0" checked> Both directions</label>
    <label><input type="radio" name="dir" value="1"> Direction 1 <small id="d1"></small></label>
    <label><input type="radio" name="dir" value="2"> Direction 2 <small id="d2"></small></label>
  </div>
</div>

<div class="card">
  <label for="minm">Hide buses leaving sooner than (minutes)</label>
  <input type="text" id="minm" inputmode="numeric" value="3" style="width:5rem">
  <div style="color:var(--dim);font-size:.85rem;margin-top:.3rem">
    You won't make a bus that leaves "Nu" — walk time to the stop.</div>
</div>

<div class="card">
  <label>Next departures (preview)</label>
  <table id="prev"><tr><td colspan="3" style="color:var(--dim)">pick a station</td></tr></table>
</div>

<button id="save" disabled>Save to display</button>
<div id="msg"></div>

<script>
const API='https://transport.integration.sl.se/v1';
let sites=null, chosen=null, deps=[];
const $=id=>document.getElementById(id);

async function loadSites(){
  if(sites) return;
  $('msg').textContent='loading station list...';
  sites=await (await fetch(API+'/sites?expand=false')).json();
  $('msg').textContent='';
}

$('q').addEventListener('input', async e=>{
  await loadSites();
  const q=e.target.value.trim().toLowerCase();
  const ul=$('hits'); ul.innerHTML='';
  if(q.length<2) return;
  sites.filter(s=>s.name.toLowerCase().includes(q)).slice(0,10).forEach(s=>{
    const li=document.createElement('li');
    li.innerHTML=`${s.name} <small>${s.note??''} #${s.id}</small>`;
    li.onclick=()=>select(s);
    ul.appendChild(li);
  });
});

function select(s){
  chosen=s;
  $('hits').innerHTML=''; $('q').value=s.name;
  $('chosen').textContent=`site #${s.id}`;
  $('save').disabled=false;
  preview();
}

async function preview(){
  if(!chosen) return;
  const r=await fetch(`${API}/sites/${chosen.id}/departures?transport=BUS&forecast=90`);
  deps=(await r.json()).departures.filter(d=>d.state!=='CANCELLED');
  // direction_code is per-line, not geographic — show sampled destinations so
  // you can see what each code means at THIS station before saving.
  for(const c of [1,2]){
    const dests=[...new Set(deps.filter(d=>d.direction_code===c).map(d=>d.destination))];
    $('d'+c).textContent=dests.length?'→ '+dests.slice(0,3).join(', '):'(no departures)';
  }
  renderPreview();
}

function dir(){return +document.querySelector('input[name=dir]:checked').value}
function minm(){return Math.max(0,parseInt($('minm').value)||0)}
// Same logic as the firmware: "Nu"->0, "5 min"->5, "17:42"->far future.
function mins(d){
  if(d.display.includes(':')) return 999;
  const m=parseInt(d.display); return isNaN(m)?0:m;
}

function renderPreview(){
  const rows=deps.filter(d=>(!dir()||d.direction_code===dir())&&mins(d)>=minm())
                 .slice(0,10);
  $('prev').innerHTML=rows.length
    ? rows.map(d=>`<tr><td${d.line.group_of_lines==='Blåbuss'?' class="blue"':''}>${d.line.designation}</td><td>${d.destination}</td><td>${d.display}</td></tr>`).join('')
    : '<tr><td colspan="3" style="color:var(--dim)">no departures</td></tr>';
}

document.querySelectorAll('input[name=dir]').forEach(r=>r.onchange=renderPreview);
$('minm').addEventListener('input',renderPreview);

$('save').onclick=async()=>{
  const body={siteId:chosen.id,name:chosen.name,direction:dir(),minMinutes:minm()};
  const r=await fetch('/api/settings',{method:'POST',
    headers:{'Content-Type':'application/json'},body:JSON.stringify(body)});
  $('msg').textContent=r.ok?'saved — display updates in a few seconds':'save failed';
};

(async()=>{  // prefill from current device settings
  const s=await (await fetch('/api/settings')).json();
  if(s.siteId){
    chosen={id:s.siteId,name:s.name};
    $('q').value=s.name; $('chosen').textContent=`site #${s.siteId}`;
    document.querySelector(`input[name=dir][value="${s.direction}"]`).checked=true;
    $('minm').value=s.minMinutes??3;
    $('save').disabled=false;
    preview();
  }
})();
</script>
</main></body></html>)html";

static void handleGetSettings() {
  JsonDocument doc;
  doc["siteId"] = settings.siteId;
  doc["name"] = settings.stationName;
  doc["direction"] = settings.direction;
  doc["minMinutes"] = settings.minMinutes;
  String out;
  serializeJson(doc, out);
  webServer().send(200, "application/json", out);
}

static void handlePostSettings() {
  WebServer& server = webServer();
  JsonDocument doc;
  if (deserializeJson(doc, server.arg("plain"))) {
    server.send(400, "text/plain", "bad json");
    return;
  }
  settings.siteId = doc["siteId"] | settings.siteId;
  strlcpy(settings.stationName, doc["name"] | settings.stationName,
          sizeof(settings.stationName));
  settings.direction = doc["direction"] | 0;
  settings.minMinutes = doc["minMinutes"] | DEFAULT_MIN_MINUTES;
  settingsSave();
  changed = true;
  server.send(200, "application/json", "{\"ok\":true}");
}

void webStart() {
  WebServer& server = webServer();
  // Serve the static page with the shared night-mode section injected before
  // </main>. The night form posts to sleep-core's /night handler (registered
  // below); it carries its own styling, so it works in buss's dark theme too.
  server.on("/", HTTP_GET, [] {
    String p = FPSTR(PAGE);
    String night = "<div class=\"card\">" + sleepWebSection(settings.night) + "</div>";
    p.replace("</main>", night + "</main>");
    webServer().send(200, "text/html", p);
  });
  server.on("/api/settings", HTTP_GET, handleGetSettings);
  server.on("/api/settings", HTTP_POST, handlePostSettings);
  sleepWebRegister(&settings.night, settingsSave);   // POST /night (shared)
  webCoreBegin(HOSTNAME);   // mDNS + /restart + /wifi + server.begin()
}

void webHandle() { webCoreHandle(); }

bool webSettingsChanged() {
  bool c = changed;
  changed = false;
  return c;
}
