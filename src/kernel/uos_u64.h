/*
 * Decimal text of an unsigned 64-bit value. The library's k_printf has no
 * 64-bit conversion, and the target has no 64-bit division helper, so this
 * pure function uses subtraction only. Any context.
 */
#ifndef UOS_U64_H
#define UOS_U64_H

#include <stddef.h>
#include <stdint.h>

/* Characters of the longest value, 18446744073709551615, plus the NUL. */
#define UOS_U64_DEC_SIZE 21u

/*
 * Writes `v` in decimal with no leading zeros and a NUL to buf. Returns the
 * digit count, or 0 when buf is NULL or size cannot hold the text and its
 * NUL; buf is then left empty when size is not 0.
 */
size_t uos_u64_dec(uint64_t v, char *buf, size_t size);

#endif
