/*
 * WebAssembly build (wasm/build.sh puts this directory first): the ACLE CRC32C
 * intrinsics vcc hashes strings with come from emscripten's NEON header
 * (SIMDe), which defines __crc32cb/__crc32cd in software, same values.
 */
#include <arm_neon.h>
