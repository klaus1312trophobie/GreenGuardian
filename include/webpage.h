#pragma once
#include <Arduino.h>

const char INDEX_HTML[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Green Guardian</title>
<link rel="icon" href="data:image/svg+xml,<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 100 100'><text y='.9em' font-size='90'>🌿</text></svg>">
<style>
:root{--bg:#0b140d;--card:#13221699;--line:#24402a;--green:#5fd068;--lime:#b5e655;--gold:#f5c542;--red:#ff6b5b;--blue:#58b8ff;--txt:#e6f2e7;--mut:#8fae94}
*{box-sizing:border-box}
body{margin:0;font-family:system-ui,-apple-system,Segoe UI,Roboto,sans-serif;color:var(--txt);
 background:radial-gradient(circle at 20% 0%,#1d3b22 0,transparent 50%),radial-gradient(circle at 100% 100%,#22361a 0,transparent 45%),var(--bg);min-height:100vh}
#bgleaf{position:fixed;right:-80px;bottom:-60px;width:420px;opacity:.05;pointer-events:none;z-index:0}
.wrap{max-width:1100px;margin:auto;padding:18px;position:relative;z-index:1}
header{display:flex;align-items:center;gap:14px;margin-bottom:14px}
header svg{width:56px;height:56px;filter:drop-shadow(0 0 10px #5fd06866);animation:sway 6s ease-in-out infinite;transform-origin:50% 90%}
@keyframes sway{50%{transform:rotate(-6deg)}}
h1{margin:0;font-size:1.7rem;letter-spacing:.5px;background:linear-gradient(90deg,var(--green),var(--lime),var(--gold));-webkit-background-clip:text;background-clip:text;color:transparent}
.sub{color:var(--mut);font-size:.9rem}
.clock{margin-left:auto;text-align:right;color:var(--mut);font-size:.85rem}
.banner{padding:12px 16px;border-radius:14px;margin-bottom:16px;font-weight:600;border:1px solid var(--line);background:#1a3a1f88}
.banner.dry{background:#4a1d1688;border-color:#ff6b5b66;animation:pulse 1.6s infinite}
@keyframes pulse{50%{box-shadow:0 0 18px #ff6b5b55}}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(240px,1fr));gap:14px}
.card{background:var(--card);border:1px solid var(--line);border-radius:18px;padding:16px;backdrop-filter:blur(6px)}
.card h2{margin:0 0 10px;font-size:.8rem;text-transform:uppercase;letter-spacing:1.5px;color:var(--mut);font-weight:600;text-align:center}
.gauge{display:flex;align-items:center;gap:14px}
.gauge svg{width:110px;height:110px;flex:none}
.gauge .val{font-size:2rem;font-weight:700}
.gauge .unit{font-size:1rem;color:var(--mut)}
.gauge .hint{font-size:.8rem;color:var(--mut);margin-top:4px}
.ring-bg{fill:none;stroke:#ffffff12;stroke-width:10}
.ring{fill:none;stroke-width:10;stroke-linecap:round;transform:rotate(-90deg);transform-origin:50% 50%;transition:stroke-dasharray .8s}
table{width:100%;border-collapse:collapse;font-size:.95rem}
td,th{padding:7px 4px;border-bottom:1px solid var(--line);text-align:right}
td:first-child,th:first-child{text-align:left}
th{color:var(--mut);font-weight:500;font-size:.8rem}
.lo{color:var(--blue)}.hi{color:var(--gold)}
button{background:linear-gradient(135deg,#2f7d38,#4caf50);color:#fff;border:0;border-radius:12px;padding:10px 14px;font-weight:600;cursor:pointer;margin:4px 3px 0;font-size:.9rem}
.btns{text-align:center}
button:hover{filter:brightness(1.15)}
button.alt{background:#24402a}button.warn{background:linear-gradient(135deg,#8a5a12,#c98a1c)}
.big{width:100%;padding:14px;font-size:1.05rem;margin:6px 0}
input{background:#0b140d;color:var(--txt);border:1px solid var(--line);border-radius:10px;padding:8px;width:80px}
label{display:flex;justify-content:space-between;align-items:center;margin:8px 0;color:var(--mut)}
.kv{display:flex;justify-content:space-between;padding:5px 0;border-bottom:1px dashed var(--line);font-size:.9rem}
.kv span:first-child{color:var(--mut)}
.wide{grid-column:1/-1}
canvas{width:100%;height:260px;display:block}
.legend{display:flex;gap:16px;font-size:.85rem;color:var(--mut);margin-bottom:6px;flex-wrap:wrap}
.legend i{display:inline-block;width:12px;height:3px;border-radius:2px;margin-right:6px;vertical-align:middle}
#toast{position:fixed;bottom:20px;left:50%;transform:translateX(-50%) translateY(100px);background:#1d3b22;border:1px solid var(--green);padding:12px 18px;border-radius:12px;transition:.3s;z-index:9}
#toast.show{transform:translateX(-50%) translateY(0)}
footer{text-align:center;color:var(--mut);font-size:.8rem;margin:22px 0 6px}
</style>
</head>
<body>
<svg id="bgleaf" viewBox="-60 -70 120 100"></svg>
<div class="wrap">
<header>
 <svg id="logo" viewBox="-60 -70 120 100"></svg>
 <div><h1>Green Guardian</h1><div class="sub">Cannabis Grow Monitor &middot; growing strong since boot</div></div>
 <div class="clock"><div id="clock">--:--</div><div id="conn">connecting...</div></div>
</header>

<div id="banner" class="banner">🌱 Loading grow data...</div>

<div class="grid">
 <div class="card"><h2>💧 Soil Moisture</h2><div class="gauge" id="g-soil"></div></div>
 <div class="card"><h2>🌡️ Temperature</h2><div class="gauge" id="g-temp"></div></div>
 <div class="card"><h2>💨 Air Humidity</h2><div class="gauge" id="g-hum"></div></div>
 <div class="card"><h2>🍃 VPD (Leaf)</h2><div class="gauge" id="g-vpd"></div></div>

 <div class="card wide">
  <h2>📈 Last 24 hours</h2>
  <div class="legend"><span><i style="background:var(--green)"></i>Soil %</span><span><i style="background:var(--gold)"></i>Temp °C</span><span><i style="background:var(--blue)"></i>Humidity %</span><span><i style="background:var(--red)"></i>Water threshold</span></div>
  <canvas id="chart"></canvas>
 </div>

 <div class="card">
  <h2>🏆 Min / Max</h2>
  <table>
   <tr><th></th><th>Min</th><th>Now</th><th>Max</th></tr>
   <tr><td>Soil</td><td class="lo" id="sMin">--</td><td id="sNow">--</td><td class="hi" id="sMax">--</td></tr>
   <tr><td>Temp</td><td class="lo" id="tMin">--</td><td id="tNow">--</td><td class="hi" id="tMax">--</td></tr>
   <tr><td>Humidity</td><td class="lo" id="hMin">--</td><td id="hNow">--</td><td class="hi" id="hMax">--</td></tr>
  </table>
  <div class="sub" style="margin-top:8px">Since: <span id="mmSince">--</span></div>
  <div class="btns"><button class="alt" onclick="post('/api/reset-minmax','Reset all min/max values?')">↺ Reset Min/Max</button></div>
 </div>

 <div class="card">
  <h2>🚿 Water &amp; Sensor</h2>
  <button class="big" onclick="post('/api/calibrate?type=wet','Did you just water thoroughly? The current reading will become 100%.')">💧 Just watered<br><small>&rarr; set 100%</small></button>
  <div class="kv"><span>Raw sensor value</span><span id="raw">--</span></div>
  <div class="kv"><span>Dry (0%) / Wet (100%)</span><span><span id="dry">--</span> / <span id="wet">--</span></span></div>
  <div class="kv"><span>Last watered</span><span id="lastW">--</span></div>
  <div class="btns">
  <button class="alt" onclick="post('/api/watered')">📝 Log watering</button>
  <button class="warn" onclick="post('/api/calibrate?type=dry','Is the sensor in completely DRY soil (or in the air)? The current reading will become 0%.')">🏜️ Set dry (0%)</button>
  </div>
 </div>

 <div class="card">
  <h2>⚙️ Settings &amp; Telegram</h2>
  <label>Water alert below (%)<input type="number" id="thr" min="5" max="95"></label>
  <label>Daily report hour (-1 = off)<input type="number" id="rep" min="-1" max="23"></label>
  <div class="btns">
  <button onclick="saveSettings()">💾 Save</button>
  <button class="alt" onclick="post('/api/telegram-test')">📨 Telegram test</button>
  </div>
  <div class="kv" style="margin-top:10px"><span>Telegram bot</span><span id="tg">--</span></div>
 </div>

 <div class="card">
  <h2>🔬 Climate details</h2>
  <div class="kv"><span>Dew point</span><span id="dew">--</span></div>
  <div class="kv"><span>Heat index</span><span id="hix">--</span></div>
  <div class="kv"><span>VPD stage</span><span id="vstage">--</span></div>
  <div class="kv"><span>WiFi signal</span><span id="rssi">--</span></div>
  <div class="kv"><span>IP</span><span id="ip">--</span></div>
  <div class="kv"><span>Uptime</span><span id="up">--</span></div>
  <div class="kv"><span>Free heap (min / largest block)</span><span id="heap">--</span></div>
  <div class="kv"><span>DHT22 read errors</span><span id="derr">--</span></div>
 </div>
</div>
<footer>🌿 All values are 10-second averages &middot; Grow responsibly &middot; Green Guardian 🌿</footer>
</div>
<div id="toast"></div>

<script>
const $=id=>document.getElementById(id);
let D=null,H=[];

function leaf(){
 const L=[[0,58],[-35,50],[35,50],[-70,38],[70,38],[-108,20],[108,20]];
 let s='<g fill="#5fd068">';
 for(const[a,l]of L){const w=l*.2;
  s+=`<path transform="rotate(${a})" d="M0 0 Q${w} ${-l*.45} 0 ${-l} Q${-w} ${-l*.45} 0 0Z"/>`;}
 return s+'</g><rect x="-1.5" y="0" width="3" height="22" rx="1.5" fill="#3f9a48"/>';
}
$('logo').innerHTML=leaf();$('bgleaf').innerHTML=leaf();

function gauge(id,val,max,txt,unit,color,hint){
 const r=45,c=2*Math.PI*r,p=val==null?0:Math.max(0,Math.min(1,val/max));
 $(id).innerHTML=`<svg viewBox="0 0 110 110"><circle class="ring-bg" cx="55" cy="55" r="${r}"/>
 <circle class="ring" cx="55" cy="55" r="${r}" stroke="${color}" stroke-dasharray="${c*p} ${c}"/></svg>
 <div><div class="val">${txt}<span class="unit"> ${unit}</span></div><div class="hint">${hint}</div></div>`;
}
const short=s=>s.replace('vegetative','veg.').replace('flowering','flower');
const f=(v,d=1)=>v==null?'--':Number(v).toFixed(d);
function ago(t,now){if(!t||!now)return'never';let s=now-t;if(s<3600)return Math.floor(s/60)+' min ago';if(s<86400)return Math.floor(s/3600)+' h ago';return Math.floor(s/86400)+' d '+Math.floor(s%86400/3600)+' h ago';}
function upt(s){const d=Math.floor(s/86400),h=Math.floor(s%86400/3600),m=Math.floor(s%3600/60);return(d?d+'d ':'')+h+'h '+m+'m';}

function tempHint(t){if(t==null)return'sensor error';if(t<18)return'🥶 too cold';if(t<=28)return'✅ comfy for the girls';if(t<=31)return'🌞 getting warm';return'🔥 too hot!';}
function humHint(h){if(h==null)return'sensor error';if(h<35)return'🏜️ very dry air';if(h<=70)return'✅ good range';return'🍄 mold risk';}
function soilHint(s,th){if(s==null)return'sensor error';if(s<th)return'🥵 thirsty – water now!';if(s<60)return'🙂 OK, water soon';return'💚 well hydrated';}

function render(){
 const d=D,th=d.threshold;
 gauge('g-soil',d.moisture,100,d.moisture==null?'--':d.moisture,'%',d.moisture!=null&&d.moisture<th?'var(--red)':'var(--green)',soilHint(d.moisture,th));
 gauge('g-temp',d.temp,40,f(d.temp),'°C','var(--gold)',tempHint(d.temp));
 gauge('g-hum',d.humidity,100,f(d.humidity),'%','var(--blue)',humHint(d.humidity));
 gauge('g-vpd',d.vpd,2,f(d.vpd,2),'kPa','var(--lime)',short(d.vpdStage));
 const m=d.minmax;
 $('sMin').textContent=f(m.soilMin,0)+'%';$('sNow').textContent=(d.moisture??'--')+'%';$('sMax').textContent=f(m.soilMax,0)+'%';
 $('tMin').textContent=f(m.tempMin)+'°';$('tNow').textContent=f(d.temp)+'°';$('tMax').textContent=f(m.tempMax)+'°';
 $('hMin').textContent=f(m.humMin)+'%';$('hNow').textContent=f(d.humidity)+'%';$('hMax').textContent=f(m.humMax)+'%';
 $('mmSince').textContent=m.since?new Date(m.since*1000).toLocaleString():'boot';
 $('raw').textContent=f(d.soilRaw,0);$('dry').textContent=d.dryRaw;$('wet').textContent=d.wetRaw;
 $('lastW').textContent=ago(d.lastWatered,d.now);
 if(document.activeElement!==$('thr'))$('thr').value=th;
 if(document.activeElement!==$('rep'))$('rep').value=d.reportHour;
 $('tg').textContent=d.telegram?'✅ connected':'⚠️ not configured';
 $('dew').textContent=f(d.dewPoint)+' °C';$('hix').textContent=f(d.heatIndex)+' °C';$('vstage').textContent=short(d.vpdStage);
 $('rssi').textContent=d.rssi?d.rssi+' dBm':'--';$('ip').textContent=d.ip;$('up').textContent=upt(d.uptime);$('heap').textContent=Math.round(d.freeHeap/1024)+' kB ('+Math.round(d.minHeap/1024)+' / '+Math.round(d.maxAlloc/1024)+')';$('derr').textContent=d.dhtErrors;
 $('clock').textContent=d.now?new Date(d.now*1000).toLocaleTimeString():'no time sync';
 const b=$('banner');
 if(d.warmingUp){b.className='banner';b.textContent='⏳ Warming up sensors (10 s average)...';}
 else if(d.moisture!=null&&d.moisture<th){b.className='banner dry';b.textContent=`🥵 Your girls are thirsty! Soil at ${d.moisture}% – time to water 💧`;}
 else if(!d.dhtOk){b.className='banner dry';b.textContent='⚠️ DHT22 not responding – check wiring on GPIO 32';}
 else{b.className='banner';b.textContent='🌿 All good – plants are happy & hydrated. Stay green!';}
}

async function load(){
 try{const r=await fetch('/data');D=await r.json();render();$('conn').textContent='🟢 live';}
 catch(e){$('conn').textContent='🔴 offline';}
}
async function loadHist(){try{H=await (await fetch('/history')).json();chart();}catch(e){}}

function chart(){
 const cv=$('chart'),dpr=devicePixelRatio||1,W=cv.clientWidth,Ht=cv.clientHeight;
 cv.width=W*dpr;cv.height=Ht*dpr;const g=cv.getContext('2d');g.scale(dpr,dpr);
 const pl=34,pr=10,pt=10,pb=24,w=W-pl-pr,h=Ht-pt-pb;
 g.clearRect(0,0,W,Ht);g.font='11px system-ui';g.fillStyle='#8fae94';g.strokeStyle='#ffffff14';
 for(let v=0;v<=100;v+=20){const y=pt+h-h*v/100;g.beginPath();g.moveTo(pl,y);g.lineTo(pl+w,y);g.stroke();g.fillText(v,6,y+4);}
 if(H.length<2){g.fillText('Collecting data... (one point every 5 minutes)',pl+10,pt+h/2);return;}
 const t0=H[0][0],t1=H[H.length-1][0],useT=t0>0&&t1>t0;
 const X=(p,i)=>pl+(useT?(p[0]-t0)/(t1-t0):i/(H.length-1))*w,Y=v=>pt+h-h*Math.max(0,Math.min(100,v))/100;
 if(useT){g.fillStyle='#8fae94';for(let k=0;k<=4;k++){const t=t0+(t1-t0)*k/4,x=pl+w*k/4;const s=new Date(t*1000).toLocaleTimeString([], {hour:'2-digit',minute:'2-digit'});g.fillText(s,Math.min(x-14,W-40),Ht-6);}}
 if(D){g.strokeStyle='#ff6b5b';g.setLineDash([5,5]);g.beginPath();g.moveTo(pl,Y(D.threshold));g.lineTo(pl+w,Y(D.threshold));g.stroke();g.setLineDash([]);}
 [[1,'#5fd068'],[2,'#f5c542'],[3,'#58b8ff']].forEach(([k,c])=>{
  g.strokeStyle=c;g.lineWidth=2.2;g.beginPath();let on=false;
  H.forEach((p,i)=>{if(p[k]==null){on=false;return;}const x=X(p,i),y=Y(p[k]);on?g.lineTo(x,y):g.moveTo(x,y);on=true;});
  g.stroke();
 });
 if(H.length){const p=H[H.length-1];if(p[1]!=null){g.fillStyle='#5fd068';g.beginPath();g.arc(X(p,H.length-1),Y(p[1]),4,0,7);g.fill();}}
}

function toast(m){const t=$('toast');t.textContent=m;t.classList.add('show');setTimeout(()=>t.classList.remove('show'),3200);}
async function post(url,confirmMsg){
 if(confirmMsg&&!confirm(confirmMsg))return;
 try{const r=await fetch(url,{method:'POST'});const j=await r.json();toast((j.ok?'✅ ':'⚠️ ')+j.msg);load();}
 catch(e){toast('⚠️ Request failed');}
}
function saveSettings(){post(`/api/settings?threshold=${$('thr').value}&reportHour=${$('rep').value}`);}

load();loadHist();setInterval(load,2000);setInterval(loadHist,60000);addEventListener('resize',chart);
</script>
</body>
</html>)rawliteral";
