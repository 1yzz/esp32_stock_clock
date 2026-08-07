#pragma once

#include <Arduino.h>

#include "app_config.h"
#include "services.h"

enum UiScreen : uint8_t {
    SCREEN_MENU = 0,
    SCREEN_CLOCK,
    SCREEN_STOCK,
    SCREEN_SETTING,
};

enum StockView : uint8_t {
    STOCK_VIEW_QUOTE = 0, /* 实时报价 */
    STOCK_VIEW_K_TODAY,   /* 当日 5 分 K */
    STOCK_VIEW_K_3D,      /* 3 天日 K */
    STOCK_VIEW_K_7D,      /* 7 天日 K */
    STOCK_VIEW_K_30D,     /* 30 天日 K */
    STOCK_VIEW_K_FULL,    /* 完整日 K（屏幕可展示上限） */
    STOCK_VIEW_COUNT,
};

inline bool stockViewIsKline(uint8_t view)
{
    return view >= STOCK_VIEW_K_TODAY && view <= STOCK_VIEW_K_FULL;
}

enum ButtonEvent : uint8_t {
    BTN_NONE = 0,
    BTN_A_SHORT,
    BTN_B_SHORT,
    BTN_A_LONG,
    BTN_B_LONG,
};

struct UiNav {
    UiScreen screen = SCREEN_MENU;
    uint8_t menuIndex = 0; /* 首页当前项，一次只显示这一项 */
    uint8_t stockIndex = 0;
    uint8_t stockView = STOCK_VIEW_QUOTE;
};

/* 首页菜单项数量（表驱动，新增选项只改 ui.cpp 表） */
uint8_t uiMenuCount();

void displayInit();
ButtonEvent buttonsPoll();
void uiNavInit(UiNav &nav);
bool uiNavHandle(UiNav &nav, ButtonEvent evt, AppConfig &cfg);
void uiRenderFull(const UiNav &nav, bool wifiOk, const AppConfig &cfg);
/* 时钟秒刷新：定宽不透明覆写，不 fillRect */
void uiRenderClockTick(bool wifiOk);
/* 仅更新天气/状态条，不碰时间区 */
void uiRenderClockMeta(bool wifiOk);
/* 股票报价页局部刷新价格区 */
void uiRenderStockQuoteTick(const UiNav &nav, const AppConfig &cfg);
void uiMarkFullRedraw();
KlineRange uiKlineRangeFromView(uint8_t stockView);
bool uiEnsureStockKline(const UiNav &nav, const AppConfig &cfg);
