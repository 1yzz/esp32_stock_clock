#include "wifi_manager.h"

#include <WiFi.h>

#include "board.h"

void wifiInit()
{
    WiFi.persistent(false);
    WiFi.mode(WIFI_AP_STA);
    WiFi.setAutoReconnect(true);
}

void wifiStartAp()
{
    WiFi.softAP(AP_SSID, AP_PASSWORD);
    Serial.printf("[wifi] SoftAP %s  IP=%s\n", AP_SSID, WiFi.softAPIP().toString().c_str());
}

bool wifiConnectSta(const AppConfig &cfg, uint32_t timeoutMs)
{
    if (cfg.wifiSsid.isEmpty()) {
        return false;
    }

    WiFi.mode(WIFI_AP_STA);
    WiFi.begin(cfg.wifiSsid.c_str(), cfg.wifiPassword.c_str());
    Serial.printf("[wifi] Connecting STA to %s\n", cfg.wifiSsid.c_str());

    uint32_t start = millis();
    while (millis() - start < timeoutMs) {
        if (WiFi.status() == WL_CONNECTED) {
            Serial.printf("[wifi] STA OK %s\n", WiFi.localIP().toString().c_str());
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
