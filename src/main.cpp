#include <Arduino.h>
#include <WiFi.h>

#include "app_config.h"
#include "board.h"
#include "http_util.h"
#include "services.h"
#include "ui.h"
#include "web_server.h"
#include "wifi_manager.h"

static AppConfig g_cfg;
static UiNav g_nav;
static bool g_wifiOk = false;
static bool g_timeOk = false;

static void onWifiSaved(const AppConfig &cfg)
{
    Serial.println("[app] WiFi saved, connecting STA...");
    g_wifiOk = wifiConnectSta(cfg, 20000);
    if (g_wifiOk) {
        timeServiceInit();
        g_timeOk = timeServiceSynced();
        weatherRefresh();
        stockRefreshAll(g_cfg);
        uiMarkFullRedraw();
        uiRenderFull(g_nav, g_wifiOk, g_cfg);
        Serial.printf("[app] STA OK %s\n", WiFi.localIP().toString().c_str());
    } else {
        Serial.println("[app] STA failed, SoftAP still up");
        uiMarkFullRedraw();
        uiRenderFull(g_nav, false, g_cfg);
    }
}

void setup()
{
    displayInit();
    delay(100);
    Serial.println("\n=== M5StickS3 Stock Clock ===");

    appConfigLoad(g_cfg);
    uiNavInit(g_nav);

    wifiInit();
    wifiStartAp();
    webServerBegin(g_cfg);
    webServerSetWifiSavedCallback(onWifiSaved);

    String apIp = WiFi.softAPIP().toString();
    Serial.printf("[app] SoftAP %s / %s -> http://%s\n", AP_SSID, AP_PASSWORD, apIp.c_str());
    Serial.printf("[app] poll quote=%lus list=%lus weather=%lus kline=%lus\n",
                  (unsigned long)(POLL_STOCK_QUOTE_MS / 1000UL), (unsigned long)(POLL_STOCK_LIST_MS / 1000UL),
                  (unsigned long)(POLL_WEATHER_MS / 1000UL), (unsigned long)(POLL_KLINE_MS / 1000UL));

    uiRenderFull(g_nav, false, g_cfg);

    if (g_cfg.configured) {
        Serial.printf("[app] try saved WiFi: %s\n", g_cfg.wifiSsid.c_str());
        g_wifiOk = wifiConnectSta(g_cfg, 15000);
        if (g_wifiOk) {
            timeServiceInit();
            g_timeOk = timeServiceSynced();
            weatherRefresh();
            stockRefreshAll(g_cfg);
            uiMarkFullRedraw();
            uiRenderFull(g_nav, g_wifiOk, g_cfg);
        } else {
            Serial.println("[app] saved WiFi failed, keep SoftAP");
        }
    }
}

void loop()
{
    static uint32_t lastWeather = 0;
    static uint32_t lastStockList = 0;
    static uint32_t lastStockQuote = 0;
    static uint32_t lastClockTick = 0;
    static uint8_t lastStockCount = 0;
    static uint8_t lastStockIndex = 255;
    static uint8_t lastStockView = 255;

    webServerLoop();

    bool dirty = false;

    if (g_cfg.stockCount != lastStockCount) {
        lastStockCount = g_cfg.stockCount;
        if (g_nav.stockIndex >= g_cfg.stockCount) {
            g_nav.stockIndex = 0;
        }
        if (wifiIsStaConnected() && !httpIsBackingOff()) {
            stockRefreshAll(g_cfg);
        }
        uiMarkFullRedraw();
        dirty = true;
    }

    ButtonEvent evt = buttonsPoll();
    if (evt != BTN_NONE) {
        UiScreen prevScreen = g_nav.screen;
        uint8_t prevIdx = g_nav.stockIndex;
        if (uiNavHandle(g_nav, evt, g_cfg)) {
            dirty = true;
            if (g_nav.screen == SCREEN_STOCK &&
                (prevScreen != SCREEN_STOCK || prevIdx != g_nav.stockIndex)) {
                lastStockQuote = 0; /* 换股尽快刷一次简行情 */
            }
        }
    }

    bool nowWifi = wifiIsStaConnected();
    if (nowWifi != g_wifiOk) {
        g_wifiOk = nowWifi;
        dirty = true;
        uiMarkFullRedraw();
        if (g_wifiOk && !g_timeOk) {
            timeServiceInit();
            g_timeOk = timeServiceSynced();
            if (!httpIsBackingOff()) {
                weatherRefresh();
                stockRefreshAll(g_cfg);
                lastWeather = millis();
                lastStockList = millis();
            }
        }
    }
    if (!g_timeOk && timeServiceSynced()) {
        g_timeOk = true;
        dirty = true;
        uiMarkFullRedraw();
    }

    if (!g_wifiOk || httpIsBackingOff()) {
        if (dirty) {
            uiRenderFull(g_nav, g_wifiOk, g_cfg);
        } else if (g_nav.screen == SCREEN_CLOCK && (millis() - lastClockTick > 200)) {
            uiRenderClockTick(g_wifiOk);
            lastClockTick = millis();
        }
        delay(20);
        return;
    }

    if (millis() - lastWeather > POLL_WEATHER_MS) {
        weatherRefresh();
        lastWeather = millis();
        if (g_nav.screen == SCREEN_CLOCK) {
            uiRenderClockMeta(g_wifiOk);
        }
    }

    if (g_nav.screen == SCREEN_STOCK && g_cfg.stockCount > 0) {
        uint8_t idx = g_nav.stockIndex % g_cfg.stockCount;
        bool stockChanged = (idx != lastStockIndex);
        bool viewChanged = (g_nav.stockView != lastStockView);

        /* 行情页：只请求当前一只简行情，默认 15s；换股时 enrich；缺中文名由 services 自动补东财 */
        if (millis() - lastStockQuote > POLL_STOCK_QUOTE_MS) {
            if (stockRefreshIndex(g_cfg, idx, stockChanged)) {
                if (g_nav.stockView == STOCK_VIEW_QUOTE && !dirty) {
                    uiRenderStockQuoteTick(g_nav, g_cfg);
                } else {
                    dirty = true;
                }
            }
            lastStockQuote = millis();
        }

        /* K 线：换视图/换股或缓存过期才请求；有缓存则跳过 */
        if (g_nav.stockView == STOCK_VIEW_KLINE) {
            const String &sym = g_cfg.stocks[idx];
            bool needK = stockChanged || viewChanged || stockKlineCount() == 0 || stockKlineSymbol() != sym ||
                         stockKlineAgeMs() >= POLL_KLINE_MS;
            if (needK && uiEnsureStockKline(g_nav, g_cfg)) {
                dirty = true;
            }
        }

        lastStockIndex = idx;
        lastStockView = g_nav.stockView;
    } else {
        /* 非行情页：低频刷全列表，不跟天气绑在一起狂打 */
        if (millis() - lastStockList > POLL_STOCK_LIST_MS) {
            stockRefreshAll(g_cfg);
            lastStockList = millis();
        }
        lastStockView = g_nav.stockView;
    }

    if (dirty) {
        uiRenderFull(g_nav, g_wifiOk, g_cfg);
        lastClockTick = millis();
    } else if (g_nav.screen == SCREEN_CLOCK) {
        if (millis() - lastClockTick > 200) {
            uiRenderClockTick(g_wifiOk);
            lastClockTick = millis();
        }
    }

    delay(20);
}
