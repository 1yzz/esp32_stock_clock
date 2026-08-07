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

/* K 线缓存上限 */
#define KLINE_MAX_BARS 78

enum KlineRange : uint8_t {
    KRANGE_TODAY_5M = 0, /* 当日：5 分钟 K */
    KRANGE_DAY_3,        /* 近 3 天：30 分钟 K（足够点数） */
    KRANGE_DAY_7,        /* 近 7 天：60 分钟 K */
    KRANGE_DAY_30,       /* 近 30 个交易日：日 K */
    KRANGE_DAY_FULL,     /* 完整日 K */
    KRANGE_COUNT,
};

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

/* 运行时 TTL（毫秒），由网页配置写入 */
void stockKlineSetTtlMs(uint32_t todayMs, uint32_t midMs, uint32_t dayMs);

/* K 线：各周期独立拉取与缓存，不做本地切片。force 才强制联网 */
uint8_t stockKlineRefresh(const String &symbol, KlineRange range, bool force = false);
bool stockKlineNeedsFetch(const String &symbol, KlineRange range);
const KlineBar *stockKlineBars();
uint8_t stockKlineCount();
String stockKlineSymbol();
KlineRange stockKlineRange();
uint32_t stockKlineAgeMs();
const char *klineRangeLabel(KlineRange range);
