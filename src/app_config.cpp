#include "app_config.h"

#include <Preferences.h>

static void setDefaultStocks(AppConfig &cfg)
{
    cfg.stocks[0] = "s_sh000001";
    cfg.stocks[1] = "s_usNDX";
    cfg.stocks[2] = "s_usAAPL";
    cfg.stockCount = 3;
}

void appConfigSetDefaults(AppConfig &cfg)
{
    cfg.wifiSsid = "";
    cfg.wifiPassword = "";
    cfg.configured = false;
    setDefaultStocks(cfg);
}

void appConfigLoad(AppConfig &cfg)
{
    appConfigSetDefaults(cfg);
    Preferences prefs;
    if (!prefs.begin("stock_clock", true)) {
        return;
    }

    cfg.wifiSsid = prefs.getString("wifi_ssid", "");
    cfg.wifiPassword = prefs.getString("wifi_password", "");
    cfg.configured = cfg.wifiSsid.length() > 0;

    uint8_t n = prefs.getUChar("stock_n", 0);
    if (n == 0 || n > MAX_STOCKS) {
        setDefaultStocks(cfg);
    } else {
        cfg.stockCount = n;
        for (uint8_t i = 0; i < n; ++i) {
            char key[12];
            snprintf(key, sizeof(key), "stk%u", (unsigned)i);
            cfg.stocks[i] = prefs.getString(key, "");
        }
        /* 过滤空项 */
        uint8_t w = 0;
        for (uint8_t i = 0; i < cfg.stockCount; ++i) {
            if (cfg.stocks[i].length()) {
                cfg.stocks[w++] = cfg.stocks[i];
            }
        }
        cfg.stockCount = w;
        if (cfg.stockCount == 0) {
            setDefaultStocks(cfg);
        }
    }
    prefs.end();
}

bool appConfigSave(const AppConfig &cfg)
{
    Preferences prefs;
    if (!prefs.begin("stock_clock", false)) {
        return false;
    }
    prefs.putString("wifi_ssid", cfg.wifiSsid);
    prefs.putString("wifi_password", cfg.wifiPassword);
    prefs.putUChar("stock_n", cfg.stockCount);
    for (uint8_t i = 0; i < cfg.stockCount; ++i) {
        char key[12];
        snprintf(key, sizeof(key), "stk%u", (unsigned)i);
        prefs.putString(key, cfg.stocks[i]);
    }
    prefs.end();
    return true;
}

bool appConfigAddStock(AppConfig &cfg, const String &symbol)
{
    String s = symbol;
    s.trim();
    if (s.length() == 0 || cfg.stockCount >= MAX_STOCKS) {
        return false;
    }
    for (uint8_t i = 0; i < cfg.stockCount; ++i) {
        if (cfg.stocks[i] == s) {
            return false;
        }
    }
    cfg.stocks[cfg.stockCount++] = s;
    return appConfigSave(cfg);
}

bool appConfigRemoveStock(AppConfig &cfg, uint8_t index)
{
    if (index >= cfg.stockCount || cfg.stockCount <= 1) {
        return false; /* 至少保留一只 */
    }
    for (uint8_t i = index; i + 1 < cfg.stockCount; ++i) {
        cfg.stocks[i] = cfg.stocks[i + 1];
    }
    cfg.stockCount--;
    return appConfigSave(cfg);
}
