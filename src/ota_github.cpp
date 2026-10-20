/*
 SM0TGY fork of ESP32APRS_Audio (GPLv3): firmware update from the fork's GitHub repository.

 The tracker reads a small manifest over HTTPS from raw.githubusercontent.com
 (branch fw-ota of github.com/tobbjo71/ESP32APRS_Audio, plain HTTP/1.1 GET:
 HTTPClient would pull strptime/mktime into IRAM, which is full) and, when the user confirms,
 downloads the matching _ota.bin into the other OTA slot, checks its MD5 and restarts.
 Server certificates are checked against an embedded Mozilla root bundle
 (data/cert/x509_crt_bundle). Only builds of this fork are offered; upstream
 firmware is never installed. The work runs in its own task; the web page polls
 /ota/status.
*/
#include <Arduino.h>
#ifdef FORK_OTA_GITHUB
#include "webservice.h"
#include <NetworkClientSecure.h>
#include <Update.h>

#if defined(BT_BLE)
#define FORK_OTA_VARIANT "ble"
#elif defined(BLUETOOTH)
#define FORK_OTA_VARIANT "bt"
#else
#define FORK_OTA_VARIANT "nobt"
#endif
#define FORK_OTA_HOST "raw.githubusercontent.com"
#define FORK_OTA_PATH "/tobbjo71/ESP32APRS_Audio/fw-ota/"

extern const uint8_t x509_crt_bundle_start[] asm("_binary_data_cert_x509_crt_bundle_start");
extern const uint8_t x509_crt_bundle_end[] asm("_binary_data_cert_x509_crt_bundle_end");

enum OtaState { OTA_IDLE, OTA_CHECKING, OTA_AVAILABLE, OTA_UPTODATE, OTA_DOWNLOADING, OTA_DONE, OTA_ERROR };
static volatile OtaState otaState = OTA_IDLE;
static volatile int otaPct = 0;
static char otaLatest[16] = "";
static char otaFile[64] = "";
static char otaMd5[40] = "";
static uint32_t otaSize = 0;
static char otaErr[96] = "";
static TaskHandle_t otaTask = NULL;

static int verNum(const char *v)
{
	int a = 0, b = 0;
	sscanf(v, "%d.%d", &a, &b);
	return a * 1000 + b;
}

static void otaFail(const char *msg)
{
	strlcpy(otaErr, msg, sizeof(otaErr));
	otaState = OTA_ERROR;
}

// Tiny extraction of "key":"value" or "key":number from the manifest.
static bool jsonGet(const String &js, const char *key, char *out, size_t cap)
{
	String k = String("\"") + key + "\"";
	int i = js.indexOf(k);
	if (i < 0)
		return false;
	i = js.indexOf(':', i + k.length());
	if (i < 0)
		return false;
	i++;
	while (i < (int)js.length() && (js[i] == ' ' || js[i] == '"'))
		i++;
	int e = i;
	while (e < (int)js.length() && js[e] != '"' && js[e] != ',' && js[e] != '}' && js[e] != ' ')
		e++;
	String v = js.substring(i, e);
	strlcpy(out, v.c_str(), cap);
	return v.length() > 0;
}


// Minimal HTTPS GET: returns status code (or <0), fills content length. Headers are consumed.
static int httpsGet(NetworkClientSecure *cli, const char *path, long *clen)
{
	*clen = -1;
	if (!cli->connect(FORK_OTA_HOST, 443))
		return -1;
	cli->printf("GET %s HTTP/1.1\r\nHost: " FORK_OTA_HOST "\r\nUser-Agent: SM0TGY-APRSTracker/" FORK_VERSION "\r\nConnection: close\r\n\r\n", path);
	cli->setTimeout(15);
	String line = cli->readStringUntil('\n');
	int code = -2;
	if (line.startsWith("HTTP/1."))
		code = line.substring(9, 12).toInt();
	while (cli->connected() || cli->available())
	{
		line = cli->readStringUntil('\n');
		line.trim();
		if (line.length() == 0)
			break;
		if (line.substring(0, 15).equalsIgnoreCase("Content-Length:"))
			*clen = line.substring(15).toInt();
	}
	return code;
}

static void otaCheckTask(void *)
{
	NetworkClientSecure *cli = new NetworkClientSecure();
	cli->setCACertBundle(x509_crt_bundle_start, x509_crt_bundle_end - x509_crt_bundle_start);
	long clen;
	int code = httpsGet(cli, FORK_OTA_PATH "manifest_" FORK_OTA_VARIANT ".json", &clen);
	if (code != 200)
	{
		char m[64];
		snprintf(m, sizeof(m), code < 0 ? "No connection to GitHub (%d)" : "GitHub answered %d", code);
		otaFail(m);
	}
	else
	{
		String js = cli->readString();
		char sz[16] = "";
		if (!jsonGet(js, "version", otaLatest, sizeof(otaLatest)) || !jsonGet(js, "file", otaFile, sizeof(otaFile)) ||
			!jsonGet(js, "md5", otaMd5, sizeof(otaMd5)) || !jsonGet(js, "size", sz, sizeof(sz)) || strlen(otaMd5) != 32)
			otaFail("Unreadable manifest");
		else
		{
			otaSize = strtoul(sz, NULL, 10);
			otaState = (verNum(otaLatest) > verNum(FORK_VERSION)) ? OTA_AVAILABLE : OTA_UPTODATE;
		}
	}
	cli->stop();
	delete cli;
	otaTask = NULL;
	vTaskDelete(NULL);
}

static void otaInstallTask(void *)
{
	NetworkClientSecure *cli = new NetworkClientSecure();
	cli->setCACertBundle(x509_crt_bundle_start, x509_crt_bundle_end - x509_crt_bundle_start);
	char path[128];
	snprintf(path, sizeof(path), FORK_OTA_PATH "%s", otaFile);
	bool ok = false;
	long len;
	int code = httpsGet(cli, path, &len);
	if (code != 200)
	{
		char m[64];
		snprintf(m, sizeof(m), "Download failed (%d)", code);
		otaFail(m);
	}
	else if (len <= 0 || (otaSize && (uint32_t)len != otaSize))
		otaFail("Unexpected file size");
	else if (!Update.begin(len))
		otaFail(Update.errorString());
	else
	{
		Update.setMD5(otaMd5);
		uint8_t *buf = (uint8_t *)malloc(4096);
		long got = 0;
		unsigned long last = millis();
		while (buf && got < len && millis() - last < 20000)
		{
			int av = cli->available();
			if (av > 0)
			{
				int n = cli->read(buf, av > 4096 ? 4096 : av);
				if (n > 0)
				{
					if (Update.write(buf, n) != (size_t)n)
						break;
					got += n;
					otaPct = (int)((int64_t)got * 100 / len);
					last = millis();
				}
			}
			else if (!cli->connected())
				break;
			else
				vTaskDelay(5 / portTICK_PERIOD_MS);
		}
		free(buf);
		if (got != len)
		{
			Update.abort();
			otaFail("Download interrupted");
		}
		else if (!Update.end(true))
			otaFail(Update.errorString()); // includes MD5 mismatch
		else
			ok = true;
	}
	cli->stop();
	delete cli;
	if (ok)
	{
		otaState = OTA_DONE;
		vTaskDelay(1500 / portTICK_PERIOD_MS);
		esp_restart();
	}
	otaTask = NULL;
	vTaskDelete(NULL);
}

static bool otaAuth(AsyncWebServerRequest *r)
{
	if (!r->authenticate(config.http_username, config.http_password))
	{
		r->requestAuthentication();
		return false;
	}
	return true;
}

static void otaStatus(AsyncWebServerRequest *r)
{
	static const char *names[] = {"idle", "checking", "available", "uptodate", "downloading", "done", "error"};
	char js[320];
	snprintf(js, sizeof(js), "{\"state\":\"%s\",\"cur\":\"%s\",\"latest\":\"%s\",\"size\":%lu,\"pct\":%d,\"err\":\"%s\",\"net\":%d}",
			 names[otaState], FORK_VERSION, otaLatest, (unsigned long)otaSize, otaPct, otaErr, WiFi.isConnected() ? 1 : 0);
	AsyncWebServerResponse *resp = r->beginResponse(200, "application/json", js);
	resp->addHeader("Cache-Control", "no-cache");
	r->send(resp);
}

void otaGithubRoutes(AsyncWebServer &srv)
{
	srv.on("/ota/status", HTTP_GET, [](AsyncWebServerRequest *r) { if (otaAuth(r)) otaStatus(r); });
	srv.on("/ota/check", HTTP_POST, [](AsyncWebServerRequest *r) {
		if (!otaAuth(r))
			return;
		if (otaTask == NULL && otaState != OTA_DOWNLOADING && otaState != OTA_DONE)
		{
			if (!WiFi.isConnected())
				otaFail("No internet: connect the Wi-Fi client (e.g. phone hotspot) first");
			else
			{
				otaErr[0] = 0;
				otaState = OTA_CHECKING;
				xTaskCreate(otaCheckTask, "otaCheck", 10240, NULL, 2, &otaTask);
			}
		}
		otaStatus(r);
	});
	srv.on("/ota/install", HTTP_POST, [](AsyncWebServerRequest *r) {
		if (!otaAuth(r))
			return;
		if (otaTask == NULL && otaState == OTA_AVAILABLE)
		{
			otaPct = 0;
			otaState = OTA_DOWNLOADING;
			xTaskCreate(otaInstallTask, "otaInstall", 10240, NULL, 2, &otaTask);
		}
		otaStatus(r);
	});
}

// Card shown at the top of the About page.
extern const char UI_OTA[] PROGMEM = R"HTML(<div class="tile" style="margin-bottom:12px"><small>Update from GitHub</small>
<b id="otaT">Running v)HTML" FORK_VERSION R"HTML(</b><span id="otaS">Needs internet through the Wi-Fi client (e.g. phone hotspot).</span>
<div class="lvl" id="otaBarW" style="display:none;margin-top:6px"><div style="height:10px"><b id="otaBar" style="height:10px;width:0"></b></div></div>
<p style="margin:8px 0 0"><button class="button" type="button" id="otaBtn" onclick="otaGo()">Check for update</button></p></div>
<script>(function(){var mode='check',T=null;
function set(t,s){document.getElementById('otaT').textContent=t;document.getElementById('otaS').textContent=s}
function btn(t,m,show){var b=document.getElementById('otaBtn');b.textContent=t;b.style.display=show?'':'none';mode=m}
function poll(){fetch('/ota/status').then(function(r){return r.json()}).then(function(j){
if(j.state=='checking'){set('Checking…','Asking GitHub for the latest build');btn('',mode,false);T=setTimeout(poll,1000);return}
if(j.state=='available'){set('v'+j.latest+' available','You run v'+j.cur+' · '+(j.size/1048576).toFixed(1)+' MB · settings are kept');btn('Install v'+j.latest,'install',true);return}
if(j.state=='uptodate'){set('Up to date','v'+j.cur+' is the latest build');btn('Check again','check',true);return}
if(j.state=='downloading'){document.getElementById('otaBarW').style.display='';document.getElementById('otaBar').style.width=j.pct+'%';set('Installing v'+j.latest+' … '+j.pct+' %','Do not power off. The tracker restarts by itself.');btn('',mode,false);T=setTimeout(poll,1000);return}
if(j.state=='done'){set('Installed – restarting','The page reloads in about 20 s');btn('',mode,false);setTimeout(function(){location.reload()},20000);return}
if(j.state=='error'){set('Update failed',j.err);btn('Try again','check',true);return}
}).catch(function(){T=setTimeout(poll,2000)})}
window.otaGo=function(){if(mode=='install'&&!confirm('Install the new firmware now? The tracker restarts when done.'))return;
fetch(mode=='install'?'/ota/install':'/ota/check',{method:'POST'}).then(function(){poll()})};
window.pageStop=function(){clearTimeout(T)};poll();})();</script>
)HTML";
#endif // FORK_OTA_GITHUB
