#include "wifi_manager.h"

#include <WiFi.h>

#include "board.h"

static bool s_apActive = false;

void wifiInit()
{
    WiFi.persistent(false);
    WiFi.mode(WIFI_AP_STA);
    WiFi.setAutoReconnect(true);
    /* 略降发射功率，减发热（仍可正常连家用路由） */
    WiFi.setTxPower(WIFI_POWER_8_5dBm);
}

void wifiStartAp()
{
    if (wifiIsStaConnected()) {
        WiFi.mode(WIFI_AP_STA);
    } else {
        WiFi.mode(WIFI_AP);
    }
    WiFi.softAP(AP_SSID, AP_PASSWORD);
    s_apActive = true;
    Serial.printf("[wifi] SoftAP %s  IP=%s\n", AP_SSID, WiFi.softAPIP().toString().c_str());
}

void wifiStopAp()
{
    wifi_mode_t mode = WiFi.getMode();
    if (!s_apActive && mode != WIFI_AP && mode != WIFI_AP_STA) {
        return;
    }
    WiFi.softAPdisconnect(true);
    s_apActive = false;
    if (wifiIsStaConnected()) {
        WiFi.mode(WIFI_STA);
        Serial.println("[wifi] SoftAP off -> STA only (lower power)");
    } else {
        /* 未连 STA 时保留 AP，避免配网断掉 */
        wifiStartAp();
    }
}

void wifiEnsureAp()
{
    wifi_mode_t mode = WiFi.getMode();
    if (s_apActive && (mode == WIFI_AP || mode == WIFI_AP_STA)) {
        return;
    }
    wifiStartAp();
}

bool wifiIsApActive()
{
    wifi_mode_t mode = WiFi.getMode();
    return s_apActive && (mode == WIFI_AP || mode == WIFI_AP_STA);
}

bool wifiConnectSta(const AppConfig &cfg, uint32_t timeoutMs)
{
    if (cfg.wifiSsid.isEmpty()) {
        return false;
    }

    if (s_apActive) {
        WiFi.mode(WIFI_AP_STA);
    } else {
        WiFi.mode(WIFI_STA);
    }
    WiFi.begin(cfg.wifiSsid.c_str(), cfg.wifiPassword.c_str());
    Serial.printf("[wifi] Connecting STA to %s\n", cfg.wifiSsid.c_str());

    uint32_t start = millis();
    while (millis() - start < timeoutMs) {
        if (WiFi.status() == WL_CONNECTED) {
            Serial.printf("[wifi] STA OK %s\n", WiFi.localIP().toString().c_str());
            WiFi.setTxPower(WIFI_POWER_8_5dBm);
            return true;
        }
        delay(200);
    }
    Serial.println("[wifi] STA connect timeout");
    return false;
}

bool wifiIsStaConnected()
{
    return WiFi.status() == WL_CONNECTED;
}

int wifiScan(String *ssids, int8_t *rssi, int maxCount)
{
    wifiEnsureAp(); /* 扫描时保证射频开着 */
    int n = WiFi.scanNetworks(false, false);
    if (n < 0) {
        return 0;
    }
    if (n > maxCount) {
        n = maxCount;
    }
    for (int i = 0; i < n; ++i) {
        ssids[i] = WiFi.SSID(i);
        rssi[i] = WiFi.RSSI(i);
    }
    WiFi.scanDelete();
    return n;
}
