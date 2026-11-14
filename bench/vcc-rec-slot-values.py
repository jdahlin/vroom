#!/usr/bin/env python3
"""Exact-cell value availability, store observation and reusable scratch contracts."""
import argparse
from pathlib import Path
import subprocess
import tempfile

from hostenv import R, llvm

SOURCE = r'''
#include <assert.h>
#include "codegen/rec/slot-values.c"

static void block_boundaries(record_slot_values *s)
{
    assert(record_slot_values_begin(s, 4));
    record_slot_value *a = record_slot_value_get(s, 1, 1, false);
    assert(!record_slot_value_is_available(a, 0, 0, false));
    assert(a->last == RECORD_SLOT_NO_STORE);
    assert(record_slot_value_note(a, false, false, 9, 3, 7, 10) == RECORD_SLOT_NO_STORE);
    assert(record_slot_value_is_available(a, 3, 7, false));
    /* An unrelated cell must not inherit the fact, even in the same block. */
    record_slot_value *b = record_slot_value_get(s, 2, 1, true);
    assert(!record_slot_value_is_available(b, 3, 7, false));
    assert(record_slot_value_note(b, true, false, 10, 4, 7, 11) == RECORD_SLOT_NO_STORE);
    /* A successor cannot forward from a predecessor without a reaching-value
     * proof. Its first store also cannot delete a predecessor's pending store. */
    a = record_slot_value_get(s, 1, 2, true);
    assert(a->value == RECORD_SLOT_NO_VALUE && a->last == RECORD_SLOT_NO_STORE);
    assert(!record_slot_value_is_available(a, 3, 7, false));
    record_slot_value_note(a, true, false, 11, 1, 8, 20);
    assert(a->loads == 1);
    a = record_slot_value_get(s, 1, 3, false);
    assert(record_slot_value_note(a, false, false, 12, 1, 8, 30) == RECORD_SLOT_NO_STORE);
    assert(a->loads == 1); /* Remaining loads are whole-function facts. */
    assert(!record_slot_value_is_dead(s, 1, false));
    assert(!record_slot_value_is_dead(s, 2, false));
}

static void value_and_memory_versions(record_slot_values *s)
{
    assert(record_slot_values_begin(s, 3));
    record_slot_value *a = record_slot_value_get(s, 1, 1, false);
    record_slot_value_note(a, false, false, 4, 15, 6, 1);
    assert(record_slot_value_is_available(a, 15, 6, false));
    /* Caller versions include implicit and partial definitions of the source.
     * Restoring the same register number alone must not make it available. */
    assert(!record_slot_value_is_available(a, 16, 6, false));
    assert(record_slot_value_is_available(a, 15, 99, false));
    assert(!record_slot_value_is_available(a, 15, 99, true));
    assert(record_slot_value_is_available(a, 15, 6, true));
    assert(!record_slot_value_is_available(a, 16, 6, true));
    /* A fresh memory read after a call/address store establishes a new fact. */
    a = record_slot_value_get(s, 1, 1, true);
    assert(record_slot_value_note(a, true, true, 8, 2, 99, 7) == RECORD_SLOT_NO_STORE);
    assert(record_slot_value_is_available(a, 2, 99, true));
    assert(!record_slot_value_is_available(a, 2, 100, true));
    assert(a->last == RECORD_SLOT_NO_STORE && a->loads == 1);
}

static void pending_stores(record_slot_values *s)
{
    assert(record_slot_values_begin(s, 3));
    record_slot_value *a = record_slot_value_get(s, 1, 1, false);
    assert(record_slot_value_note(a, false, false, 4, 1, 0, 10) == RECORD_SLOT_NO_STORE);
    a = record_slot_value_get(s, 1, 1, false);
    assert(record_slot_value_note(a, false, false, 5, 1, 0, 11) == 10);
    /* Even a successfully forwarded read observes its pending store. */
    a = record_slot_value_get(s, 1, 1, true);
    assert(record_slot_value_is_available(a, 1, 0, false));
    assert(a->last == RECORD_SLOT_NO_STORE);
    a->position = 12; /* Adapter's liveness extension for the removed load. */
    a = record_slot_value_get(s, 1, 1, false);
    assert(record_slot_value_note(a, false, false, 6, 1, 0, 13) == RECORD_SLOT_NO_STORE);
    /* A failed forwarding attempt also observes the preceding store. */
    a = record_slot_value_get(s, 1, 1, true);
    assert(!record_slot_value_is_available(a, 2, 0, false));
    record_slot_value_note(a, true, false, 7, 1, 0, 14);
    a = record_slot_value_get(s, 1, 1, false);
    assert(record_slot_value_note(a, false, false, 8, 1, 0, 15) == RECORD_SLOT_NO_STORE);
    a = record_slot_value_get(s, 1, 2, false);
    assert(record_slot_value_note(a, false, false, 9, 1, 0, 20) == RECORD_SLOT_NO_STORE);
    /* Escaped cells may be read through pointers between any two records. */
    a = record_slot_value_get(s, 2, 1, false);
    assert(record_slot_value_note(a, false, true, 4, 1, 1, 30) == RECORD_SLOT_NO_STORE);
    assert(record_slot_value_note(a, false, true, 5, 1, 1, 31) == RECORD_SLOT_NO_STORE);
    assert(a->last == RECORD_SLOT_NO_STORE);
    assert(!record_slot_value_is_dead(s, 2, true));
}

static void remaining_reads(record_slot_values *s)
{
    assert(record_slot_values_begin(s, 3));
    record_slot_value *a = record_slot_value_get(s, 1, 1, false);
    record_slot_value_note(a, false, false, 4, 1, 0, 1);
    a = record_slot_value_get(s, 1, 1, true);
    assert(record_slot_value_is_available(a, 1, 0, false));
    a->position = 2; /* Forwarded read: no memory read remains. */
    assert(a->loads == 0 && record_slot_value_is_dead(s, 1, false));
    /* A load in another block cannot use that local fact. Retaining even one
     * real load must retain this cell's stores throughout the function. */
    a = record_slot_value_get(s, 1, 2, true);
    assert(!record_slot_value_is_available(a, 1, 0, false));
    record_slot_value_note(a, true, false, 5, 1, 0, 8);
    assert(a->loads == 1 && !record_slot_value_is_dead(s, 1, false));
    a = record_slot_value_get(s, 1, 3, false);
    record_slot_value_note(a, false, false, 6, 1, 0, 10);
    assert(a->loads == 1 && !record_slot_value_is_dead(s, 1, false));
    assert(record_slot_value_is_dead(s, 2, false));
    assert(!record_slot_value_is_dead(s, 2, true));
}

static void large_value_ids(record_slot_values *s)
{
    const uint32_t ids[] = { 0, 31, 127, 128, 65536, 0x80000000u, UINT32_MAX - 1 };
    assert(record_slot_values_begin(s, 2));
    record_slot_value *a = record_slot_value_get(s, 1, 1, false);
    for (unsigned k = 0; k < sizeof(ids) / sizeof(*ids); ++k) {
        record_slot_value_note(a, false, false, ids[k], k + 1, 0, k + 10);
        assert(a->value == ids[k]);
        assert(record_slot_value_is_available(a, k + 1, 0, false));
        assert(!record_slot_value_is_available(a, k + 2, 0, false));
    }
    /* UINT32_MAX is reserved, never a usable wasm local or native value. */
    record_slot_value_note(a, true, false, RECORD_SLOT_NO_VALUE, 99, 0, 99);
    assert(!record_slot_value_is_available(a, 99, 0, false));
}

static void reset_reuse(record_slot_values *s)
{
    assert(record_slot_values_begin(s, 32));
    record_slot_value *buffer = s->values;
    uint32_t capacity = s->capacity;
    record_slot_value *a = record_slot_value_get(s, 31, 7, true);
    record_slot_value_note(a, true, false, 500, 9, 8, 99);
    assert(!record_slot_value_is_dead(s, 31, false));
    assert(record_slot_values_begin(s, 2));
    assert(s->count == 2 && s->capacity == capacity && s->values == buffer);
    a = record_slot_value_get(s, 1, 7, false);
    assert(a->value == RECORD_SLOT_NO_VALUE && a->last == RECORD_SLOT_NO_STORE);
    assert(a->loads == 0 && record_slot_value_is_dead(s, 1, false));
    /* Re-expose a previously populated cell without allocation. Both local
     * generation and whole-function remaining-read state must be reset. */
    assert(record_slot_values_begin(s, 32));
    assert(s->capacity == capacity && s->values == buffer);
    a = record_slot_value_get(s, 31, 7, false);
    assert(!record_slot_value_is_available(a, 9, 8, false));
    assert(a->loads == 0 && a->last == RECORD_SLOT_NO_STORE);
    assert(record_slot_value_is_dead(s, 31, false));
    assert(!record_slot_values_begin(s, 0));
    assert(!record_slot_values_begin(s, UINT32_MAX));
}

int main(void)
{
    record_slot_values s = {0};
    block_boundaries(&s);
    value_and_memory_versions(&s);
    pending_stores(&s);
    remaining_reads(&s);
    large_value_ids(&s);
    reset_reuse(&s);
    free(s.values);
    return 0;
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', default=llvm('clang'))
    parser.add_argument('--sanitize', action='store_true')
    parser.add_argument('--source-root', type=Path, default=Path(R),
                        help='checkout whose implementation is tested')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='vcc-rec-slot-values-') as tmp:
        source = Path(tmp) / 'slot-values.c'
        binary = Path(tmp) / 'slot-values'
        source.write_text(SOURCE)
        flags = ['-fsanitize=address,undefined', '-fno-omit-frame-pointer'] if args.sanitize else []
        subprocess.run([args.cc, '-std=gnu11', '-O1', '-g', '-I', str(args.source_root / 'src'), '-I', str(args.source_root / 'src' / 'include'),
                        *flags, str(source), '-o', str(binary)], check=True, timeout=30)
        subprocess.run([str(binary)], check=True, timeout=10)
    print('machine record exact-slot value contracts: PASS')


if __name__ == '__main__':
    main()
