/* Shim for macOS: glibc-compatible <endian.h> */
#ifndef VROOM_ENDIAN_H
#define VROOM_ENDIAN_H
#include <machine/endian.h>
#ifndef __BYTE_ORDER
#define __BYTE_ORDER    __BYTE_ORDER__
#define __LITTLE_ENDIAN __ORDER_LITTLE_ENDIAN__
#define __BIG_ENDIAN    __ORDER_BIG_ENDIAN__
#endif
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define htobe16(x) __builtin_bswap16(x)
#define htobe32(x) __builtin_bswap32(x)
#define htobe64(x) __builtin_bswap64(x)
#define htole16(x) (x)
#define htole32(x) (x)
#define htole64(x) (x)
#else
#define htobe16(x) (x)
#define htobe32(x) (x)
#define htobe64(x) (x)
#define htole16(x) __builtin_bswap16(x)
#define htole32(x) __builtin_bswap32(x)
#define htole64(x) __builtin_bswap64(x)
#endif
#define be16toh(x) htobe16(x)
#define be32toh(x) htobe32(x)
#define be64toh(x) htobe64(x)
#define le16toh(x) htole16(x)
#define le32toh(x) htole32(x)
#define le64toh(x) htole64(x)
#endif
