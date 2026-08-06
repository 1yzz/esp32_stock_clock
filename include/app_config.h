#pragma once

#include <Arduino.h>

#define MAX_STOCKS 8

/* 用户可配：WiFi + 股票列表。天气 Key 为项目内置。 */
struct AppConfig {
    String wifiSsid;
    String wifiPassword;
    String stocks[MAX_STOCKS];
    uint8_t stockCount = 0;
    bool configured = false;
};

void appConfigSetDefaults(AppConfig &cfg);
void appConfigLoad(AppConfig &cfg);
bool appConfigSave(const AppConfig &cfg);

bool appConfigAddStock(AppConfig &cfg, const String &symbol);
bool appConfigRemoveStock(AppConfig &cfg, uint8_t index);
