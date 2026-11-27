/*
 * mangling/encoder.cpp: the encoder's candidate table and its marks
 * (mangling-internal.h); the bytes are frontend/itanium/writer.h's
 * (doc/PLAN-mangling.md).
 */
#include "mangling-internal.h"

namespace cplusplus
{

itanium_encoder::mark itanium_encoder::position() const
{
    return {out->n, candidates.size()};
}

void itanium_encoder::rewind(mark m)
{
    out->n = m.bytes;
    candidates.resize(m.candidates);
}

} // namespace cplusplus
