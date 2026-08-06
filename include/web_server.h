#pragma once

#include "app_config.h"

void webServerBegin(AppConfig &cfg);
void webServerSetWifiSavedCallback(void (*cb)(const AppConfig &));
void webServerLoop();
