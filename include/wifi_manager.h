#pragma once

#include "app_config.h"

void wifiInit();
void wifiStartAp();
bool wifiConnectSta(const AppConfig &cfg, uint32_t timeoutMs = 15000);
bool wifiIsStaConnected();
int wifiScan(String *ssids, int8_t *rssi, int maxCount);
