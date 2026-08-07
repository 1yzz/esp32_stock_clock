#include "services.h"

#include <stdint.h>
#include <string.h>
#include <time.h>

#include "board.h"
#include "gbk_utf8.h"
#include "http_util.h"

static WeatherSnapshot s_weather;
static StockQuote s_quotes[MAX_STOCKS];
static uint8_t s_quoteCount = 0;
static uint32_t s_cnNameNextTryMs[MAX_STOCKS]; /* 东财中文名失败后的下次重试时间 */
static bool s_timeSynced = false;
static String s_geoCity;
static uint32_t s_geoAtMs = 0;
static constexpr uint32_t GEO_CACHE_MS = 6UL * 60UL * 60UL * 1000UL; /* 6h */

/*
 * 各周期独立缓存与请求（无本地切片）：
 *   当日 → 5 分钟
 *   3 天 → 30 分钟
 *   7 天 → 60 分钟
 *   30 天 / 完整 → 日 K（各自一份，lmt 不同）
 */
struct KlineStore {
    String symbol;
    KlineBar bars[KLINE_MAX_BARS];
    uint8_t count = 0;
    uint32_t atMs = 0;
};

struct KlineRangeSpec {
    KlineStore *store;
    const char *klt;
    const char *tqKey;
    bool preferToday;
    uint32_t ttlMs;
    uint8_t fetchLimit; /* 直接按此数量向接口要，展示全部 */
};

static KlineStore s_m5Store;
static KlineStore s_m30Store;
static KlineStore s_m60Store;
static KlineStore s_day30Store;
static KlineStore s_dayFullStore;
static KlineBar s_viewBars[KLINE_MAX_BARS];
static uint8_t s_viewCount = 0;
static String s_viewSymbol;
static KlineRange s_viewRange = KRANGE_TODAY_5M;
static uint32_t s_viewAtMs = 0;

/* 可由网页配置覆盖，默认取 board.h */
static uint32_t s_ttlTodayMs = POLL_KLINE_MS;
static uint32_t s_ttlMidMs = POLL_KLINE_MID_MS;
static uint32_t s_ttlDayMs = POLL_KLINE_DAY_MS;

void stockKlineSetTtlMs(uint32_t todayMs, uint32_t midMs, uint32_t dayMs)
{
    if (todayMs < 15000UL) {
        todayMs = 15000UL;
    }
    if (midMs < 60000UL) {
        midMs = 60000UL;
    }
    if (dayMs < 60000UL) {
        dayMs = 60000UL;
    }
    s_ttlTodayMs = todayMs;
    s_ttlMidMs = midMs;
    s_ttlDayMs = dayMs;
    Serial.printf("[kline] ttl today=%lus mid=%lus day=%lus\n", (unsigned long)(s_ttlTodayMs / 1000UL),
                  (unsigned long)(s_ttlMidMs / 1000UL), (unsigned long)(s_ttlDayMs / 1000UL));
}

const char *klineRangeLabel(KlineRange range)
{
    switch (range) {
    case KRANGE_TODAY_5M:
        return "当日5分";
    case KRANGE_DAY_3:
        return "3天30分";
    case KRANGE_DAY_7:
        return "7天60分";
    case KRANGE_DAY_30:
        return "30天K";
    case KRANGE_DAY_FULL:
        return "完整K";
    default:
        return "K线";
    }
}

static KlineRangeSpec klineSpec(KlineRange range)
{
    switch (range) {
    case KRANGE_TODAY_5M:
        return {&s_m5Store, "5", "m5", true, s_ttlTodayMs, KLINE_MAX_BARS};
    case KRANGE_DAY_3:
        return {&s_m30Store, "30", "m30", false, s_ttlMidMs, KLINE_MAX_BARS};
    case KRANGE_DAY_7:
        return {&s_m60Store, "60", "m60", false, s_ttlMidMs, KLINE_MAX_BARS};
    case KRANGE_DAY_30:
        return {&s_day30Store, "101", "day", false, s_ttlDayMs, 30};
    case KRANGE_DAY_FULL:
    default:
        return {&s_dayFullStore, "101", "day", false, s_ttlDayMs, KLINE_MAX_BARS};
    }
}

static uint32_t storeAgeMs(const KlineStore &st)
{
    if (st.count == 0 || st.atMs == 0) {
        return UINT32_MAX;
    }
    return millis() - st.atMs;
}

static bool storeFresh(const KlineStore &st, const String &symbol, uint32_t ttlMs)
{
    return st.symbol == symbol && st.count > 0 && storeAgeMs(st) < ttlMs;
}

/* 整份展示，不做尾部截取 */
static void publishStore(const KlineStore &st, KlineRange range)
{
    s_viewSymbol = st.symbol;
    s_viewRange = range;
    s_viewAtMs = st.atMs;
    s_viewCount = st.count;
    if (st.count > 0) {
        memcpy(s_viewBars, st.bars, st.count * sizeof(KlineBar));
    }
}

void timeServiceInit()
{
    configTime(8 * 3600, 0, "ntp.aliyun.com", "pool.ntp.org");
    for (int i = 0; i < 30; ++i) {
        time_t now = time(nullptr);
        if (now > 1700000000) {
            s_timeSynced = true;
            Serial.println("[time] NTP synced");
            return;
        }
        delay(500);
    }
    Serial.println("[time] NTP timeout");
}

bool timeServiceSynced()
{
    if (!s_timeSynced) {
        time_t now = time(nullptr);
        s_timeSynced = now > 1700000000;
    }
    return s_timeSynced;
}

void timeServiceFormat(String &timeStr, String &dateStr)
{
    time_t now = time(nullptr);
    if (now < 1700000000) {
        timeStr = "--:--:--";
        dateStr = "waiting NTP";
        return;
    }
    struct tm info;
    localtime_r(&now, &info);
    char tbuf[16];
    char dbuf[32];
    strftime(tbuf, sizeof(tbuf), "%H:%M:%S", &info);
    strftime(dbuf, sizeof(dbuf), "%Y-%m-%d %a", &info);
    timeStr = tbuf;
    dateStr = dbuf;
}

static bool extractJsonStringAfter(const String &json, const char *anchor, const char *key, String &out)
{
    int base = 0;
    if (anchor && anchor[0]) {
        base = json.indexOf(anchor);
        if (base < 0) {
            return false;
        }
    }
    String pattern = String("\"") + key + "\":\"";
    int start = json.indexOf(pattern, base);
    if (start < 0) {
        return false;
    }
    start += pattern.length();
    int end = json.indexOf('"', start);
    if (end < 0) {
        return false;
    }
    out = json.substring(start, end);
    return true;
}

/* 去掉「市/地区」等后缀，便于心知 location 匹配 */
static String normalizeCityName(String city)
{
    city.trim();
    const char *suffixes[] = {"市", "地区", "自治州", "盟", "特别行政区"};
    for (size_t i = 0; i < sizeof(suffixes) / sizeof(suffixes[0]); ++i) {
        String s = suffixes[i];
        if (city.endsWith(s) && city.length() > s.length()) {
            city = city.substring(0, city.length() - s.length());
            break;
        }
    }
    return city;
}

/* 取 JSON 字符串数组第 index 项，如 "location":["中国","江苏","镇江",...] */
static bool extractJsonArrayStringAt(const String &json, const char *key, int index, String &out)
{
    String marker = String("\"") + key + "\":[";
    int start = json.indexOf(marker);
    if (start < 0) {
        return false;
    }
    start += marker.length();
    int end = json.indexOf(']', start);
    if (end < 0) {
        return false;
    }
    String arr = json.substring(start, end);
    int i = 0;
    int p = 0;
    while (i <= index) {
        int q1 = arr.indexOf('"', p);
        if (q1 < 0) {
            return false;
        }
        int q2 = arr.indexOf('"', q1 + 1);
        if (q2 < 0) {
            return false;
        }
        if (i == index) {
            out = arr.substring(q1 + 1, q2);
            return out.length() > 0;
        }
        ++i;
        p = q2 + 1;
    }
    return false;
}

static bool geoCommitCity(const String &raw, String &cityOut)
{
    String city = normalizeCityName(raw);
    if (!city.length()) {
        return false;
    }
    s_geoCity = city;
    s_geoAtMs = millis();
    cityOut = city;
    Serial.printf("[geo] city=%s\n", city.c_str());
    return true;
}

/*
 * 国内 IP 定位当前城市（UTF-8）：
 *   1) 腾讯 https://r.inews.qq.com/api/ip2city → city
 *   2) IPIP https://myip.ipip.net/json → location[2]
 */
static bool geoResolveCity(String &cityOut)
{
    if (s_geoCity.length() && (millis() - s_geoAtMs) < GEO_CACHE_MS) {
        cityOut = s_geoCity;
        return true;
    }

    String payload;
    String city;

    if (httpGetText(GEO_QQ_URL, payload, 8000)) {
        Serial.printf("[geo] qq %s\n", payload.substring(0, 200).c_str());
        extractJsonString(payload, "city", city);
        if (!city.length()) {
            extractJsonString(payload, "province", city); /* 直辖市兜底 */
        }
        if (geoCommitCity(city, cityOut)) {
            return true;
        }
    }

    payload = "";
    city = "";
    if (httpGetText(GEO_IPIP_URL, payload, 8000)) {
        Serial.printf("[geo] ipip %s\n", payload.substring(0, 200).c_str());
        /* location: [国家, 省, 市, ...] */
        if (!extractJsonArrayStringAt(payload, "location", 2, city) || !city.length()) {
            extractJsonArrayStringAt(payload, "location", 1, city);
        }
        if (geoCommitCity(city, cityOut)) {
            return true;
        }
    }

    Serial.println("[geo] resolve fail");
    return false;
}

String geoCityGet()
{
    return s_geoCity;
}

/*
 * 心知 V3 天气实况：
 *   GET .../v3/weather/now.json?key=<私钥>&location=<城市>&language=zh-Hans&unit=c
 */
static bool weatherFetch(const String &location, String &payload)
{
    String url = String("https://api.seniverse.com/v3/weather/now.json?key=") + DEFAULT_WEATHER_KEY +
                 "&location=" + urlEncode(location) + "&language=zh-Hans&unit=c";
    return httpGetText(url, payload);
}

bool weatherRefresh()
{
    String geoCity;
    bool haveGeo = geoResolveCity(geoCity);

    String payload;
    bool ok = false;
    if (haveGeo) {
        ok = weatherFetch(geoCity, payload);
    }
    if (!ok) {
        ok = weatherFetch(DEFAULT_CITY, payload);
    }
    if (!ok) {
        Serial.println("[weather] http fail");
        s_weather = WeatherSnapshot();
        s_weather.city = haveGeo ? geoCity : DEFAULT_CITY;
        s_weather.text = "offline";
        s_weather.ok = false;
        return false;
    }

    Serial.printf("[weather] %s\n", payload.substring(0, 240).c_str());

    String city, path, text, code, temperature;
    extractJsonStringAfter(payload, "\"location\"", "name", city);
    extractJsonStringAfter(payload, "\"location\"", "path", path);
    extractJsonStringAfter(payload, "\"now\"", "text", text);
    extractJsonStringAfter(payload, "\"now\"", "code", code);
    extractJsonStringAfter(payload, "\"now\"", "temperature", temperature);

    /* 优先显示公开定位城市；心知返回名作为补充 */
    if (haveGeo) {
        s_weather.city = geoCity;
    } else {
        s_weather.city = city.length() ? city : DEFAULT_CITY;
    }
    s_weather.path = path;
    s_weather.text = text.length() ? text : "--";
    s_weather.code = code;
    s_weather.temp = temperature.length() ? (temperature + "℃") : "--";
    s_weather.ok = (temperature.length() > 0) || (text.length() > 0);

    Serial.printf("[weather] city=%s text=%s temp=%s code=%s\n", s_weather.city.c_str(), s_weather.text.c_str(),
                  s_weather.temp.c_str(), s_weather.code.c_str());
    return s_weather.ok;
}

WeatherSnapshot weatherGet()
{
    return s_weather;
}

static void stripQuotes(String &s)
{
    if (s.length() >= 2 && s[0] == '"' && s[s.length() - 1] == '"') {
        s = s.substring(1, s.length() - 1);
    }
}

static String stockBareCode(const String &symbol)
{
    String s = symbol;
    if (s.startsWith("s_")) {
        s = s.substring(2);
    }
    return s;
}

/* 上交所 / 深交所 A 股（含指数） */
static bool isAshareSymbol(const String &symbol)
{
    String bare = stockBareCode(symbol);
    bare.toLowerCase();
    return bare.startsWith("sh") || bare.startsWith("sz");
}

static bool isCnListedSymbol(const String &symbol)
{
    String bare = stockBareCode(symbol);
    bare.toLowerCase();
    return bare.startsWith("sh") || bare.startsWith("sz") || bare.startsWith("hk");
}

/* 名称是否含可显示的 UTF-8 中文（避免把乱码当中文名） */
static bool nameHasUtf8Chinese(const String &name)
{
    const uint8_t *p = (const uint8_t *)name.c_str();
    size_t n = name.length();
    for (size_t i = 0; i + 2 < n; ++i) {
        /* 常用汉字三字节 UTF-8: E4-E9 开头 */
        if (p[i] >= 0xE4 && p[i] <= 0xE9 && (p[i + 1] & 0xC0) == 0x80 && (p[i + 2] & 0xC0) == 0x80) {
            return true;
        }
    }
    return false;
}

static void fillMarketMeta(StockQuote &out)
{
    String bare = stockBareCode(out.symbol);
    bare.toLowerCase();
    /* 用常见汉字 + ASCII，减少生僻标点缺字 */
    if (bare.startsWith("us")) {
        out.currency = "USD";
        String code = out.code;
        if (code.endsWith(".OQ") || code.indexOf("NDX") >= 0 || code.startsWith(".")) {
            out.market = "纳斯达克 实时 USD";
        } else if (code.endsWith(".N")) {
            out.market = "纽交所 实时 USD";
        } else {
            out.market = "美股 实时 USD";
        }
    } else if (bare.startsWith("sh")) {
        out.currency = "CNY";
        out.market = "上交所 实时 CNY";
    } else if (bare.startsWith("sz")) {
        out.currency = "CNY";
        out.market = "深交所 实时 CNY";
    } else if (bare.startsWith("hk")) {
        out.currency = "HKD";
        out.market = "港交所 实时 HKD";
    } else {
        out.market = "行情 实时";
    }
}

/* 腾讯简行情：market~name~code~price~change~percent~... */
static bool parseSimpleQuoteLine(const String &line, StockQuote &out)
{
    int eq = line.indexOf('=');
    if (eq < 0) {
        return false;
    }
    String raw = line.substring(eq + 1);
    stripQuotes(raw);
    int semi = raw.indexOf(';');
    if (semi >= 0) {
        raw = raw.substring(0, semi);
    }

    String parts[8];
    int start = 0;
    int idx = 0;
    while (idx < 8) {
        int p = raw.indexOf('~', start);
        if (p < 0) {
            parts[idx++] = raw.substring(start);
            break;
        }
        parts[idx++] = raw.substring(start, p);
        start = p + 1;
    }
    if (idx < 6) {
        return false;
    }
    /* 暂存腾讯名（GBK/UTF-8 自适应）；A/港股最终以东财 UTF-8 为准 */
    out.name = textToUtf8(parts[1]);
    out.code = parts[2];
    out.price = parts[3];
    out.change = parts[4];
    out.percent = parts[5];
    if (out.percent.length() && !out.percent.endsWith("%")) {
        out.percent += "%";
    }
    /* 涨跌显示加号 */
    if (out.change.length() && out.change[0] != '-' && out.change[0] != '+') {
        out.change = String("+") + out.change;
    }
    if (out.percent.length() && out.percent[0] != '-' && out.percent[0] != '+') {
        out.percent = String("+") + out.percent;
    }
    fillMarketMeta(out);
    out.ok = out.price.length() > 0 && out.price != out.code;
    return out.ok;
}

/* 全量行情补充 high/low/time/英文名（美股） */
static void enrichFromFullQuote(const String &symbol, StockQuote &out)
{
    String bare = stockBareCode(symbol);
    String payload;
    if (!httpGetText("http://qt.gtimg.cn/q=" + bare, payload)) {
        return;
    }
    int eq = payload.indexOf('=');
    if (eq < 0) {
        return;
    }
    String raw = payload.substring(eq + 1);
    stripQuotes(raw);
    int semi = raw.indexOf(';');
    if (semi >= 0) {
        raw = raw.substring(0, semi);
    }

    String parts[50];
    int start = 0;
    int n = 0;
    while (n < 50) {
        int p = raw.indexOf('~', start);
        if (p < 0) {
            parts[n++] = raw.substring(start);
            break;
        }
        parts[n++] = raw.substring(start, p);
        start = p + 1;
    }
    if (n < 35) {
        return;
    }

    bare.toLowerCase();
    if (bare.startsWith("us")) {
        /* 美股：31涨跌 32涨幅 33高 34低 30时间 35币种 ~45 英文名 */
        if (n > 32) {
            out.change = parts[31];
            out.percent = parts[32];
            if (out.change.length() && out.change[0] != '-' && out.change[0] != '+') {
                out.change = String("+") + out.change;
            }
            if (out.percent.length() && !out.percent.endsWith("%")) {
                out.percent += "%";
            }
            if (out.percent.length() && out.percent[0] != '-' && out.percent[0] != '+') {
                out.percent = String("+") + out.percent;
            }
        }
        if (n > 34) {
            out.high = parts[33];
            out.low = parts[34];
        }
        if (n > 30) {
            out.time = parts[30];
        }
        if (n > 35 && parts[35].length()) {
            out.currency = parts[35];
        }
        if (n > 45 && parts[45].length() > 1 && parts[45][0] >= 'A') {
            out.nameEn = parts[45];
        }
        out.price = parts[3];
        out.code = parts[2];
        out.name = gbkToUtf8(parts[1]);
    } else {
        /* A股常见：3现价 4昨收 5开 33高 34低 31涨跌 32涨幅 30时间 */
        out.price = parts[3];
        if (n > 34) {
            out.high = parts[33];
            out.low = parts[34];
        }
        if (n > 32) {
            out.change = parts[31];
            out.percent = parts[32];
            if (out.change.length() && out.change[0] != '-' && out.change[0] != '+') {
                out.change = String("+") + out.change;
            }
            if (out.percent.length() && !out.percent.endsWith("%")) {
                out.percent += "%";
            }
            if (out.percent.length() && out.percent[0] != '-' && out.percent[0] != '+') {
                out.percent = String("+") + out.percent;
            }
        }
        if (n > 30) {
            out.time = parts[30];
        }
        out.currency = "CNY";
    }
    fillMarketMeta(out);
    out.ok = out.price.length() > 0;
}

static bool stockToSecid(const String &symbol, String &secid);

/* 东财 UTF-8 中文名（A/港股专用，避免腾讯 GBK 显示成未知字符） */
static bool fetchEastmoneyCnName(const String &symbol, String &nameOut)
{
    String secid;
    if (!stockToSecid(symbol, secid)) {
        return false;
    }
    /* HTTP 更稳；返回 UTF-8 中文名 f58 */
    String url = String("http://push2.eastmoney.com/api/qt/stock/get?fltt=2&invt=2&secid=") + secid +
                 "&fields=f57,f58";
    String payload;
    if (!httpGetText(url, payload, 8000)) {
        return false;
    }
    String name;
    if (!extractJsonString(payload, "f58", name) || !name.length()) {
        return false;
    }
    if (!nameHasUtf8Chinese(name)) {
        return false;
    }
    nameOut = name;
    Serial.printf("[stock] cn name %s -> %s\n", symbol.c_str(), nameOut.c_str());
    return true;
}

bool stockRefreshSymbol(const String &symbol, StockQuote &out, bool enrich)
{
    out = StockQuote();
    out.symbol = symbol;
    String url = "http://qt.gtimg.cn/q=" + symbol;
    String payload;
    if (!httpGetText(url, payload)) {
        return false;
    }
    if (!parseSimpleQuoteLine(payload, out)) {
        return false;
    }
    out.symbol = symbol;

    if (isAshareSymbol(symbol)) {
        /* A股：腾讯名 GBK→UTF-8 可直接显示；东财 UTF-8 优先覆盖 */
        String tencentName = out.name;
        if (!nameHasUtf8Chinese(tencentName)) {
            out.name = "";
        }
        if (enrich) {
            String cn;
            if (fetchEastmoneyCnName(symbol, cn)) {
                out.name = cn;
            } else if (nameHasUtf8Chinese(tencentName)) {
                out.name = tencentName;
            }
        }
    } else if (isCnListedSymbol(symbol)) {
        /* 港股：仍以东财 UTF-8 为主 */
        String tencentName = out.name;
        out.name = "";
        if (enrich) {
            String cn;
            if (fetchEastmoneyCnName(symbol, cn)) {
                out.name = cn;
            } else if (nameHasUtf8Chinese(tencentName)) {
                out.name = tencentName;
            }
        }
    } else if (enrich) {
        String bare = stockBareCode(symbol);
        bare.toLowerCase();
        if (bare.startsWith("us")) {
            enrichFromFullQuote(symbol, out);
        }
    }
    return out.ok;
}

bool stockRefreshIndex(const AppConfig &cfg, uint8_t index, bool enrich)
{
    if (index >= cfg.stockCount || index >= MAX_STOCKS) {
        return false;
    }
    String keptName = s_quotes[index].name;
    StockQuote q;
    if (!stockRefreshSymbol(cfg.stocks[index], q, enrich)) {
        return false;
    }

    if (isAshareSymbol(cfg.stocks[index])) {
        /* A股：已有中文名则保留；否则用本次腾讯转码名，再尝试东财升级 */
        if (nameHasUtf8Chinese(keptName)) {
            q.name = keptName;
        } else if (!nameHasUtf8Chinese(q.name)) {
            q.name = "";
        }
        if (!nameHasUtf8Chinese(q.name) || enrich) {
            if ((int32_t)(millis() - s_cnNameNextTryMs[index]) >= 0) {
                String cn;
                if (fetchEastmoneyCnName(cfg.stocks[index], cn)) {
                    q.name = cn;
                    s_cnNameNextTryMs[index] = 0;
                } else if (!nameHasUtf8Chinese(q.name)) {
                    s_cnNameNextTryMs[index] = millis() + 120UL * 1000UL;
                } else {
                    s_cnNameNextTryMs[index] = 0;
                }
            }
        }
    } else if (isCnListedSymbol(cfg.stocks[index])) {
        if (nameHasUtf8Chinese(keptName)) {
            q.name = keptName;
            s_cnNameNextTryMs[index] = 0;
        } else if (nameHasUtf8Chinese(q.name)) {
            s_cnNameNextTryMs[index] = 0;
        } else if ((int32_t)(millis() - s_cnNameNextTryMs[index]) >= 0) {
            String cn;
            if (fetchEastmoneyCnName(cfg.stocks[index], cn)) {
                q.name = cn;
                s_cnNameNextTryMs[index] = 0;
            } else {
                q.name = "";
                s_cnNameNextTryMs[index] = millis() + 120UL * 1000UL;
            }
        } else {
            q.name = "";
        }
    }

    if (s_quoteCount < cfg.stockCount) {
        s_quoteCount = cfg.stockCount;
        if (s_quoteCount > MAX_STOCKS) {
            s_quoteCount = MAX_STOCKS;
        }
    }
    s_quotes[index] = q;
    return true;
}

bool stockRefreshAll(const AppConfig &cfg)
{
    String keptName[MAX_STOCKS];
    uint8_t oldCount = s_quoteCount;
    for (uint8_t i = 0; i < MAX_STOCKS; ++i) {
        keptName[i] = (i < oldCount) ? s_quotes[i].name : String();
    }

    s_quoteCount = cfg.stockCount;
    if (s_quoteCount > MAX_STOCKS) {
        s_quoteCount = MAX_STOCKS;
    }

    String q;
    for (uint8_t i = 0; i < s_quoteCount; ++i) {
        if (i) {
            q += ',';
        }
        q += cfg.stocks[i];
        s_quotes[i] = StockQuote();
        s_quotes[i].symbol = cfg.stocks[i];
    }
    if (q.length() == 0) {
        return false;
    }

    String payload;
    if (!httpGetText("http://qt.gtimg.cn/q=" + q, payload)) {
        return false;
    }

    bool any = false;
    int start = 0;
    while (start < (int)payload.length()) {
        int end = payload.indexOf(';', start);
        if (end < 0) {
            end = payload.length();
        }
        String line = payload.substring(start, end);
        for (uint8_t i = 0; i < s_quoteCount; ++i) {
            if (line.indexOf(cfg.stocks[i]) >= 0 || line.indexOf(stockBareCode(cfg.stocks[i])) >= 0) {
                if (parseSimpleQuoteLine(line, s_quotes[i])) {
                    s_quotes[i].symbol = cfg.stocks[i];
                    if (isAshareSymbol(cfg.stocks[i])) {
                        /* A股：优先缓存名，否则保留本次腾讯转码中文名 */
                        if (nameHasUtf8Chinese(keptName[i])) {
                            s_quotes[i].name = keptName[i];
                        } else if (!nameHasUtf8Chinese(s_quotes[i].name)) {
                            s_quotes[i].name = "";
                        }
                    } else if (isCnListedSymbol(cfg.stocks[i])) {
                        if (nameHasUtf8Chinese(keptName[i])) {
                            s_quotes[i].name = keptName[i];
                        } else {
                            s_quotes[i].name = "";
                        }
                    }
                    any = true;
                }
            }
        }
        start = end + 1;
    }
    return any;
}

StockQuote stockGetAt(uint8_t index)
{
    if (index >= s_quoteCount) {
        return StockQuote();
    }
    return s_quotes[index];
}

uint8_t stockCachedCount()
{
    return s_quoteCount;
}

/* 配置代码 → 东方财富 secid */
static bool stockToSecid(const String &symbol, String &secid)
{
    String bare = stockBareCode(symbol);
    String lower = bare;
    lower.toLowerCase();

    if (lower.startsWith("sh") && lower.length() >= 8) {
        secid = "1." + lower.substring(2);
        return true;
    }
    if (lower.startsWith("sz") && lower.length() >= 8) {
        secid = "0." + lower.substring(2);
        return true;
    }
    if (lower.startsWith("us") && lower.length() > 2) {
        String ticker = bare.substring(2);
        ticker.toUpperCase();
        /* 纳指用 100.NDX，个股美股 105.XXX */
        if (ticker == "NDX" || ticker == ".NDX") {
            secid = "100.NDX";
        } else {
            secid = "105." + ticker;
        }
        return true;
    }
    if (lower.startsWith("hk") && lower.length() > 2) {
        secid = "116." + lower.substring(2);
        return true;
    }
    return false;
}

/* 本地日历日 YYYY-MM-DD；未对时则空串（不过滤） */
static String localDateYmd()
{
    time_t now = time(nullptr);
    if (now < 1700000000) {
        return String();
    }
    struct tm info;
    localtime_r(&now, &info);
    char buf[12];
    strftime(buf, sizeof(buf), "%Y-%m-%d", &info);
    return String(buf);
}

static bool parseEastmoneyKlinesTo(const String &payload, bool preferToday, KlineStore &dst)
{
    dst.count = 0;
    int arr = payload.indexOf("\"klines\":[");
    if (arr < 0) {
        return false;
    }
    arr += 10;
    int end = payload.indexOf(']', arr);
    if (end < 0) {
        return false;
    }

    String today = preferToday ? localDateYmd() : String();
    KlineBar tmp[KLINE_MAX_BARS];
    uint8_t n = 0;
    uint8_t head = 0;
    bool onToday = false;

    int p = arr;
    while (p < end) {
        int q1 = payload.indexOf('"', p);
        if (q1 < 0 || q1 >= end) {
            break;
        }
        int q2 = payload.indexOf('"', q1 + 1);
        if (q2 < 0 || q2 > end) {
            break;
        }
        String row = payload.substring(q1 + 1, q2);
        int f0 = row.indexOf(',');
        int f1 = row.indexOf(',', f0 + 1);
        int f2 = row.indexOf(',', f1 + 1);
        int f3 = row.indexOf(',', f2 + 1);
        int f4 = row.indexOf(',', f3 + 1);
        if (f0 > 0 && f1 > f0 && f2 > f1 && f3 > f2) {
            bool isToday = today.length() && row.startsWith(today);
            if (isToday && !onToday) {
                n = 0;
                head = 0;
                onToday = true;
            } else if (onToday && !isToday) {
                p = q2 + 1;
                continue;
            }

            KlineBar b;
            b.open = row.substring(f0 + 1, f1).toFloat();
            b.close = row.substring(f1 + 1, f2).toFloat();
            b.high = row.substring(f2 + 1, f3).toFloat();
            b.low = (f4 > f3) ? row.substring(f3 + 1, f4).toFloat() : row.substring(f3 + 1).toFloat();
            if (n < KLINE_MAX_BARS) {
                tmp[n++] = b;
            } else {
                tmp[head] = b;
                head = (uint8_t)((head + 1) % KLINE_MAX_BARS);
            }
        }
        p = q2 + 1;
    }

    if (n == 0) {
        return false;
    }
    if (n < KLINE_MAX_BARS) {
        memcpy(dst.bars, tmp, n * sizeof(KlineBar));
        dst.count = n;
    } else {
        for (uint8_t i = 0; i < KLINE_MAX_BARS; ++i) {
            dst.bars[i] = tmp[(head + i) % KLINE_MAX_BARS];
        }
        dst.count = KLINE_MAX_BARS;
    }
    return true;
}

static bool parseTencentKlinesTo(const String &payload, const char *key, KlineStore &dst)
{
    dst.count = 0;
    String marker = String("\"") + key + "\":[";
    int day = payload.indexOf(marker);
    if (day < 0) {
        return false;
    }
    int p = day + marker.length();
    while (dst.count < KLINE_MAX_BARS) {
        int a = payload.indexOf('[', p);
        if (a < 0) {
            break;
        }
        int b = payload.indexOf(']', a);
        if (b < 0) {
            break;
        }
        String row = payload.substring(a + 1, b);
        String vals[6];
        int vi = 0, rs = 0;
        while (vi < 6) {
            int q1 = row.indexOf('"', rs);
            if (q1 < 0) {
                break;
            }
            int q2 = row.indexOf('"', q1 + 1);
            if (q2 < 0) {
                break;
            }
            vals[vi++] = row.substring(q1 + 1, q2);
            rs = q2 + 1;
        }
        if (vi < 5) {
            int c0 = row.indexOf(',');
            if (c0 > 0) {
                vals[0] = row.substring(0, c0);
                vals[0].replace("\"", "");
                int start = c0 + 1;
                vi = 1;
                while (vi < 6 && start < (int)row.length()) {
                    int c = row.indexOf(',', start);
                    String part = (c < 0) ? row.substring(start) : row.substring(start, c);
                    part.trim();
                    part.replace("\"", "");
                    vals[vi++] = part;
                    if (c < 0) {
                        break;
                    }
                    start = c + 1;
                }
            }
        }
        if (vi >= 5) {
            dst.bars[dst.count].open = vals[1].toFloat();
            dst.bars[dst.count].close = vals[2].toFloat();
            dst.bars[dst.count].high = vals[3].toFloat();
            dst.bars[dst.count].low = vals[4].toFloat();
            dst.count++;
        }
        p = b + 1;
        if (p < (int)payload.length() && payload[p] == ']') {
            break;
        }
    }
    return dst.count > 0;
}

static bool fetchKlineStore(const String &symbol, const KlineRangeSpec &spec, KlineStore &dst)
{
    String secid;
    if (!stockToSecid(symbol, secid)) {
        dst.count = 0;
        dst.symbol = symbol;
        dst.atMs = 0;
        return false;
    }

    uint8_t limit = spec.fetchLimit;
    if (limit == 0 || limit > KLINE_MAX_BARS) {
        limit = KLINE_MAX_BARS;
    }

    String url = String("http://push2his.eastmoney.com/api/qt/stock/kline/get?secid=") + secid +
                 "&fields1=f1,f2,f3,f4,f5,f6&fields2=f51,f52,f53,f54,f55,f56&klt=" + spec.klt +
                 "&fqt=1&end=20500101&lmt=" + String(limit);
    String payload;
    bool ok = httpGetText(url, payload, 12000);
    if (!ok) {
        url = String("https://push2his.eastmoney.com/api/qt/stock/kline/get?secid=") + secid +
              "&fields1=f1,f2,f3,f4,f5,f6&fields2=f51,f52,f53,f54,f55,f56&klt=" + spec.klt +
              "&fqt=1&end=20500101&lmt=" + String(limit);
        ok = httpGetText(url, payload, 12000);
    }

    if (ok && parseEastmoneyKlinesTo(payload, spec.preferToday, dst)) {
        dst.symbol = symbol;
        dst.atMs = millis();
        Serial.printf("[kline] em %s klt=%s lmt=%u bars=%u\n", symbol.c_str(), spec.klt, (unsigned)limit,
                      (unsigned)dst.count);
        return true;
    }

    String bare = stockBareCode(symbol);
    bare.toLowerCase();
    url = String("https://web.ifzq.gtimg.cn/appstock/app/fqkline/get?param=") + bare + "," + spec.tqKey +
          ",,," + String(limit) + ",qfq";
    payload = "";
    if (!httpGetText(url, payload, 12000) || !parseTencentKlinesTo(payload, spec.tqKey, dst)) {
        dst.count = 0;
        dst.symbol = symbol;
        dst.atMs = 0;
        Serial.printf("[kline] fail %s %s\n", symbol.c_str(), spec.tqKey);
        return false;
    }
    dst.symbol = symbol;
    dst.atMs = millis();
    Serial.printf("[kline] tq %s %s bars=%u\n", symbol.c_str(), spec.tqKey, (unsigned)dst.count);
    return true;
}

bool stockKlineNeedsFetch(const String &symbol, KlineRange range)
{
    if (range >= KRANGE_COUNT) {
        range = KRANGE_TODAY_5M;
    }
    KlineRangeSpec spec = klineSpec(range);
    return !storeFresh(*spec.store, symbol, spec.ttlMs);
}

uint32_t stockKlineAgeMs()
{
    if (s_viewCount == 0 || s_viewAtMs == 0) {
        return UINT32_MAX;
    }
    return millis() - s_viewAtMs;
}

uint8_t stockKlineRefresh(const String &symbol, KlineRange range, bool force)
{
    if (range >= KRANGE_COUNT) {
        range = KRANGE_TODAY_5M;
    }

    KlineRangeSpec spec = klineSpec(range);
    KlineStore &store = *spec.store;

    if (!force && storeFresh(store, symbol, spec.ttlMs)) {
        publishStore(store, range);
        Serial.printf("[kline] cache %s %s bars=%u age=%lu\n", symbol.c_str(), klineRangeLabel(range),
                      (unsigned)s_viewCount, (unsigned long)storeAgeMs(store));
        return s_viewCount;
    }

    if (!fetchKlineStore(symbol, spec, store)) {
        if (store.symbol == symbol && store.count > 0) {
            publishStore(store, range);
            return s_viewCount;
        }
        s_viewCount = 0;
        s_viewSymbol = symbol;
        s_viewRange = range;
        s_viewAtMs = 0;
        return 0;
    }

    publishStore(store, range);
    return s_viewCount;
}

const KlineBar *stockKlineBars()
{
    return s_viewBars;
}

uint8_t stockKlineCount()
{
    return s_viewCount;
}

String stockKlineSymbol()
{
    return s_viewSymbol;
}

KlineRange stockKlineRange()
{
    return s_viewRange;
}
