#pragma once

#define BOARD_NAME "M5StickS3"

#define AP_SSID     "M5StickS3-Clock"
#define AP_PASSWORD "12345678"

/* 心知 V3：location 用城市名；key= 私钥。城市由国内 IP 定位获得 */
#define DEFAULT_CITY           "shanghai"
#define DEFAULT_WEATHER_UID    "PAfRjOU1ujUE5G37K"
#define DEFAULT_WEATHER_KEY    "SxUL-69yfaxl6mCAP"
/* 国内 IP→城市：腾讯新闻 / IPIP（UTF-8 JSON） */
#define GEO_QQ_URL   "https://r.inews.qq.com/api/ip2city"
#define GEO_IPIP_URL "https://myip.ipip.net/json"

/*
 * 接口节流（免费源易封，宁慢勿刷）
 * - 股票页只刷当前一只简行情
 * - 全列表 / 天气 / K 线低频
 */
#define POLL_WEATHER_MS      (10UL * 60UL * 1000UL) /* 天气 10 分钟 */
#define POLL_STOCK_QUOTE_MS  (15UL * 1000UL)        /* 当前标的 15 秒 */
#define POLL_STOCK_LIST_MS   (3UL * 60UL * 1000UL)  /* 全列表 3 分钟（非行情页） */
#define POLL_KLINE_MS        (5UL * 60UL * 1000UL)  /* 日 K 5 分钟 */
#define HTTP_MIN_GAP_MS      1000UL                 /* 任意请求最小间隔 */
#define HTTP_BACKOFF_MAX_MS  (10UL * 60UL * 1000UL) /* 被限流后最长退避 */
