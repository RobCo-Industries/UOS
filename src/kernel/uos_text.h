/*
 * Small text helpers for maintenance commands and markers. Pure:
 * src/kernel/text.c. Any context.
 */
#ifndef UOS_TEXT_H
#define UOS_TEXT_H

#include <stddef.h>
#include <stdint.h>

/*
 * Reads a decimal number of digits only, at most `max`, into *out.
 * Returns 0, or -1 for NULL, an empty text, another character or a value
 * over `max`; *out is then unchanged.
 */
int uos_dec_u32(const char *s, uint32_t max, uint32_t *out);

/*
 * Copies `in` to `out` as one marker word: printable ASCII other than the
 * blank, '%' and '"' as is, every other byte as %XX, as the console's
 * markers do. Cut before a character that does not fit. NULL is "".
 */
void uos_quote_word(const char *in, char *out, size_t size);

#endif
