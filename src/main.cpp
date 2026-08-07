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
    wifiEnsureAp();
    g_wifiOk = wifiConnectSta(cfg, 20000);
    if (g_wifiOk) {
        timeServiceInit();
        g_timeOk = timeServiceSynced();
        weatherRefresh();
        stockRefreshAll(g_cfg);
        /* STA 正常后关 SoftAP，显著降功耗/发热；进「配置」页会再开 */
        if (g_nav.screen != SCREEN_SETTING) {
            wifiStopAp();
        }
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
#if defined(APP_CPU_MHZ)
    setCpuFrequencyMhz(APP_CPU_MHZ);
    Serial.printf("[app] CPU %u MHz\n", (unsigned)getCpuFrequencyMhz());
#endif
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
    Serial.printf("[app] poll quote=%lus list=%lus weather=%lus klineTTL today=%us mid=%us day=%us\n",
                  (unsigned long)(POLL_STOCK_QUOTE_MS / 1000UL), (unsigned long)(POLL_STOCK_LIST_MS / 1000UL),
                  (unsigned long)(POLL_WEATHER_MS / 1000UL), (unsigned)g_cfg.ttlKlineTodaySec,
                  (unsigned)g_cfg.ttlKlineMidSec, (unsigned)g_cfg.ttlKlineDaySec);
    Serial.printf("[app] brightness=%d idleDelay=%dms\n", DISPLAY_BRIGHTNESS, LOOP_IDLE_DELAY_MS);

    uiRenderFull(g_nav, false, g_cfg);

    if (g_cfg.configured) {
        Serial.printf("[app] try saved WiFi: %s\n", g_cfg.wifiSsid.c_str());
        g_wifiOk = wifiConnectSta(g_cfg, 15000);
        if (g_wifiOk) {
            timeServiceInit();
            g_timeOk = timeServiceSynced();
            weatherRefresh();
            stockRefreshAll(g_cfg);
            wifiStopAp(); /* 日常运行只用 STA */
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
    static UiScreen lastScreen = SCREEN_MENU;

    /* 进配置页开 SoftAP；离开且已连 STA 则关掉以省电 */
    if (g_nav.screen != lastScreen) {
        if (g_nav.screen == SCREEN_SETTING) {
            wifiEnsureAp();
            uiMarkFullRedraw();
        } else if (lastScreen == SCREEN_SETTING && wifiIsStaConnected()) {
            wifiStopAp();
        }
        lastScreen = g_nav.screen;
    }

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
        } else if (g_nav.screen == SCREEN_CLOCK && (millis() - lastClockTick > CLOCK_TICK_MS)) {
            uiRenderClockTick(g_wifiOk);
            lastClockTick = millis();
        }
        delay(LOOP_IDLE_DELAY_MS);
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

        /* K 线：各周期独立缓存；换周期仅在对应桶过期时联网 */
        if (stockViewIsKline(g_nav.stockView)) {
            const String &sym = g_cfg.stocks[idx];
            KlineRange kr = uiKlineRangeFromView(g_nav.stockView);
            bool needNet = stockKlineNeedsFetch(sym, kr);
            bool needShow = stockChanged || viewChanged || stockKlineCount() == 0 ||
                            stockKlineSymbol() != sym || stockKlineRange() != kr;
            if (needShow || needNet) {
                if (uiEnsureStockKline(g_nav, g_cfg)) {
                    dirty = true;
                } else if (needShow) {
                    dirty = true; /* 展示空态 */
                }
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
        if (millis() - lastClockTick > CLOCK_TICK_MS) {
            uiRenderClockTick(g_wifiOk);
            lastClockTick = millis();
        }
    }

    delay(LOOP_IDLE_DELAY_MS);
}
