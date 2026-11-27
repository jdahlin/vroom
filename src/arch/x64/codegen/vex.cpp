/* In a function whose target has AVX, the compiler's own SSE instructions are
 * written in their VEX forms, as clang writes them: a legacy SSE instruction
 * keeps bits 128 and up of its destination, which after 256- or 512-bit code
 * costs a dependency on them (a loop mixing the two ran three times slower).
 * The rewrite is of the printed text; the user's inline asm is not touched. */
#include <stdlib.h>
#include <string.h>
#include "common/fail.h"
#include "common/scoped-cleanup.h"
#include "x64-internal.h"

enum {
    X64_VEX_SAME,  /* the same operands */
    X64_VEX_THREE, /* the destination also the first source */
    X64_VEX_MERGE  /* THREE between registers (movss) or on a load (movlps) */
};

static const struct {
    const char *name;
    int form;
} x64_vex_forms[] = {
#define VEX_SAME(n) {n, X64_VEX_SAME},
#define VEX_THREE(n) {n, X64_VEX_THREE},
#define VEX_MERGE(n) {n, X64_VEX_MERGE},
#include "arch/x64/vexforms.h"
#undef VEX_SAME
#undef VEX_THREE
#undef VEX_MERGE
};

/* one line [line, end) appended to b, rewritten when it is an SSE instruction
   on xmm registers */
static void x64_vex_line(tbuf *b, const char *line, const char *end)
{
    const char *name = line, *stop = end;

    while (name < end && (*name == '\t' || *name == ' '))
        name++;
    const char *operands = name;

    while (operands < end && *operands != '\t' && *operands != ' ')
        operands++;
    size_t length = (size_t)(operands - name);
    const char *comment =
        (const char *)memchr(operands, '#', (size_t)(end - operands));

    if (comment)
        stop = comment;
    while (stop > operands && (stop[-1] == ' ' || stop[-1] == '\t'))
        stop--;
    /* the operands: an xmm register among them, the commas outside
       parentheses, where the last one begins */
    bool xmm = false, memory = false, immediate = false;
    const char *last = operands;
    unsigned count = 1;
    int depth = 0;

    for (const char *p = operands; p < stop; p++) {
        if (*p == '(')
            depth++, memory = true;
        else if (*p == ')')
            depth--;
        else if (*p == ',' && !depth)
            count++, last = p + 1;
        else if (*p == '$')
            immediate = true;
        else if (stop - p >= 4 && !memcmp(p, "%xmm", 4))
            xmm = true;
    }
    int form = -1;

    if (xmm && length)
        for (size_t i = 0;
             i < sizeof x64_vex_forms / sizeof *x64_vex_forms && form < 0; i++)
            if (strlen(x64_vex_forms[i].name) == length &&
                !memcmp(x64_vex_forms[i].name, name, length))
                form = x64_vex_forms[i].form;
    if (form < 0) {
        vtext_append(b, line, (size_t)(end - line));
        return;
    }
    while (last < stop && (*last == ' ' || *last == '\t'))
        last++;
    if (form == X64_VEX_MERGE) {
        /* between registers, or a load into half a register */
        bool registers =
            !memory && memchr(operands, '%', (size_t)(stop - operands));
        bool load = memory && last < stop && *last == '%';
        bool half = name[3] == 'l' || name[3] == 'h';

        form = X64_VEX_SAME;
        if (count == 2 && ((registers && !half) || (load && half)))
            form = X64_VEX_THREE;
    }
    /* (two operands, or three with an immediate first) */
    if (form == X64_VEX_THREE && count != 2u + immediate) {
        vtext_append(b, line, (size_t)(end - line));
        return;
    }
    vtext_append(b, line, (size_t)(name - line));
    vtext_char_append(b, 'v');
    vtext_append(b, name, (size_t)(stop - name));
    if (form == X64_VEX_THREE) {
        vtext_string_append(b, ", ");
        vtext_append(b, last, (size_t)(stop - last));
    }
    vtext_append(b, stop, (size_t)(end - stop));
}

/* the assembly b holds from `from`, in VEX forms */
VPRIV void x64_vex_rewrite(tbuf *b, size_t from)
{
    size_t length = b->n - from;

    if (!length)
        return;
    VAUTOFREE char *copy = (char *)malloc(length);

    if (!copy)
        vout_of_memory();
    memcpy(copy, b->p + from, length);
    b->n = from;
    for (const char *p = copy, *end = copy + length; p < end;) {
        const char *line = (const char *)memchr(p, '\n', (size_t)(end - p));
        const char *next = end;

        if (line)
            next = line + 1;
        else
            line = end;
        x64_vex_line(b, p, line);
        vtext_append(b, line, (size_t)(next - line));
        p = next;
    }
}
