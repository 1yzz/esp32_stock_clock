#pragma once

#include <Arduino.h>

bool httpGetText(const String &url, String &out, uint32_t timeoutMs = 10000);
/* 全局节流：是否因退避暂时不要打网 */
bool httpIsBackingOff();
uint32_t httpBackoffRemainMs();

String urlEncode(const String &input);
bool extractJsonString(const String &json, const char *key, String &out);
