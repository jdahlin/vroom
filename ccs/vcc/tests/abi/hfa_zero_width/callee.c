#include "types.h"
struct complex_float cf_roundtrip(struct complex_float x) { x.x += 2.0f + 3.0fi; return x; }
struct doubles doubles_roundtrip(struct doubles x) { x.x += 5; x.y += 7; return x; }
struct padded padded_roundtrip(struct padded x) { x.x += 11; x.y += 13; return x; }
struct bits bits_roundtrip(struct bits x) { x.x += 17; x.b ^= 1; return x; }
