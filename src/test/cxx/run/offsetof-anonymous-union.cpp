/* GCC manual 6.53, offsetof, of a member of an anonymous union or struct
   ([class.union.anon]/1): its offset is from the start of the enclosing class,
   past the unnamed object's own offset, whatever the nesting.  The record of
   the code generator (codegen/rec/record.h), that read a jump's label from
   offset 0 when vcc compiled itself. */
#include <stddef.h>
#include <stdint.h>
struct record {
    uint8_t opcode, size;
    uint8_t registers[4];
    uint8_t condition, shift;
    union {
        int64_t immediate;
        struct {
            int32_t offset;
            uint32_t reference;
        };
        uint32_t label;
    };
};
struct nested {
    char c;
    union {
        short s;
        union {
            struct {
                char pad;
                int deep;
            };
            long l;
        };
    };
    int after;
};
int main()
{
    int bad = 0;
    bad += offsetof(record, opcode) != 0;
    bad += offsetof(record, immediate) != 8;
    bad += offsetof(record, offset) != 8;
    bad += offsetof(record, reference) != 12;
    bad += offsetof(record, label) != 8;
    bad += offsetof(nested, s) != 8;
    bad += offsetof(nested, l) != 8;
    bad += offsetof(nested, pad) != 8;
    bad += offsetof(nested, deep) != 12;
    bad += offsetof(nested, after) != 16;
    record r = {};
    r.label = 7;
    bad += *reinterpret_cast<uint32_t *>(reinterpret_cast<char *>(&r) +
                                         offsetof(record, label)) != 7;
    return bad;
}
