/*
 * Numbers as text, without stdio: the digits of an unsigned value in decimal
 * or lower-case hexadecimal, and the value of one hexadecimal digit.
 *
 *   vformat_unsigned(buf, v)   decimal digits of v at buf (at most 20 bytes),
 *                              no NUL; returns the count
 *   vformat_hex(buf, v)        lower-case hex digits, no 0x, at least one
 *                              (at most 16 bytes), no NUL; returns the count
 *   vhex_digit_value(c)        0..15 for 0-9, a-f and A-F, else -1
 */
#ifndef VCC_COMMON_FORMAT_H
#define VCC_COMMON_FORMAT_H

#include <stddef.h>
#include <stdint.h>

static inline size_t vformat_unsigned(char *buf, uint64_t v)
{
	char t[20];
	size_t i = sizeof t, n;

	do
		t[--i] = (char)('0' + v % 10);
	while ((v /= 10));
	n = sizeof t - i;
	for (size_t k = 0; k < n; k++)
		buf[k] = t[i + k];
	return n;
}

static inline size_t vformat_hex(char *buf, uint64_t v)
{
	size_t n = v ? (size_t)(67 - __builtin_clzll(v)) >> 2 : 1;

	for (size_t i = n; i-- > 0; v >>= 4)
		buf[i] = "0123456789abcdef"[v & 15];
	return n;
}

static inline int vhex_digit_value(int c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	c |= 0x20;
	return c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
}

#endif
