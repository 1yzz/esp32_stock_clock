#include "http_util.h"

#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <ctype.h>

#include "board.h"

static uint32_t s_lastGetMs = 0;
static uint32_t s_backoffUntil = 0;
static uint32_t s_backoffStep = 60UL * 1000UL; /* 首次退避 60s，失败翻倍 */

bool httpIsBackingOff()
{
    return (int32_t)(millis() - s_backoffUntil) < 0;
}

uint32_t httpBackoffRemainMs()
{
    if (!httpIsBackingOff()) {
        return 0;
    }
    return s_backoffUntil - millis();
}

static void httpNoteSuccess()
{
    s_backoffStep = 60UL * 1000UL;
}

static void httpNoteLimited()
{
    uint32_t step = s_backoffStep;
    if (step < 60UL * 1000UL) {
        step = 60UL * 1000UL;
    }
    if (step > HTTP_BACKOFF_MAX_MS) {
        step = HTTP_BACKOFF_MAX_MS;
    }
    s_backoffUntil = millis() + step;
    Serial.printf("[http] backoff %lu ms (rate limit / error)\n", (unsigned long)step);
    if (s_backoffStep < HTTP_BACKOFF_MAX_MS) {
        s_backoffStep = step * 2;
        if (s_backoffStep > HTTP_BACKOFF_MAX_MS) {
            s_backoffStep = HTTP_BACKOFF_MAX_MS;
        }
    }
}

bool httpGetText(const String &url, String &out, uint32_t timeoutMs)
{
    if (httpIsBackingOff()) {
        Serial.printf("[http] skip (backoff %lu ms) %s\n", (unsigned long)httpBackoffRemainMs(),
                      url.substring(0, 60).c_str());
        return false;
    }

    uint32_t now = millis();
    if (s_lastGetMs != 0 && (now - s_lastGetMs) < HTTP_MIN_GAP_MS) {
        delay(HTTP_MIN_GAP_MS - (now - s_lastGetMs));
    }

    HTTPClient http;
    http.setTimeout(timeoutMs);
    http.setUserAgent("ESP32-StockClock/1.1");

    /* client 必须活到 http.end() */
    WiFiClientSecure secureClient;
    WiFiClient plainClient;
    bool ok = false;

    if (url.startsWith("https://")) {
        secureClient.setInsecure();
        ok = http.begin(secureClient, url);
    } else {
        ok = http.begin(plainClient, url);
    }
    if (!ok) {
        return false;
    }

    s_lastGetMs = millis();
    int code = http.GET();
    String body = http.getString();
    if (code != HTTP_CODE_OK) {
        Serial.printf("[http] GET %d %s\n", code, url.c_str());
        if (body.length()) {
            Serial.printf("[http] body %s\n", body.substring(0, 240).c_str());
        }
        /* 限流 / 网关拒绝 → 退避 */
        if (code == 403 || code == 429 || code == 503 || code == 509 || code < 0) {
            httpNoteLimited();
        }
        http.end();
        return false;
    }

    httpNoteSuccess();
    out = body;
    http.end();
    return out.length() > 0;
}

String urlEncode(const String &input)
{
    static const char hex[] = "0123456789ABCDEF";
    String out;
    out.reserve(input.length() * 2);
    for (size_t i = 0; i < input.length(); ++i) {
        unsigned char c = (unsigned char)input[i];
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out += (char)c;
        } else {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 0x0F];
        }
    }
    return out;
}

bool extractJsonString(const String &json, const char *key, String &out)
{
    String pattern = String("\"") + key + "\":\"";
    int start = json.indexOf(pattern);
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
