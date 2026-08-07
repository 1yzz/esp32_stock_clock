#pragma once

#include <Arduino.h>

#define MAX_STOCKS 8

/* 用户可配：WiFi + 股票列表 + K 线 TTL。天气 Key 为项目内置。 */
struct AppConfig {
    String wifiSsid;
    String wifiPassword;
    String stocks[MAX_STOCKS];
    uint8_t stockCount = 0;
    bool configured = false;
    /* K 线缓存 TTL（秒），网页可改 */
    uint32_t ttlKlineTodaySec = 60;  /* 当日 5 分 */
    uint32_t ttlKlineMidSec = 600;   /* 3天30分 / 7天60分 */
    uint32_t ttlKlineDaySec = 1800;  /* 30天 / 完整日 K */
};

void appConfigSetDefaults(AppConfig &cfg);
void appConfigLoad(AppConfig &cfg);
bool appConfigSave(AppConfig &cfg);
void appConfigClampTtl(AppConfig &cfg);
/* 把 TTL 同步到行情服务运行时 */
void appConfigApplyRuntime(const AppConfig &cfg);

bool appConfigAddStock(AppConfig &cfg, const String &symbol);
bool appConfigRemoveStock(AppConfig &cfg, uint8_t index);
