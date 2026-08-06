#include "web_server.h"

#include <WebServer.h>
#include <WiFi.h>

#include "wifi_manager.h"

static WebServer server(80);
static AppConfig *s_cfg = nullptr;
static void (*s_onWifiSaved)(const AppConfig &) = nullptr;

static String htmlEscape(const String &in)
{
    String out;
    out.reserve(in.length() + 8);
    for (size_t i = 0; i < in.length(); ++i) {
        char c = in[i];
        if (c == '&') {
            out += F("&amp;");
        } else if (c == '<') {
            out += F("&lt;");
        } else if (c == '>') {
            out += F("&gt;");
        } else if (c == '"') {
            out += F("&quot;");
        } else {
            out += c;
        }
    }
    return out;
}

static void handleRoot()
{
    String html;
    html.reserve(4500);
    html += F(
        "<!doctype html><html><head><meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>配置</title>"
        "<style>"
        "body{font-family:sans-serif;padding:16px;max-width:520px;margin:auto;background:#f7f7f7}"
        "h2{margin:0 0 8px}"
        "section{background:#fff;border-radius:8px;padding:14px;margin:12px 0;box-shadow:0 1px 3px rgba(0,0,0,.08)}"
        "input,button,select{width:100%;margin:6px 0;padding:10px;box-sizing:border-box;font-size:16px}"
        "select#aplist{height:180px}"
        ".meta{color:#666;font-size:13px;margin:6px 0}"
        ".ssid{font-weight:600;padding:8px;background:#f2f2f2;border-radius:6px;min-height:1.2em}"
        "button.primary{background:#1a73e8;color:#fff;border:0;border-radius:6px}"
        "button.secondary{background:#eee;border:0;border-radius:6px}"
        "button.danger{background:#d93025;color:#fff;border:0;border-radius:6px;width:auto;padding:6px 12px}"
        "ul{list-style:none;padding:0;margin:0}"
        "li{display:flex;justify-content:space-between;align-items:center;padding:8px 0;border-bottom:1px solid #eee}"
        "code{background:#f0f0f0;padding:2px 6px;border-radius:4px}"
        "</style></head><body>");
    html += F("<h2>配置</h2><p class='meta'>状态: ");
    html += wifiIsStaConnected() ? F("已连接 STA") : F("SoftAP / 未连 STA");
    if (wifiIsStaConnected()) {
        html += F(" · ");
        html += WiFi.localIP().toString();
    }
    html += F("</p>");

    /* WiFi */
    html += F("<section><h3>WiFi</h3>"
              "<button type='button' class='secondary' onclick='scanWifi()'>扫描附近 WiFi</button>"
              "<p class='meta' id='scanHint'>点击扫描</p>"
              "<select id='aplist' size='8'></select>"
              "<p class='meta'>已选网络</p>"
              "<div class='ssid' id='ssidShow'>");
    html += s_cfg->wifiSsid.length() ? htmlEscape(s_cfg->wifiSsid) : F("（请从上方列表选择）");
    html += F("</div>"
              "<form method='post' action='/wifi' onsubmit='return beforeWifi()'>"
              "<input type='hidden' name='ssid' id='ssid' value='");
    html += htmlEscape(s_cfg->wifiSsid);
    html += F("'>"
              "<label>WiFi 密码</label>"
              "<input name='password' type='password' placeholder='输入密码（开放网络可留空）' "
              "autocomplete='current-password'>"
              "<button type='submit' class='primary'>保存并连接</button>"
              "</form></section>");

    /* Stocks */
    html += F("<section><h3>股票列表</h3>"
              "<p class='meta'>腾讯行情代码，如 <code>s_sh000001</code> / <code>s_usAAPL</code> / "
              "<code>sz000001</code></p><ul>");
    for (uint8_t i = 0; i < s_cfg->stockCount; ++i) {
        html += F("<li><code>");
        html += htmlEscape(s_cfg->stocks[i]);
        html += F("</code>");
        if (s_cfg->stockCount > 1) {
            html += F("<form method='post' action='/stock/del' style='margin:0'>"
                      "<input type='hidden' name='index' value='");
            html += String(i);
            html += F("'><button type='submit' class='danger'>删除</button></form>");
        }
        html += F("</li>");
    }
    html += F("</ul>"
              "<form method='post' action='/stock/add'>"
              "<input name='symbol' placeholder='添加代码，如 s_usTSLA' maxlength='24' required>"
              "<button type='submit' class='primary'>添加股票</button>"
              "</form></section>");

    html += F(
        "<p class='meta'>城市与天气由 IP 自动定位，无需配置。</p>"
        "<script>"
        "function pickAp(){"
        "  const s=document.getElementById('aplist');"
        "  if(!s.value) return;"
        "  document.getElementById('ssid').value=s.value;"
        "  document.getElementById('ssidShow').textContent=s.value;"
        "}"
        "async function scanWifi(){"
        "  const hint=document.getElementById('scanHint');"
        "  hint.textContent='扫描中...';"
        "  try{"
        "    const r=await fetch('/scan'); const list=await r.json();"
        "    const sel=document.getElementById('aplist');"
        "    sel.innerHTML='';"
        "    list.forEach(ap=>{"
        "      if(!ap.ssid) return;"
        "      const o=document.createElement('option');"
        "      o.value=ap.ssid;"
        "      o.textContent=ap.ssid+'  ('+ap.rssi+' dBm)';"
        "      sel.appendChild(o);"
        "    });"
        "    hint.textContent=list.length?('找到 '+list.length+' 个网络'):'未找到网络';"
        "    sel.onchange=pickAp; sel.onclick=pickAp;"
        "  }catch(e){ hint.textContent='扫描失败'; }"
        "}"
        "function beforeWifi(){"
        "  if(!document.getElementById('ssid').value){ alert('请先选择 WiFi'); return false; }"
        "  return true;"
        "}"
        "</script></body></html>");
    server.send(200, "text/html; charset=utf-8", html);
}

static void handleScan()
{
    String ssids[20];
    int8_t rssi[20];
    int n = wifiScan(ssids, rssi, 20);

    String body = "[";
    for (int i = 0; i < n; ++i) {
        if (ssids[i].length() == 0) {
            continue;
        }
        if (body.length() > 1) {
            body += ',';
        }
        String s = ssids[i];
        s.replace("\\", "\\\\");
        s.replace("\"", "\\\"");
        body += "{\"ssid\":\"";
        body += s;
        body += "\",\"rssi\":";
        body += String(rssi[i]);
        body += '}';
    }
    body += ']';
    server.send(200, "application/json", body);
}

static void sendOkRedirect(const char *msg)
{
    String html = F("<!doctype html><html><body style='font-family:sans-serif;padding:16px'>");
    html += F("<h2>");
    html += msg;
    html += F("</h2><p><a href='/'>返回配置</a></p></body></html>");
    server.send(200, "text/html; charset=utf-8", html);
}

static void handleWifi()
{
    if (server.hasArg("ssid") && server.arg("ssid").length() > 0) {
        s_cfg->wifiSsid = server.arg("ssid");
        s_cfg->wifiPassword = server.hasArg("password") ? server.arg("password") : "";
        s_cfg->configured = true;
        appConfigSave(*s_cfg);
        if (s_onWifiSaved) {
            s_onWifiSaved(*s_cfg);
        }
    }
    sendOkRedirect("WiFi 已保存，正在连接...");
}

static void handleStockAdd()
{
    if (server.hasArg("symbol")) {
        if (!appConfigAddStock(*s_cfg, server.arg("symbol"))) {
            sendOkRedirect("添加失败（重复/已满/空）");
            return;
        }
    }
    sendOkRedirect("已添加股票");
}

static void handleStockDel()
{
    if (server.hasArg("index")) {
        uint8_t idx = (uint8_t)server.arg("index").toInt();
        if (!appConfigRemoveStock(*s_cfg, idx)) {
            sendOkRedirect("删除失败（至少保留一只）");
            return;
        }
    }
    sendOkRedirect("已删除股票");
}

void webServerBegin(AppConfig &cfg)
{
    s_cfg = &cfg;
    s_onWifiSaved = nullptr;
    server.on("/", HTTP_GET, handleRoot);
    server.on("/scan", HTTP_GET, handleScan);
    server.on("/wifi", HTTP_POST, handleWifi);
    server.on("/stock/add", HTTP_POST, handleStockAdd);
    server.on("/stock/del", HTTP_POST, handleStockDel);
    /* 兼容旧路径 */
    server.on("/config", HTTP_POST, handleWifi);
    server.begin();
    Serial.println("[web] config server on :80");
}

void webServerSetWifiSavedCallback(void (*cb)(const AppConfig &))
{
    s_onWifiSaved = cb;
}

void webServerLoop()
{
    server.handleClient();
}
