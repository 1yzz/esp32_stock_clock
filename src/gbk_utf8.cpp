#include "gbk_utf8.h"

#include "gbk_uni_table.h"

bool utf8LooksValid(const String &s)
{
    const uint8_t *p = (const uint8_t *)s.c_str();
    size_t n = s.length();
    size_t i = 0;
    bool hasMulti = false;
    while (i < n) {
        uint8_t c = p[i];
        if (c <= 0x7F) {
            ++i;
            continue;
        }
        hasMulti = true;
        int need = 0;
        if ((c & 0xE0) == 0xC0) {
            need = 1;
        } else if ((c & 0xF0) == 0xE0) {
            need = 2;
        } else if ((c & 0xF8) == 0xF0) {
            need = 3;
        } else {
            return false;
        }
        if (i + need >= n) {
            return false;
        }
        for (int k = 1; k <= need; ++k) {
            if ((p[i + k] & 0xC0) != 0x80) {
                return false;
            }
        }
        i += (size_t)need + 1;
    }
    return hasMulti || n > 0;
}

static uint16_t gbkLookupUnicode(uint16_t gbk)
{
    int lo = 0;
    int hi = (int)kGbkUniCount - 1;
    while (lo <= hi) {
        int mid = (lo + hi) >> 1;
        uint16_t v = kGbkUniGbk[mid];
        if (v == gbk) {
            return kGbkUniUni[mid];
        }
        if (v < gbk) {
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }
    return 0;
}

static void appendUtf8(String &out, uint16_t uni)
{
    if (uni < 0x80) {
        out += (char)uni;
    } else if (uni < 0x800) {
        out += (char)(0xC0 | (uni >> 6));
        out += (char)(0x80 | (uni & 0x3F));
    } else {
        out += (char)(0xE0 | (uni >> 12));
        out += (char)(0x80 | ((uni >> 6) & 0x3F));
        out += (char)(0x80 | (uni & 0x3F));
    }
}

String gbkToUtf8(const String &gbk)
{
    String out;
    out.reserve(gbk.length() * 2);
    const uint8_t *p = (const uint8_t *)gbk.c_str();
    size_t n = gbk.length();
    for (size_t i = 0; i < n;) {
        uint8_t b = p[i];
        if (b < 0x80) {
            out += (char)b;
            ++i;
            continue;
        }
        if (i + 1 >= n) {
            break;
        }
        uint16_t code = ((uint16_t)b << 8) | p[i + 1];
        uint16_t uni = gbkLookupUnicode(code);
        if (uni) {
            appendUtf8(out, uni);
        }
        i += 2;
    }
    return out;
}

String textToUtf8(const String &raw)
{
    if (!raw.length()) {
        return raw;
    }
    /* 纯 ASCII */
    bool onlyAscii = true;
    for (size_t i = 0; i < raw.length(); ++i) {
        if ((uint8_t)raw[i] >= 0x80) {
            onlyAscii = false;
            break;
        }
    }
    if (onlyAscii) {
        return raw;
    }
    /* 已是合法 UTF-8 多字节（如东财 JSON） */
    if (utf8LooksValid(raw)) {
        /* 粗判：若高字节像 GBK 双字节且非法 UTF-8 已在上面失败；
         * 合法 UTF-8 直接用 */
        return raw;
    }
    return gbkToUtf8(raw);
}
