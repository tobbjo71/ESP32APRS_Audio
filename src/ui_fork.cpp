/*
 SM0TGY fork of ESP32APRS_Audio (GPLv3): mobile web UI.
 Shell page with bottom navigation (Heard, Map, Packets, Messages, More),
 the pages Heard / Map (radar, drawn offline) / Packets / More, and
 /status.json used by the header chips and summaries.
 All markup lives in flash (PROGMEM) and is sent without heap copies.
*/
#include <Arduino.h>
#include "webservice.h"

extern unsigned long upTimeStamp;
extern unsigned long lastTxMillis;
extern int btClientState();
extern char *heardCacheDup();
extern char *pktLogJson();
extern AsyncWebServerResponse *beginBufferResponse(AsyncWebServerRequest *request, char *buf);

#define ICON(d) "<svg width=\"24\" height=\"24\" viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"2\" stroke-linecap=\"round\" stroke-linejoin=\"round\" aria-hidden=\"true\">" d "</svg>"
#define CHEV "<svg width=\"16\" height=\"16\" viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"2.5\" stroke-linecap=\"round\" aria-hidden=\"true\"><path d=\"M9 6l6 6-6 6\"/></svg>"

static const char UI_SHELL[] PROGMEM =
R"HTML(<!DOCTYPE html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<meta http-equiv="Cache-Control" content="no-cache, no-store, must-revalidate">
<title>APRS Tracker</title>
<link rel="stylesheet" href="style.css?v=)HTML" FORK_VERSION R"HTML(">
<script src="/jquery-3.7.1.js"></script>
</head><body>
<div class="container">
<header class="top"><div class="top1"><div><div id="hCall" class="mono big">&nbsp;</div><div id="hSub" class="sub"></div></div><div id="hTime" class="sub"></div></div><div id="chips" class="chips"></div></header>
<div id="back" class="back" hidden><a href="#" onclick="go('more');return false">&lsaquo; Settings</a><span id="backT"></span></div>
<div class="contentwide" id="contentmain"></div>
</div>
<nav class="bnav" aria-label="Main menu">
<button type="button" data-p="heard" onclick="go('heard')">)HTML" ICON("<path d=\"M4 6h16M4 12h16M4 18h10\"/>") R"HTML(Heard</button>
<button type="button" data-p="map" onclick="go('map')">)HTML" ICON("<circle cx=\"12\" cy=\"12\" r=\"9\"/><circle cx=\"12\" cy=\"12\" r=\"4\"/><path d=\"M12 12l6-6\"/>") R"HTML(Map</button>
<button type="button" data-p="pkt" onclick="go('pkt')">)HTML" ICON("<path d=\"M7 4v16M7 20l-3-3M7 20l3-3M17 20V4M17 4l-3 3M17 4l3 3\"/>") R"HTML(Packets</button>
<button type="button" data-p="msg" onclick="go('msg')">)HTML" ICON("<path d=\"M4 5h16v11H9l-5 4z\"/>") R"HTML(Messages</button>
<button type="button" data-p="more" onclick="go('more')">)HTML" ICON("<circle cx=\"5\" cy=\"12\" r=\"1.5\"/><circle cx=\"12\" cy=\"12\" r=\"1.5\"/><circle cx=\"19\" cy=\"12\" r=\"1.5\"/>") R"HTML(More</button>
</nav>
<div id="symov" class="symov" hidden></div>
<script>
var PG={heard:'/heard',map:'/radar',pkt:'/pkt',msg:'/msg',more:'/more'},ST=null;
function navOn(p){document.querySelectorAll('.bnav button').forEach(function(b){b.classList.toggle('on',b.dataset.p==p)})}
function stopPage(){window.onStatus=null;if(window.pageStop){try{window.pageStop()}catch(e){}window.pageStop=null}}
function go(p){stopPage();navOn(p);document.getElementById('back').hidden=true;$('#contentmain').load(PG[p]);window.scrollTo(0,0)}
function setting(u,t){stopPage();navOn('more');document.getElementById('back').hidden=false;document.getElementById('backT').textContent=t;$('#contentmain').load(u);window.scrollTo(0,0)}
function selectTab(e,n){var m={DashBoard:'heard',RX:'pkt',MSG:'msg'};go(m[n]||'more')}
function ago(s){if(s<60)return s+' s';if(s<3600)return Math.round(s/60)+' min';return Math.round(s/3600)+' h'}
function fup(u){var d=Math.floor(u/86400),h=Math.floor(u%86400/3600),m=Math.floor(u%3600/60);return(d?d+' d ':'')+h+' h '+m+' min'}
function chip(t,c){return '<span class="chip '+(c||'')+'"><i></i>'+t+'</span>'}
function st(){fetch('/status.json').then(function(r){return r.json()}).then(function(s){ST=s;
document.getElementById('hCall').textContent=s.call;document.getElementById('hSub').textContent=s.host+' \u00b7 v'+s.ver;document.getElementById('hTime').textContent=s.time;
var c=s.gps.fix?chip('GPS '+s.gps.sat+' sat'):chip('GPS no fix','w');
c+=s.bt==2?chip('BT app connected'):(s.bt==1?chip('BT waiting','off'):'');
c+=s.ltx<0?chip('No TX yet','off'):chip('TX '+ago(s.ltx)+' ago');
document.getElementById('chips').innerHTML=c;if(window.onStatus){try{window.onStatus(s)}catch(e){}}
}).catch(function(){document.getElementById('chips').innerHTML=chip('No contact','w')})}
var SYMH={1:[34,68,74,123,124,125,126],2:[34,37,49,50,51,52,53,54,55,56,61,77,89,90,92,93,108,113,120,124,125,126]};
var SYMC=[[62,1],[45,1],[35,1],[91,1],[106,1],[107,1],[60,1],[98,1],[115,1],[39,1],[118,1],[82,1],[117,1],[102,1]];
function symBtn(c,t,s){return '<button type="button" title="'+(t==1?'/':'\\')+String.fromCharCode(c)+'" onclick="symSet('+s+','+c+','+t+')"><img src="/symbols/icons/'+c+'-'+t+'.png" alt=""></button>'}
function symPick(sel){var s=(sel===undefined)?'null':sel,h='<div class="symbox"><div class="symhd"><b>Choose symbol</b><button class="button" type="button" onclick="symClose()">Close</button></div><div class="gh">Common</div><div class="symgrid">';
SYMC.forEach(function(x){h+=symBtn(x[0],x[1],s)});
[1,2].forEach(function(t){h+='</div><div class="gh">'+(t==1?'Primary table /':'Alternate table \\')+'</div><div class="symgrid">';for(var c=33;c<127;c++){if(SYMH[t].indexOf(c)<0)h+=symBtn(c,t,s)}});
var o=document.getElementById('symov');o.innerHTML=h+'</div></div>';o.hidden=false;o.onclick=function(e){if(e.target===o)symClose()}}
function symClose(){document.getElementById('symov').hidden=true}
function symSet(sel,c,t){symClose();if(sel===null)setValue(c,t);else setValue(sel,c,t)}
if(window.EventSource){var es=new EventSource('/eventMsg');es.addEventListener('chatMsg',function(e){var m=document.getElementById('chatMsg');if(m)m.innerHTML=e.data},false)}
$(document).on('submit','#contentmain form',function(e){if(!e.isDefaultPrevented()){e.preventDefault();alert('This page could not save: its script did not start. Please report the page name.')}});
$.ajaxPrefilter(function(o){if(o.url!='/update'&&!o.timeout)o.timeout=20000});$(document).ajaxComplete(function(){$('#contentmain button:disabled').prop('disabled',false)});
st();setInterval(st,5000);var DL={'#system':['/system','System and status'],'#radiotest':['/tnc2','Radio level and TX test']}[location.hash];if(DL){history.replaceState(null,'','/');setting(DL[0],DL[1])}else go('heard');
</script>
</body></html>
)HTML";

static const char UI_HEARD[] PROGMEM = R"HTML(<h2 class="pg">Heard stations <span id="hc"></span></h2>
<div id="hl"><p class="muted">Loading&hellip;</p></div>
<script>(function(){
var CMP=['N','NE','E','SE','S','SW','W','NW'];
function tsec(t){var a=String(t).split(' '),b=(a[1]||'0:0:0').split(':');return(+a[0])*86400+(+b[0])*3600+(+b[1])*60+(+b[2])}
function esc(s){return String(s).replace(/[&<>"]/g,function(c){return{'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]})}
function via(p){if(!p)return'';if(p.indexOf('RF: DIRECT')==0)return'direct';if(p.indexOf('DIGI: ')==0)return'via '+p.substr(6);if(p.indexOf('INET:')==0)return'internet';if(p.indexOf('RF: ')==0)return p.substr(4);return p}
function lvl(a){var d=parseFloat(a);if(!isFinite(d))return'';var w=Math.max(0,Math.min(100,(d+40)/40*100)),c=d>-10?'var(--red)':(d>=-20?'var(--o5)':'var(--och)');return '<div class="lvl"><div><b style="width:'+w+'%;background:'+c+'"></b></div><span>'+d.toFixed(0)+' dBV</span></div>'}
function draw(d){d.sort(function(a,b){return tsec(b.time)-tsec(a.time)});
document.getElementById('hc').textContent=d.length?d.length+(d.length==1?' station':' stations'):'';
if(!d.length){document.getElementById('hl').innerHTML='<p class="muted">No stations heard yet.</p>';return}
var h='';d.forEach(function(r){var t=(String(r.time).split(' ')[1]||'').substr(0,5),pos=(r.km>=0)?((r.km<10?r.km.toFixed(1):Math.round(r.km))+' km \u00b7 '+r.brg+'\u00b0 '+CMP[Math.round(r.brg/45)%8]):'no position';
h+='<div class="card"><div class="ic"><img src="/symbols/icons/'+esc(r.icon)+'" alt="" width="24" height="24"></div><div class="b"><div class="r1"><span class="cs mono">'+esc(r.callsign)+'</span><span class="t">'+t+'</span></div><div>'+pos+' \u00b7 '+esc(via(r.path))+'</div><div class="m">'+esc(r.packet)+(r.packet==1?' packet':' packets')+'</div>'+lvl(r.audio)+'</div></div>'});
document.getElementById('hl').innerHTML=h}
function load(){fetch('/heard.json').then(function(r){if(r.status==503){setTimeout(load,1500);throw 0}return r.json()}).then(draw).catch(function(){})}
load();var T=setInterval(load,10000);window.pageStop=function(){clearInterval(T)};
})();</script>
)HTML";

static const char UI_RADAR[] PROGMEM = R"HTML(<h2 class="pg">Map <span>north up &middot; you in the centre</span></h2>
<div class="radar"><svg id="rd" viewBox="0 0 340 340" role="img" aria-label="Heard stations around own position"></svg></div>
<div class="seg" id="rsc"></div>
<p class="muted" id="rinfo"></p>
<script>(function(){var SC=[5,10,25,50,100,250],sc=0,D=[];
try{sc=+localStorage.getItem('rsc')||0}catch(e){}
function esc(s){return String(s).replace(/[&<>"]/g,function(c){return{'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]})}
function nice(k){for(var i=0;i<SC.length;i++)if(k<=SC[i])return SC[i];return SC[SC.length-1]}
function draw(){var vis=D.filter(function(r){return r.km>=0}),mx=4;vis.forEach(function(r){if(r.km>mx)mx=r.km});var S=sc||nice(mx*1.1),i;
var g='<circle cx="170" cy="170" r="160" fill="#F2EBD8" stroke="#D9CCA8" stroke-width="1.5"/>';
for(i=1;i<4;i++)g+='<circle cx="170" cy="170" r="'+(40*i)+'" fill="none" stroke="#D9CCA8"/>';
g+='<line x1="170" y1="10" x2="170" y2="330" stroke="#D9CCA8"/><line x1="10" y1="170" x2="330" y2="170" stroke="#D9CCA8"/><text x="170" y="25" text-anchor="middle" font-size="13" font-weight="700" fill="#2F3820">N</text>';
for(i=1;i<=4;i++)g+='<text x="'+(170+40*i-3)+'" y="183" text-anchor="end" font-size="10" fill="#5A5B4C">'+(+(S*i/4).toFixed(1))+(i==4?' km':'')+'</text>';
var out=0;vis.forEach(function(r){if(r.km>S){out++;return}var rr=160*r.km/S,a=r.brg*Math.PI/180,x=170+rr*Math.sin(a),y=170-rr*Math.cos(a),dg=r.path&&r.path.indexOf('DIGI')==0;
g+='<circle cx="'+x.toFixed(1)+'" cy="'+y.toFixed(1)+'" r="5.5" '+(dg?'fill="none" stroke="#4C5A2E" stroke-width="2.5"':'fill="#4C5A2E"')+'/>';
var e=x>240;g+='<text x="'+(e?x-8:x+8).toFixed(1)+'" y="'+(y-7).toFixed(1)+'" text-anchor="'+(e?'end':'start')+'" font-size="11" font-weight="700" fill="#23251C" font-family="ui-monospace,Menlo,monospace">'+esc(r.callsign)+'</text>'});
g+='<path d="M170 163 L174.5 175 L170 172 L165.5 175 Z" fill="#B5651D"/>';
document.getElementById('rd').innerHTML=g;
var b='';[0].concat(SC).forEach(function(v){b+='<button type="button" class="'+(v==sc?'on':'')+'" onclick="rdSc('+v+')">'+(v?v+' km':'Auto')+'</button>'});document.getElementById('rsc').innerHTML=b;
var n=D.length-vis.length;document.getElementById('rinfo').textContent=(n?n+' without position. ':'')+(out?out+' outside the scale. ':'')+'Filled dot = heard direct, ring = via digipeater.'}
window.rdSc=function(v){sc=v;try{localStorage.setItem('rsc',v)}catch(e){}draw()};
function load(){fetch('/heard.json').then(function(r){if(r.status==503){setTimeout(load,1500);throw 0}return r.json()}).then(function(d){D=d;draw()}).catch(function(){})}
draw();load();var T=setInterval(load,15000);window.pageStop=function(){clearInterval(T)};
})();</script>
)HTML";

static const char UI_PKT[] PROGMEM = R"HTML(<h2 class="pg">Packets <span id="pc"></span></h2>
<div class="tile" style="margin-bottom:10px"><small>Audio level, last received packet</small><b class="mono" id="pdb">&ndash;</b>
<div class="lvl"><div style="height:12px"><b id="pbar" style="height:12px;width:0"></b></div></div><span>&minus;40 &hellip; 0 dBV &middot; good &minus;20 &hellip; &minus;10</span></div>
<div class="seg" id="pf"><button type="button" class="on" data-f="">All</button><button type="button" data-f="RX">Received</button><button type="button" data-f="TX">Sent</button></div>
<div id="pl"><p class="muted">Loading&hellip;</p></div>
<script>(function(){var L=[],F='',stopped=false;
function lv(db){var b=document.getElementById('pbar');b.style.width=Math.max(0,Math.min(100,(db+40)/40*100))+'%';b.style.background=db>-10?'var(--red)':(db>=-20?'var(--o5)':'var(--och)');document.getElementById('pdb').textContent=db.toFixed(1)+' dBV'}
function esc(s){return String(s).replace(/[&<>"]/g,function(c){return{'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]})}
function draw(){var h='';L.forEach(function(p){if(F&&p.d!=F)return;h+='<div class="pk"><div class="h"><span class="tag '+(p.d=='TX'?'tx':'')+'">'+p.d+'</span>'+p.t+(p.db!==null?' &middot; '+p.db.toFixed(0)+' dBV':'')+'</div><div class="raw">'+esc(p.r)+'</div></div>'});
document.getElementById('pl').innerHTML=h||'<p class="muted">No packets in the last 60 minutes.</p>';document.getElementById('pc').textContent=L.length?L.length+' in the last 60 min':''}
document.querySelectorAll('#pf button').forEach(function(b){b.onclick=function(){F=b.dataset.f;document.querySelectorAll('#pf button').forEach(function(x){x.classList.toggle('on',x==b)});draw()}});
function hm(ts){var t=new Date(Number(ts)*1000);return t.getFullYear()<2020?'--:--':('0'+t.getHours()).slice(-2)+':'+('0'+t.getMinutes()).slice(-2)+':'+('0'+t.getSeconds()).slice(-2)}
function dbv(mv){var d=20*Math.log10(parseFloat(mv)/1000);return isFinite(d)?d:null}
var ws=null;function boot(){L.length=0;fetch('/pkt.json').then(function(r){return r.json()}).then(function(a){a.forEach(function(e){L.push({d:e.d,t:hm(e.ts),db:e.d=='RX'?dbv(e.mv):null,r:e.r})});for(var i=0;i<L.length;i++)if(L[i].db!==null){lv(L[i].db);break}draw();live()}).catch(function(){draw();live()})}boot();
function live(){if(stopped)return;ws=new WebSocket('ws://'+location.hostname+':81/ws');ws.onclose=function(){if(!stopped)setTimeout(boot,3000)};
ws.onmessage=function(e){var j;try{j=JSON.parse(e.data)}catch(x){return}if(parseInt(j.Active)!=1)return;var d=j.Dir=='TX'?'TX':'RX',db=null;
if(d=='RX'){db=20*Math.log10(parseFloat(j.mVrms)/1000);if(!isFinite(db))db=null;else lv(db)}
var raw='';try{raw=atob(j.RAW)}catch(x){}var t=new Date(Number(j.timeStamp)*1000),ts=t.getFullYear()<2020?'--:--':('0'+t.getHours()).slice(-2)+':'+('0'+t.getMinutes()).slice(-2)+':'+('0'+t.getSeconds()).slice(-2);
L.unshift({d:d,t:ts,db:db,r:raw});if(L.length>200)L.pop();draw()}}
window.pageStop=function(){stopped=true;try{if(ws)ws.close()}catch(e){}};
})();</script>
)HTML";

#define ROW(url, title, id) "<a href=\"#\" onclick=\"setting('" url "','" title "');return false\">" title "<span id=\"" id "\"></span>" CHEV "</a>\n"
static const char UI_MORE[] PROGMEM = "<h2 class=\"pg\">Settings</h2>\n"
	"<div class=\"gh\">Operation</div><div class=\"grp\">\n"
	ROW("/tracker", "Tracker", "m_trk")
	ROW("/radio", "Radio", "m_radio")
	ROW("/tnc2", "Radio level and TX test", "m_rt")
	ROW("/gnss", "GNSS", "m_gnss")
	ROW("/digi", "Digipeater", "m_digi")
	ROW("/igate", "IGate", "m_igate")
	ROW("/igate?part=filter", "IGate filter", "m_flt")
	"</div><div class=\"gh\">Connection</div><div class=\"grp\">\n"
	ROW("/wireless", "Wi-Fi and Bluetooth", "m_wifi")
	"</div><div class=\"gh\">Device</div><div class=\"grp\">\n"
	ROW("/system", "System and status", "m_sys")
	ROW("/mod", "Hardware", "m_hw")
	ROW("/storage", "Files", "m_files")
	ROW("/about", "About", "m_about")
	"</div>\n"
	"<script>(function(){function t(i,v){var e=document.getElementById(i);if(e)e.textContent=v}\n"
	"window.onStatus=function(s){t('m_trk',s.trk?('on'+(s.sb?' \\u00b7 SmartBeacon':'')):'off');t('m_digi',s.digi?'on':'off');t('m_igate',s.igate?'on':'off');"
	"t('m_gnss',s.gps.fix?('fix \\u00b7 '+s.gps.sat+' sat \\u00b7 HDOP '+s.gps.hdop):'no fix');"
	"t('m_wifi',s.wifi.cl+' AP client'+(s.wifi.cl==1?'':'s')+(s.wifi.sta?' \\u00b7 '+s.wifi.ssid:'')+(s.bt==2?' \\u00b7 BT app':(s.bt==1?' \\u00b7 BT on':'')));"
	"t('m_sys','up '+fup(s.up));t('m_about','v'+s.ver)};if(ST)window.onStatus(ST)})();</script>\n";

// Status tiles shown at the top of the System page (inserted by handle_system).
extern const char UI_SYSTILES[] PROGMEM = R"HTML(<div class="tiles" id="sysT"></div>
<p><a class="button" href="/diag">Diagnostics in detail &rarr;</a></p>
<script>(function(){function T(k,v,s){return '<div class="tile"><small>'+k+'</small><b>'+v+'</b><span>'+s+'</span></div>'}
window.onStatus=function(s){document.getElementById('sysT').innerHTML=
T('Uptime',fup(s.up),'')+T('Free memory',Math.round(s.heap/1024)+' kB','lowest '+Math.round(s.hmin/1024)+' kB \u00b7 PSRAM '+(s.ps/1048576).toFixed(1)+' MB')+
T('Wi-Fi',s.wifi.sta?'Connected':'Own AP',(s.wifi.sta?s.wifi.ssid+'<br>IP '+s.wifi.ip+'<br>':'')+s.wifi.cl+' client'+(s.wifi.cl==1?'':'s')+' on own AP (192.168.4.1)')+
T('Bluetooth',s.bt==2?'App connected':(s.bt==1?'Waiting':'Off'),'')+
T('GPS',s.gps.fix?'Fix':'No fix',s.gps.sat+' sat \u00b7 HDOP '+s.gps.hdop)+T('Firmware','v'+s.ver,'RX '+s.rx+' \u00b7 TX '+s.tx)};if(ST)window.onStatus(ST)})();</script>
)HTML";

static bool uiAuth(AsyncWebServerRequest *request)
{
	if (!request->authenticate(config.http_username, config.http_password))
	{
		request->requestAuthentication();
		return false;
	}
	return true;
}

static void uiSendP(AsyncWebServerRequest *request, const char *page)
{
	AsyncWebServerResponse *r = request->beginResponse_P(200, "text/html", (const uint8_t *)page, strlen_P(page));
	r->addHeader("Cache-Control", "no-cache");
	request->send(r);
}

void handle_ui_shell(AsyncWebServerRequest *request)
{
	if (uiAuth(request))
		uiSendP(request, UI_SHELL);
}

static void jsonStr(char *dst, size_t cap, const char *src)
{
	size_t o = 0;
	for (; *src && o + 2 < cap; src++)
	{
		unsigned char c = (unsigned char)*src;
		if (c == '"' || c == '\\')
		{
			dst[o++] = '\\';
			dst[o++] = c;
		}
		else if (c >= 32 && c < 127)
			dst[o++] = c;
	}
	dst[o] = 0;
}

static void handle_status_json(AsyncWebServerRequest *request)
{
	if (!uiAuth(request))
		return;
	char call[24], host[70], ssid[70], tm[8] = "";
	if (config.trk_en)
	{
		char c[20];
		if (config.trk_ssid)
			snprintf(c, sizeof(c), "%s-%d", config.trk_mycall, config.trk_ssid);
		else
			snprintf(c, sizeof(c), "%s", config.trk_mycall);
		jsonStr(call, sizeof(call), c);
	}
	else
		jsonStr(call, sizeof(call), config.host_name);
	jsonStr(host, sizeof(host), config.host_name);
	jsonStr(ssid, sizeof(ssid), WiFi.isConnected() ? WiFi.SSID().c_str() : "");
	time_t now = time(NULL);
	if (now > 1600000000)
	{
		struct tm lt;
		localtime_r(&now, &lt);
		snprintf(tm, sizeof(tm), "%02d:%02d", lt.tm_hour, lt.tm_min);
	}
	bool fix = gps.location.isValid() && gps.location.age() < 10000;
	long ltx = lastTxMillis ? (long)((millis() - lastTxMillis) / 1000) : -1;
	char js[640];
	snprintf(js, sizeof(js),
			 "{\"call\":\"%s\",\"host\":\"%s\",\"ver\":\"%s\",\"time\":\"%s\",\"up\":%lu,"
			 "\"gps\":{\"fix\":%d,\"sat\":%lu,\"hdop\":%.1f},\"bt\":%d,"
			 "\"wifi\":{\"cl\":%d,\"sta\":%d,\"ssid\":\"%s\",\"ip\":\"%s\"},"
			 "\"heap\":%lu,\"hmin\":%lu,\"ps\":%lu,\"ltx\":%ld,\"rx\":%lu,\"tx\":%lu,"
			 "\"trk\":%d,\"sb\":%d,\"digi\":%d,\"igate\":%d}",
			 call, host, FORK_VERSION, tm, (unsigned long)(millis() / 1000 - upTimeStamp),
			 fix ? 1 : 0, (unsigned long)gps.satellites.value(), gps.hdop.isValid() ? gps.hdop.hdop() : 0.0, btClientState(),
			 (int)WiFi.softAPgetStationNum(), WiFi.isConnected() ? 1 : 0, ssid, WiFi.isConnected() ? WiFi.localIP().toString().c_str() : "",
			 (unsigned long)ESP.getFreeHeap(), (unsigned long)ESP.getMinFreeHeap(), (unsigned long)ESP.getFreePsram(), ltx,
			 (unsigned long)status.rxCount, (unsigned long)status.txCount,
			 config.trk_en ? 1 : 0, config.trk_smartbeacon ? 1 : 0, config.digi_en ? 1 : 0, config.igate_en ? 1 : 0);
	AsyncWebServerResponse *r = request->beginResponse(200, "application/json", js);
	r->addHeader("Cache-Control", "no-cache");
	request->send(r);
}

static void handle_heard_json(AsyncWebServerRequest *request)
{
	if (!uiAuth(request))
		return;
	char *js = heardCacheDup();
	lastHeardTimeout = 0; // ask the loop task for a fresh list
	lastHeard_Flag = true;
	if (!js)
	{
		request->send(503, "text/plain", "Not ready");
		return;
	}
	AsyncWebServerResponse *r = beginBufferResponse(request, js);
	r->setContentType("application/json");
	r->addHeader("Cache-Control", "no-cache");
	request->send(r);
}

void uiForkRoutes(AsyncWebServer &srv)
{
	srv.on("/status.json", HTTP_GET, handle_status_json);
	srv.on("/heard.json", HTTP_GET, handle_heard_json);
	srv.on("/pkt.json", HTTP_GET, [](AsyncWebServerRequest *r) {
		if (!uiAuth(r))
			return;
		char *js = pktLogJson();
		if (!js)
		{
			r->send(500, "text/plain", "Out of memory");
			return;
		}
		AsyncWebServerResponse *resp = beginBufferResponse(r, js);
		resp->setContentType("application/json");
		resp->addHeader("Cache-Control", "no-cache");
		r->send(resp);
	});
	srv.on("/heard", HTTP_GET, [](AsyncWebServerRequest *r) { if (uiAuth(r)) uiSendP(r, UI_HEARD); });
	srv.on("/radar", HTTP_GET, [](AsyncWebServerRequest *r) { if (uiAuth(r)) uiSendP(r, UI_RADAR); });
	srv.on("/pkt", HTTP_GET, [](AsyncWebServerRequest *r) { if (uiAuth(r)) uiSendP(r, UI_PKT); });
	srv.on("/more", HTTP_GET, [](AsyncWebServerRequest *r) { if (uiAuth(r)) uiSendP(r, UI_MORE); });
}
