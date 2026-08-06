#include "ui.h"

#include <M5Unified.h>
#include <WiFi.h>

#include "board.h"
#include "services.h"
#include "wifi_manager.h"

static constexpr int STATUS_H = 14;
static constexpr int TIME_TOP = STATUS_H + 2;
static constexpr int TIME_H = 40;
static constexpr uint32_t LONG_MS = 700;

static bool s_needFull = true;
static String s_lastTime;
static String s_lastDate;
static String s_lastWeatherLine;
static String s_lastCity;

/* 离屏画时间，再一次性 push，避免「先擦后画」闪烁 */
static M5Canvas s_timeSpr(&M5.Display);
static bool s_timeSprOk = false;

static bool ensureTimeSprite()
{
    if (s_timeSprOk) {
        return true;
    }
    s_timeSpr.setColorDepth(16);
    s_timeSprOk = s_timeSpr.createSprite(M5.Display.width(), TIME_H);
    return s_timeSprOk;
}

static void pushTimeString(const String &timeStr)
{
    const int wScr = M5.Display.width();
    if (ensureTimeSprite()) {
        s_timeSpr.fillSprite(TFT_BLACK);
        s_timeSpr.setFont(&fonts::Font0);
        s_timeSpr.setTextSize(4);
        s_timeSpr.setTextDatum(middle_center);
        s_timeSpr.setTextColor(TFT_WHITE);
        s_timeSpr.drawString(timeStr, s_timeSpr.width() / 2, s_timeSpr.height() / 2);
        s_timeSpr.pushSprite(0, TIME_TOP);
        return;
    }
    /* 无 Sprite 时：定宽不透明覆写（HH:MM:SS 长度固定） */
    M5.Display.setFont(&fonts::Font0);
    M5.Display.setTextSize(4);
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    M5.Display.setTextDatum(middle_center);
    M5.Display.drawString(timeStr, wScr / 2, TIME_TOP + TIME_H / 2);
    M5.Display.setTextDatum(top_left);
}

void uiMarkFullRedraw()
{
    s_needFull = true;
    s_lastTime = "";
    s_lastDate = "";
    s_lastWeatherLine = "";
    s_lastCity = "";
}

void displayInit()
{
    auto cfg = M5.config();
    cfg.serial_baudrate = 115200;
    cfg.clear_display = true;
    cfg.internal_imu = false;
    cfg.internal_mic = false;
    cfg.internal_spk = false;
    cfg.internal_rtc = false;
    M5.begin(cfg);

    M5.Display.setRotation(1);
    M5.Display.setBrightness(180);
    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.setTextWrap(false);
    uiMarkFullRedraw();
}

ButtonEvent buttonsPoll()
{
    static bool aDown = false, bDown = false;
    static bool aLongFired = false, bLongFired = false;
    static uint32_t aStart = 0, bStart = 0;

    M5.update();
    bool a = M5.BtnA.isPressed();
    bool b = M5.BtnB.isPressed();
    uint32_t now = millis();

    if (a && !aDown) {
        aDown = true;
        aLongFired = false;
        aStart = now;
    }
    if (b && !bDown) {
        bDown = true;
        bLongFired = false;
        bStart = now;
    }

    if (a && aDown && !aLongFired && (now - aStart) >= LONG_MS) {
        aLongFired = true;
        return BTN_A_LONG;
    }
    if (b && bDown && !bLongFired && (now - bStart) >= LONG_MS) {
        bLongFired = true;
        return BTN_B_LONG;
    }

    if (!a && aDown) {
        aDown = false;
        if (!aLongFired) {
            return BTN_A_SHORT;
        }
    }
    if (!b && bDown) {
        bDown = false;
        if (!bLongFired) {
            return BTN_B_SHORT;
        }
    }
    return BTN_NONE;
}

/* 首页菜单表：以后加功能只往这里加一行 */
struct MenuItem {
    const char *titleZh;
    UiScreen target;
};

static const MenuItem kMenuItems[] = {
    {"时钟", SCREEN_CLOCK},
    {"行情", SCREEN_STOCK},
    {"配置", SCREEN_SETTING},
};
static constexpr uint8_t kMenuCount = sizeof(kMenuItems) / sizeof(kMenuItems[0]);

uint8_t uiMenuCount()
{
    return kMenuCount;
}

static void enterMenuItem(UiNav &nav, AppConfig &cfg)
{
    if (nav.menuIndex >= kMenuCount) {
        nav.menuIndex = 0;
    }
    UiScreen target = kMenuItems[nav.menuIndex].target;
    nav.screen = target;
    if (target == SCREEN_STOCK) {
        if (cfg.stockCount == 0 || nav.stockIndex >= cfg.stockCount) {
            nav.stockIndex = 0;
        }
        nav.stockView = STOCK_VIEW_QUOTE;
    }
}

void uiNavInit(UiNav &nav)
{
    nav.screen = SCREEN_MENU;
    nav.menuIndex = 0;
    nav.stockIndex = 0;
    nav.stockView = STOCK_VIEW_QUOTE;
    uiMarkFullRedraw();
}

bool uiNavHandle(UiNav &nav, ButtonEvent evt, AppConfig &cfg)
{
    if (evt == BTN_NONE) {
        return false;
    }

    if (nav.screen != SCREEN_MENU && (evt == BTN_A_LONG || evt == BTN_B_LONG)) {
        nav.screen = SCREEN_MENU;
        uiMarkFullRedraw();
        return true;
    }

    if (nav.screen == SCREEN_MENU) {
        if (evt == BTN_B_SHORT) {
            nav.menuIndex = (uint8_t)((nav.menuIndex + 1) % kMenuCount);
            uiMarkFullRedraw();
            return true;
        }
        if (evt == BTN_A_SHORT) {
            enterMenuItem(nav, cfg);
            uiMarkFullRedraw();
            return true;
        }
        return false;
    }

    if (nav.screen == SCREEN_STOCK) {
        if (evt == BTN_A_SHORT) {
            if (cfg.stockCount > 0) {
                nav.stockIndex = (nav.stockIndex + 1) % cfg.stockCount;
            }
            uiMarkFullRedraw();
            return true;
        }
        if (evt == BTN_B_SHORT) {
            nav.stockView = (nav.stockView + 1) % STOCK_VIEW_COUNT;
            uiMarkFullRedraw();
            return true;
        }
    }

    return false;
}

static void drawStatusBar(bool wifiOk, const char *titleZh)
{
    const int w = M5.Display.width();
    M5.Display.fillRect(0, 0, w, STATUS_H, TFT_NAVY);
    M5.Display.setTextSize(1);

    M5.Display.setFont(&fonts::Font0);
    M5.Display.setCursor(3, 3);
    if (wifiOk) {
        M5.Display.setTextColor(TFT_GREENYELLOW, TFT_NAVY);
        M5.Display.printf("W%d", WiFi.RSSI());
    } else {
        M5.Display.setTextColor(TFT_ORANGE, TFT_NAVY);
        M5.Display.print("AP");
    }

    M5.Display.setFont(&fonts::efontCN_12);
    M5.Display.setTextColor(TFT_CYAN, TFT_NAVY);
    M5.Display.setTextDatum(top_center);
    M5.Display.drawString(titleZh, w / 2, 1);
    M5.Display.setTextDatum(top_left);

    M5.Display.setFont(&fonts::Font0);
    int bat = M5.Power.getBatteryLevel();
    if (bat < 0) {
        bat = 0;
    }
    if (bat > 100) {
        bat = 100;
    }
    uint16_t c = bat <= 20 ? TFT_RED : (bat <= 40 ? TFT_YELLOW : TFT_GREENYELLOW);
    M5.Display.setTextColor(c, TFT_NAVY);
    char batStr[8];
    snprintf(batStr, sizeof(batStr), "%d%%", bat);
    M5.Display.setCursor(w - (int)strlen(batStr) * 6 - 3, 3);
    M5.Display.print(batStr);
}

static void drawMenu(const UiNav &nav)
{
    drawStatusBar(wifiIsStaConnected(), "菜单");

    uint8_t idx = nav.menuIndex % kMenuCount;
    const MenuItem &item = kMenuItems[idx];
    const int wScr = M5.Display.width();
    const int hScr = M5.Display.height();

    /* 序号：当前 / 总数 */
    M5.Display.setFont(&fonts::Font0);
    M5.Display.setTextSize(1);
    M5.Display.setTextColor(TFT_DARKGREY, TFT_BLACK);
    char idxStr[12];
    snprintf(idxStr, sizeof(idxStr), "%u/%u", (unsigned)(idx + 1), (unsigned)kMenuCount);
    M5.Display.setCursor(4, STATUS_H + 4);
    M5.Display.print(idxStr);

    /* 一次只显示当前中文项（居中） */
    M5.Display.setFont(&fonts::efontCN_12);
    M5.Display.setTextSize(2);
    M5.Display.setTextDatum(middle_center);
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    M5.Display.drawString(item.titleZh, wScr / 2, STATUS_H + (hScr - STATUS_H - 14) / 2);
    M5.Display.setTextDatum(top_left);
    M5.Display.setTextSize(1);

    M5.Display.setFont(&fonts::efontCN_12);
    M5.Display.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    M5.Display.setCursor(4, hScr - 14);
    M5.Display.print("A进入 B下一项");
}

static constexpr int DATE_Y = TIME_TOP + TIME_H + 2;
static constexpr int WEATHER_Y = DATE_Y + 14;

static void drawClockWeatherArea()
{
    WeatherSnapshot w = weatherGet();
    const int wScr = M5.Display.width();

    String city = w.ok && w.city.length() ? w.city : (w.city.length() ? w.city : "--");
    String line = w.text + " " + w.temp;
    if (!w.ok && w.text == "offline") {
        line = "天气获取失败";
    }
    if (city == s_lastCity && line == s_lastWeatherLine) {
        return;
    }

    M5.Display.fillRect(0, WEATHER_Y, wScr, 40, TFT_BLACK);
    M5.Display.setFont(&fonts::efontCN_12);
    M5.Display.setTextColor(TFT_YELLOW, TFT_BLACK);
    M5.Display.setCursor(4, WEATHER_Y);
    M5.Display.print(city);

    M5.Display.setTextColor(TFT_GREEN, TFT_BLACK);
    M5.Display.setCursor(4, WEATHER_Y + 18);
    M5.Display.print(line);

    s_lastCity = city;
    s_lastWeatherLine = line;
}

static void drawClockFull(bool wifiOk)
{
    drawStatusBar(wifiOk, "时钟");
    String timeStr, dateStr;
    timeServiceFormat(timeStr, dateStr);

    pushTimeString(timeStr);
    s_lastTime = timeStr;

    const int wScr = M5.Display.width();
    M5.Display.setFont(&fonts::Font0);
    M5.Display.setTextSize(1);
    M5.Display.setTextDatum(top_center);
    M5.Display.setTextColor(TFT_CYAN, TFT_BLACK);
    M5.Display.drawString(dateStr, wScr / 2, DATE_Y);
    M5.Display.setTextDatum(top_left);
    s_lastDate = dateStr;

    s_lastCity = "";
    s_lastWeatherLine = "";
    drawClockWeatherArea();

    M5.Display.setFont(&fonts::efontCN_12);
    M5.Display.setTextColor(TFT_DARKGREY, TFT_BLACK);
    M5.Display.setCursor(4, M5.Display.height() - 14);
    M5.Display.print("长按返回");
}

void uiRenderClockMeta(bool wifiOk)
{
    if (s_needFull) {
        return;
    }
    drawStatusBar(wifiOk, "时钟");
    drawClockWeatherArea();
}

void uiRenderClockTick(bool wifiOk)
{
    (void)wifiOk;
    if (s_needFull) {
        return;
    }
    String timeStr, dateStr;
    timeServiceFormat(timeStr, dateStr);
    if (timeStr == s_lastTime && dateStr == s_lastDate) {
        return;
    }

    if (timeStr != s_lastTime) {
        pushTimeString(timeStr);
        s_lastTime = timeStr;
    }
    if (dateStr != s_lastDate) {
        const int wScr = M5.Display.width();
        M5.Display.setFont(&fonts::Font0);
        M5.Display.setTextSize(1);
        M5.Display.setTextDatum(top_center);
        M5.Display.setTextColor(TFT_CYAN, TFT_BLACK);
        /* 日期区用背景色覆写，不用整行 fillRect */
        M5.Display.drawString(dateStr + "   ", wScr / 2, DATE_Y);
        M5.Display.setTextDatum(top_left);
        s_lastDate = dateStr;
    }
}

static bool quoteIsDown(const StockQuote &q)
{
    return q.change.startsWith("-") || q.percent.startsWith("-");
}

static String stockTicker(const StockQuote &q)
{
    String code = q.code;
    if (code.endsWith(".OQ") || code.endsWith(".N") || code.endsWith(".A") || code.endsWith(".B")) {
        code = code.substring(0, code.length() - 3);
    }
    if (code.startsWith(".")) {
        code = code.substring(1);
    }
    if (code.length() && code != "--") {
        return code;
    }
    String bare = q.symbol;
    if (bare.startsWith("s_")) {
        bare = bare.substring(2);
    }
    bare.toUpperCase();
    return bare.length() ? bare : "--";
}

/* A股：有合法中文名才显示，否则只显示代码（避免未知方块字） */
static bool titleHasChinese(const String &s)
{
    const uint8_t *p = (const uint8_t *)s.c_str();
    for (size_t i = 0; i + 2 < s.length(); ++i) {
        if (p[i] >= 0xE4 && p[i] <= 0xE9 && (p[i + 1] & 0xC0) == 0x80 && (p[i + 2] & 0xC0) == 0x80) {
            return true;
        }
    }
    return false;
}

static String stockTitle(const StockQuote &q)
{
    String code = stockTicker(q);
    if (titleHasChinese(q.name)) {
        return q.name + " (" + code + ")";
    }
    if (q.nameEn.length()) {
        return q.nameEn + " (" + code + ")";
    }
    /* A股代码加市场前缀，更易读 */
    String bare = q.symbol;
    if (bare.startsWith("s_")) {
        bare = bare.substring(2);
    }
    bare.toUpperCase();
    if (bare.startsWith("SH") || bare.startsWith("SZ")) {
        return bare;
    }
    return code;
}

static void drawStockFooter()
{
    M5.Display.setFont(&fonts::efontCN_12);
    M5.Display.setTextColor(TFT_DARKGREY, TFT_BLACK);
    M5.Display.setCursor(4, M5.Display.height() - 14);
    M5.Display.print("A切换 B视图 长按返回");
}

/* Yahoo 风格：交易所 · 名称(代码) · 价格 · 涨跌 */
static void drawStockQuoteBody(const StockQuote &q, uint8_t idx, uint8_t total)
{
    const int top = STATUS_H + 2;
    const int wScr = M5.Display.width();

    M5.Display.setFont(&fonts::efontCN_12);
    M5.Display.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    M5.Display.setCursor(4, top);
    String meta = q.market.length() ? q.market : "行情 实时";
    M5.Display.print(meta);

    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    M5.Display.setCursor(4, top + 16);
    M5.Display.printf("%u/%u ", (unsigned)(idx + 1), (unsigned)total);
    M5.Display.print(stockTitle(q));

    M5.Display.drawFastHLine(4, top + 34, wScr - 8, TFT_DARKGREY);

    bool down = quoteIsDown(q);
    uint16_t chgColor = down ? TFT_RED : TFT_GREEN;

    M5.Display.setFont(&fonts::Font0);
    M5.Display.setTextSize(3);
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    M5.Display.setCursor(4, top + 42);
    String price = q.price;
    if (price.length() > 8) {
        price = price.substring(0, 8);
    }
    M5.Display.print(price);

    M5.Display.setTextSize(1);
    M5.Display.setTextColor(chgColor, TFT_BLACK);
    M5.Display.setCursor(4, top + 74);
    M5.Display.print(q.change);
    M5.Display.print(" (");
    M5.Display.print(q.percent);
    M5.Display.print(")");

    M5.Display.setFont(&fonts::efontCN_12);
    M5.Display.setTextColor(TFT_DARKGREY, TFT_BLACK);
    M5.Display.setCursor(4, top + 90);
    if (q.high != "--" || q.low != "--") {
        M5.Display.printf("高 %s  低 %s", q.high.c_str(), q.low.c_str());
    } else if (q.time.length()) {
        M5.Display.setFont(&fonts::Font0);
        M5.Display.print(q.time);
    }
}

static void drawKlineChart(int x, int y, int w, int h)
{
    uint8_t n = stockKlineCount();
    const KlineBar *bars = stockKlineBars();
    M5.Display.fillRect(x, y, w, h, TFT_BLACK);
    M5.Display.drawRect(x, y, w, h, TFT_DARKGREY);
    if (n == 0 || !bars) {
        M5.Display.setFont(&fonts::efontCN_12);
        M5.Display.setTextColor(TFT_DARKGREY, TFT_BLACK);
        M5.Display.setCursor(x + 8, y + h / 2 - 6);
        M5.Display.print("暂无日K数据");
        return;
    }

    float mn = bars[0].low;
    float mx = bars[0].high;
    for (uint8_t i = 1; i < n; ++i) {
        if (bars[i].low < mn) {
            mn = bars[i].low;
        }
        if (bars[i].high > mx) {
            mx = bars[i].high;
        }
    }
    if (mx <= mn) {
        mx = mn + 1.0f;
    }
    float span = mx - mn;

    int gap = 1;
    int barW = (w - 4) / n;
    if (barW < 2) {
        barW = 2;
    }
    if (barW > 6) {
        barW = 6;
    }

    for (uint8_t i = 0; i < n; ++i) {
        const KlineBar &b = bars[i];
        bool up = b.close >= b.open;
        uint16_t col = up ? TFT_GREEN : TFT_RED;
        int cx = x + 2 + (int)i * ((w - 4) / n) + ((w - 4) / n) / 2;
        int yHigh = y + 2 + (int)((mx - b.high) / span * (h - 4));
        int yLow = y + 2 + (int)((mx - b.low) / span * (h - 4));
        int yO = y + 2 + (int)((mx - b.open) / span * (h - 4));
        int yC = y + 2 + (int)((mx - b.close) / span * (h - 4));
        if (yLow < yHigh) {
            int t = yLow;
            yLow = yHigh;
            yHigh = t;
        }
        M5.Display.drawFastVLine(cx, yHigh, yLow - yHigh + 1, col);
        int bodyTop = up ? yC : yO;
        int bodyBot = up ? yO : yC;
        if (bodyBot < bodyTop) {
            int t = bodyBot;
            bodyBot = bodyTop;
            bodyTop = t;
        }
        int bodyH = bodyBot - bodyTop;
        if (bodyH < 1) {
            bodyH = 1;
        }
        int half = barW / 2;
        if (half < 1) {
            half = 1;
        }
        M5.Display.fillRect(cx - half, bodyTop, half * 2 + (barW % 2 ? 0 : 0), bodyH, col);
        (void)gap;
    }

    /* 高低标注 */
    M5.Display.setFont(&fonts::Font0);
    M5.Display.setTextSize(1);
    M5.Display.setTextColor(TFT_DARKGREY, TFT_BLACK);
    char buf[16];
    dtostrf(mx, 0, 2, buf);
    M5.Display.setCursor(x + 2, y + 2);
    M5.Display.print(buf);
    dtostrf(mn, 0, 2, buf);
    M5.Display.setCursor(x + 2, y + h - 10);
    M5.Display.print(buf);
}

bool uiEnsureStockKline(const UiNav &nav, const AppConfig &cfg)
{
    if (cfg.stockCount == 0) {
        return false;
    }
    uint8_t idx = nav.stockIndex % cfg.stockCount;
    /* 内部按 POLL_KLINE_MS 缓存，避免重复打东财/腾讯 */
    return stockKlineRefresh(cfg.stocks[idx], false) > 0;
}

void uiRenderStockQuoteTick(const UiNav &nav, const AppConfig &cfg)
{
    if (nav.screen != SCREEN_STOCK || nav.stockView != STOCK_VIEW_QUOTE || cfg.stockCount == 0) {
        return;
    }
    uint8_t idx = nav.stockIndex % cfg.stockCount;
    StockQuote q = stockGetAt(idx);
    if (!q.ok) {
        return;
    }
    /* 清价格区域再画，避免整屏闪 */
    M5.Display.fillRect(0, STATUS_H + 34, M5.Display.width(), M5.Display.height() - STATUS_H - 50, TFT_BLACK);
    const int top = STATUS_H + 2;
    M5.Display.drawFastHLine(4, top + 32, M5.Display.width() - 8, TFT_DARKGREY);

    bool down = quoteIsDown(q);
    uint16_t chgColor = down ? TFT_RED : TFT_GREEN;
    M5.Display.setFont(&fonts::Font0);
    M5.Display.setTextSize(3);
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    M5.Display.setCursor(4, top + 40);
    String price = q.price;
    if (price.length() > 8) {
        price = price.substring(0, 8);
    }
    M5.Display.print(price);
    M5.Display.setTextSize(1);
    M5.Display.setTextColor(chgColor, TFT_BLACK);
    M5.Display.setCursor(4, top + 72);
    M5.Display.print(q.change);
    M5.Display.print(" (");
    M5.Display.print(q.percent);
    M5.Display.print(")");
    M5.Display.setTextColor(TFT_DARKGREY, TFT_BLACK);
    M5.Display.setCursor(4, top + 88);
    if (q.high != "--") {
        M5.Display.printf("H %s  L %s", q.high.c_str(), q.low.c_str());
    }
}

static void drawStock(const UiNav &nav, bool wifiOk, const AppConfig &cfg)
{
    drawStatusBar(wifiOk, "行情");
    if (cfg.stockCount == 0) {
        M5.Display.setFont(&fonts::efontCN_12);
        M5.Display.setTextColor(TFT_RED, TFT_BLACK);
        M5.Display.setCursor(8, 40);
        M5.Display.print("无股票，请到配置添加");
        return;
    }

    uint8_t idx = nav.stockIndex % cfg.stockCount;
    StockQuote q = stockGetAt(idx);
    if (!q.ok) {
        q.symbol = cfg.stocks[idx];
        q.code = stockTicker(q);
    }

    if (nav.stockView == STOCK_VIEW_QUOTE) {
        drawStockQuoteBody(q, idx, cfg.stockCount);
        drawStockFooter();
        return;
    }

    /* 日 K */
    const int top = STATUS_H + 2;
    M5.Display.setFont(&fonts::efontCN_12);
    M5.Display.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    M5.Display.setCursor(4, top);
    M5.Display.print("日K");

    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    M5.Display.setCursor(4, top + 14);
    M5.Display.printf("%u/%u ", (unsigned)(idx + 1), (unsigned)cfg.stockCount);
    M5.Display.print(stockTitle(q));

    bool down = quoteIsDown(q);
    M5.Display.setFont(&fonts::Font0);
    M5.Display.setTextSize(1);
    M5.Display.setTextColor(down ? TFT_RED : TFT_GREEN, TFT_BLACK);
    M5.Display.setCursor(4, top + 30);
    M5.Display.print(q.price);
    M5.Display.print(' ');
    M5.Display.print(q.percent);

    uiEnsureStockKline(nav, cfg);
    drawKlineChart(2, top + 42, M5.Display.width() - 4, M5.Display.height() - top - 58);
    drawStockFooter();
}

static void drawSetting(bool wifiOk)
{
    drawStatusBar(wifiOk, "配置");
    M5.Display.setFont(&fonts::efontCN_12);
    M5.Display.setTextColor(TFT_YELLOW, TFT_BLACK);
    M5.Display.setCursor(4, STATUS_H + 6);
    M5.Display.print("网页配置");

    M5.Display.setFont(&fonts::Font0);
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    M5.Display.setCursor(4, STATUS_H + 28);
    if (wifiOk) {
        M5.Display.printf("STA %s", WiFi.localIP().toString().c_str());
    } else {
        M5.Display.print("SoftAP mode");
    }
    M5.Display.setCursor(4, STATUS_H + 44);
    M5.Display.printf("AP %s", AP_SSID);
    M5.Display.setCursor(4, STATUS_H + 60);
    M5.Display.printf("PW %s", AP_PASSWORD);
    M5.Display.setCursor(4, STATUS_H + 76);
    M5.Display.printf("http://%s", WiFi.softAPIP().toString().c_str());

    M5.Display.setFont(&fonts::efontCN_12);
    M5.Display.setTextColor(TFT_CYAN, TFT_BLACK);
    M5.Display.setCursor(4, STATUS_H + 96);
    M5.Display.print("浏览器改WiFi/股票");

    M5.Display.setFont(&fonts::efontCN_12);
    M5.Display.setTextColor(TFT_DARKGREY, TFT_BLACK);
    M5.Display.setCursor(4, M5.Display.height() - 14);
    M5.Display.print("长按返回");
}

void uiRenderFull(const UiNav &nav, bool wifiOk, const AppConfig &cfg)
{
    M5.Display.fillScreen(TFT_BLACK);
    if (nav.screen == SCREEN_MENU) {
        drawMenu(nav);
    } else if (nav.screen == SCREEN_CLOCK) {
        drawClockFull(wifiOk);
    } else if (nav.screen == SCREEN_STOCK) {
        drawStock(nav, wifiOk, cfg);
    } else {
        drawSetting(wifiOk);
    }
    s_needFull = false;
}
