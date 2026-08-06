#pragma once

#include <Arduino.h>

/* 腾讯行情等 GBK 文本 → UTF-8（供 efont 中文显示） */
String gbkToUtf8(const String &gbk);
bool utf8LooksValid(const String &s);
/* 已是合法 UTF-8 则原样返回，否则按 GBK 转 */
String textToUtf8(const String &raw);
