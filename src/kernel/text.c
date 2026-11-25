#include "uos_text.h"

int uos_dec_u32(const char *s, uint32_t max, uint32_t *out)
{
    uint32_t n = 0;

    if (s == NULL || out == NULL || *s == '\0') {
        return -1;
    }
    for (; *s >= '0' && *s <= '9'; s++) {
        if (n > (max - (uint32_t)(*s - '0')) / 10u) {
            return -1;
        }
        n = n * 10u + (uint32_t)(*s - '0');
    }
    if (*s != '\0') {
        return -1;
    }
    *out = n;
    return 0;
}

void uos_quote_word(const char *in, char *out, size_t size)
{
    static const char hex[] = "0123456789ABCDEF";
    size_t o = 0;
    unsigned char c;

    if (out == NULL || size == 0) {
        return;
    }
    for (; in != NULL && *in != '\0'; in++) {
        c = (unsigned char)*in;
        if (c > ' ' && c < 0x7f && c != '%' && c != '"') {
            if (o + 1 >= size) {
                break;
            }
            out[o++] = (char)c;
        } else {
            if (o + 3 >= size) {
                break;
            }
            out[o++] = '%';
            out[o++] = hex[c >> 4];
            out[o++] = hex[c & 15];
        }
    }
    out[o] = '\0';
}
