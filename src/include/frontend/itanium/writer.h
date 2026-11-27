/*
 * frontend/itanium/writer.h: the bytes of an Itanium C++ ABI mangled name,
 * shared by the C++ front end's mangler (frontend/cplusplus/abi/itanium/
 * mangling/) and the C front end's overloadable functions
 * (frontend/c/mangle.cpp).  The tables beside it are the one place each
 * letter is spelled: operators.inc ([abi:mangling-operator]),
 * builtin-types.inc ([abi:mangling-builtin]) and productions.inc (every
 * other terminal, by production).  The writer reads no front end's types;
 * what the letters mean is the mangler's to decide (doc/PLAN-mangling.md).
 */
#ifndef VCC_FRONTEND_ITANIUM_WRITER_H
#define VCC_FRONTEND_ITANIUM_WRITER_H

#include "common/buffer.h"
#include "common/format.h"
#include <stdint.h>
#include <string.h>

namespace itanium
{

/* the terminals of productions.inc */
enum class terminal : uint8_t {
#define ITANIUM_TERMINAL(name, code) name,
#include "productions.inc"
#undef ITANIUM_TERMINAL
};

/* the operators of operators.inc */
enum class operator_name : uint8_t {
#define ITANIUM_OPERATOR(name, code, arity, token) name,
#include "operators.inc"
#undef ITANIUM_OPERATOR
    none
};

/* the builtin types of builtin-types.inc, by the C++ front end's
   fundamental_type names */
enum class builtin : uint8_t {
#define ITANIUM_BUILTIN_TYPE(type, code) type,
#include "builtin-types.inc"
#undef ITANIUM_BUILTIN_TYPE
    none
};

inline const char *code(terminal t)
{
    static const char *const codes[] = {
#define ITANIUM_TERMINAL(name, code) code,
#include "productions.inc"
#undef ITANIUM_TERMINAL
    };
    return codes[static_cast<size_t>(t)];
}

inline const char *code(operator_name o)
{
    static const char *const codes[] = {
#define ITANIUM_OPERATOR(name, code, arity, token) code,
#include "operators.inc"
#undef ITANIUM_OPERATOR
    };
    return codes[static_cast<size_t>(o)];
}

inline const char *code(builtin b)
{
    static const char *const codes[] = {
#define ITANIUM_BUILTIN_TYPE(type, code) code,
#include "builtin-types.inc"
#undef ITANIUM_BUILTIN_TYPE
    };
    return codes[static_cast<size_t>(b)];
}

/* the writer: terminals, numbers, <source-name>s, <seq-id>s and
   <substitution>s appended to a buffer */
class writer
{
  public:
    explicit writer(vbuffer *out) : out(out)
    {
    }
    void write(terminal t)
    {
        vbuffer_string_append(out, code(t));
    }
    void write(operator_name o)
    {
        vbuffer_string_append(out, code(o));
    }
    void write(builtin b)
    {
        vbuffer_string_append(out, code(b));
    }
    /* [abi:mangle.number] <number> ::= [n] <non-negative decimal integer> */
    void number(uint64_t n)
    {
        vbuffer_format_append(out, "%llu", static_cast<unsigned long long>(n));
    }
    void value_number(bool negative, uint64_t magnitude)
    {
        if (negative)
            write(terminal::n_negative);
        number(magnitude);
    }
    void signed_number(int64_t n)
    {
        uint64_t magnitude = static_cast<uint64_t>(n);
        if (n < 0)
            magnitude = 0 - magnitude;
        value_number(n < 0, magnitude);
    }
    /* [abi:mangle.name] <source-name> ::= <positive length number> <identifier> */
    void source_name(const char *bytes, size_t length)
    {
        number(length);
        vbuffer_append(out, bytes, length);
    }
    void source_name(const char *identifier)
    {
        source_name(identifier, strlen(identifier));
    }
    /* the [<number>] _ of several productions: nothing before the _ for
       the first (n 0), n - 1 for another (T_, T0_, Ut_, Ut0_, ...) */
    void ordinal_underscore(uint64_t n)
    {
        if (n)
            number(n - 1);
        write(terminal::underscore);
    }
    /* [abi:mangling-scope] <discriminator> ::= _ <non-negative number>,
       and __ <number> _ for one of 10 or more */
    void discriminator(uint64_t n)
    {
        if (n < 10) {
            write(terminal::underscore);
            number(n);
            return;
        }
        write(terminal::long_discriminator);
        number(n);
        write(terminal::underscore);
    }
    /* [abi:mangle.seq-id] <seq-id> ::= <0-9A-Z>+, base 36, no leading 0 */
    void seq_id(size_t n)
    {
        char digits[16];
        size_t count = 0;
        do {
            unsigned d = static_cast<unsigned>(n % 36);
            char c = static_cast<char>('0' + d);
            if (d >= 10)
                c = static_cast<char>('A' + d - 10);
            digits[count++] = c;
            n /= 36;
        } while (n);
        while (count)
            vbuffer_append(out, &digits[--count], 1);
    }
    /* [abi:mangling-compression] <substitution> ::= S_ | S <seq-id> _, of
       candidate i: S_ the first, S <i-1 in base 36> _ another */
    void substitution(size_t i)
    {
        write(terminal::S_substitution);
        if (i)
            seq_id(i - 1);
        write(terminal::underscore);
    }
    /* a symbol that is not mangled: its identifier as it is */
    void unmangled(const char *bytes, size_t length)
    {
        vbuffer_append(out, bytes, length);
    }

  protected:
    vbuffer *out;
};

} // namespace itanium

#endif
