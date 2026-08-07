#include "app_config.h"

#include <Preferences.h>

#include "board.h"
#include "services.h"

static void setDefaultStocks(AppConfig &cfg)
{
    cfg.stocks[0] = "s_sh000001";
    cfg.stocks[1] = "s_usNDX";
    cfg.stocks[2] = "s_usAAPL";
    cfg.stockCount = 3;
}

void appConfigClampTtl(AppConfig &cfg)
{
    if (cfg.ttlKlineTodaySec < 15) {
        cfg.ttlKlineTodaySec = 15;
    }
    if (cfg.ttlKlineTodaySec > 3600) {
        cfg.ttlKlineTodaySec = 3600;
    }
    if (cfg.ttlKlineMidSec < 60) {
        cfg.ttlKlineMidSec = 60;
    }
    if (cfg.ttlKlineMidSec > 86400) {
        cfg.ttlKlineMidSec = 86400;
    }
    if (cfg.ttlKlineDaySec < 60) {
        cfg.ttlKlineDaySec = 60;
    }
    if (cfg.ttlKlineDaySec > 86400) {
        cfg.ttlKlineDaySec = 86400;
    }
}

void appConfigApplyRuntime(const AppConfig &cfg)
{
    stockKlineSetTtlMs((uint32_t)cfg.ttlKlineTodaySec * 1000UL, (uint32_t)cfg.ttlKlineMidSec * 1000UL,
                       (uint32_t)cfg.ttlKlineDaySec * 1000UL);
}

void appConfigSetDefaults(AppConfig &cfg)
{
    cfg.wifiSsid = "";
    cfg.wifiPassword = "";
    cfg.configured = false;
    cfg.ttlKlineTodaySec = POLL_KLINE_MS / 1000UL;
    cfg.ttlKlineMidSec = POLL_KLINE_MID_MS / 1000UL;
    cfg.ttlKlineDaySec = POLL_KLINE_DAY_MS / 1000UL;
    setDefaultStocks(cfg);
}

void appConfigLoad(AppConfig &cfg)
{
    appConfigSetDefaults(cfg);
    Preferences prefs;
    if (!prefs.begin("stock_clock", true)) {
        appConfigApplyRuntime(cfg);
        return;
    }

    cfg.wifiSsid = prefs.getString("wifi_ssid", "");
    cfg.wifiPassword = prefs.getString("wifi_password", "");
    cfg.configured = cfg.wifiSsid.length() > 0;

    cfg.ttlKlineTodaySec = prefs.getUInt("ttl_kt", cfg.ttlKlineTodaySec);
    cfg.ttlKlineMidSec = prefs.getUInt("ttl_km", cfg.ttlKlineMidSec);
    cfg.ttlKlineDaySec = prefs.getUInt("ttl_kd", cfg.ttlKlineDaySec);
    appConfigClampTtl(cfg);

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
    appConfigApplyRuntime(cfg);
}

bool appConfigSave(AppConfig &cfg)
{
    appConfigClampTtl(cfg);

    Preferences prefs;
    if (!prefs.begin("stock_clock", false)) {
        return false;
    }
    prefs.putString("wifi_ssid", cfg.wifiSsid);
    prefs.putString("wifi_password", cfg.wifiPassword);
    prefs.putUInt("ttl_kt", cfg.ttlKlineTodaySec);
    prefs.putUInt("ttl_km", cfg.ttlKlineMidSec);
    prefs.putUInt("ttl_kd", cfg.ttlKlineDaySec);
    prefs.putUChar("stock_n", cfg.stockCount);
    for (uint8_t i = 0; i < cfg.stockCount; ++i) {
        char key[12];
        snprintf(key, sizeof(key), "stk%u", (unsigned)i);
        prefs.putString(key, cfg.stocks[i]);
    }
    prefs.end();
    appConfigApplyRuntime(cfg);
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
