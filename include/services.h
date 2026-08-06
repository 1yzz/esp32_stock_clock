#pragma once

#include <Arduino.h>

#include "app_config.h"

struct WeatherSnapshot {
    String city = "--";
    String path = "";
    String text = "--";
    String code = "";
    String temp = "--";
    bool ok = false;
};

struct StockQuote {
    String symbol;          /* 配置代码，如 s_usAAPL */
    String code = "--";     /* AAPL.OQ / 000001 */
    String name = "--";     /* 中文名 */
    String nameEn = "";     /* 英文名（若有） */
    String market = "";     /* 交易所说明 */
    String currency = "";   /* USD / CNY */
    String price = "--";
    String change = "--";
    String percent = "--";
    String high = "--";
    String low = "--";
    String time = "";
    bool ok = false;
};

#define KLINE_MAX_BARS 36

struct KlineBar {
    float open = 0;
    float high = 0;
    float low = 0;
    float close = 0;
};

void timeServiceInit();
bool timeServiceSynced();
void timeServiceFormat(String &timeStr, String &dateStr);

bool weatherRefresh();
WeatherSnapshot weatherGet();
String geoCityGet();

/* enrich=true 会多打全量接口，仅换股时用；实时刷用 light */
bool stockRefreshSymbol(const String &symbol, StockQuote &out, bool enrich = false);
bool stockRefreshAll(const AppConfig &cfg);
bool stockRefreshIndex(const AppConfig &cfg, uint8_t index, bool enrich = false);
StockQuote stockGetAt(uint8_t index);
uint8_t stockCachedCount();

/* K 线：有缓存且未过期则跳过网络 */
uint8_t stockKlineRefresh(const String &symbol, bool force = false);
const KlineBar *stockKlineBars();
uint8_t stockKlineCount();
String stockKlineSymbol();
uint32_t stockKlineAgeMs();
