/*
 * lex.operators.cpp: [lex.operators] Operators and punctuators, with the
 * alternative tokens of [lex.digraph].
 *
 *     preprocessing-op-or-punc:
 *         preprocessing-operator
 *         operator-or-punctuator
 *
 * The table of spellings is lex.operators.inc.
 */
#include "../cplusplus-internal.h"

namespace cplusplus
{

namespace
{

struct op_or_punc_description {
    const char *spelling;
    preprocessing_op_or_punc primary;
    const char *clang_name;
};

constexpr op_or_punc_description op_or_punc_descriptions[] = {
#define PREPROCESSING_OP_OR_PUNC(name, spelling, primary, clang_name)          \
    {spelling, preprocessing_op_or_punc::primary, clang_name},
#include "lex.operators.inc"
#undef PREPROCESSING_OP_OR_PUNC
};

} // namespace

const char *preprocessing_op_or_punc_spelling(preprocessing_op_or_punc op)
{
    return op_or_punc_descriptions[static_cast<int>(op)].spelling;
}

/* [lex.digraph]/2: "each alternative token behaves the same, respectively,
   as its primary token, except for its spelling" */
preprocessing_op_or_punc
preprocessing_op_or_punc_primary(preprocessing_op_or_punc op)
{
    return op_or_punc_descriptions[static_cast<int>(op)].primary;
}

VPRIV const char *
preprocessing_op_or_punc_clang_name(preprocessing_op_or_punc op)
{
    return op_or_punc_descriptions[static_cast<int>(op)].clang_name;
}

/*
 * The longest preprocessing-op-or-punc at `at` ([lex.pptoken]/3.3, "the
 * longest sequence of characters that could constitute a preprocessing
 * token"), with the exception of [lex.pptoken]/3.2:
 *
 *   "Otherwise, if the next three characters are <:: and the subsequent
 *   character is neither : nor >, the < is treated as a preprocessing token
 *   by itself and not as the first character of the alternative token <:."
 *
 * Returns the end, 0 if no operator or punctuator starts at `at`.  Every
 * candidate is tried longest first against the translation characters, so a
 * splice may fall anywhere inside one.
 */
uint32_t lexer::scan_preprocessing_op_or_punc(uint32_t at,
                                              preprocessing_op_or_punc *op,
                                              bool *spelling_differs)
{
    unsigned char c[5];
    uint32_t end[5];
    uint32_t q = at;
    for (int i = 0; i < 4; i++) {
        translation_character t = peek(q);
        c[i] = t.extent ? t.character : 0;
        q += t.extent;
        end[i] = q;
    }
    c[4] = 0;

    int best = -1;
    size_t best_length = 0;
    for (int i = 0; i < static_cast<int>(preprocessing_op_or_punc::count);
         i++) {
        const char *s = op_or_punc_descriptions[i].spelling;
        size_t n = strlen(s);
        if (n <= best_length || memcmp(s, c, n))
            continue;
        /* <=> is C++20's; before it, <= and > ([diff.cpp17.expr]) */
        if (i == static_cast<int>(preprocessing_op_or_punc::SPACESHIP) &&
            !behavior.has(language_feature::three_way_comparison_operator))
            continue;
        /* .* and ->* are operators, but clang's raw lexer forms them too */
        best = i;
        best_length = n;
    }
    if (best < 0)
        return 0;
    /* [lex.pptoken]/3.2: <:: not followed by : or > */
    if (best == static_cast<int>(preprocessing_op_or_punc::LESS_COLON) &&
        c[2] == ':' && c[3] != ':' && c[3] != '>' &&
        behavior.has(language_feature::less_colon_colon_is_less)) {
        best = static_cast<int>(preprocessing_op_or_punc::LESS);
        best_length = 1;
    }
    *op = static_cast<preprocessing_op_or_punc>(best);
    uint32_t stop = end[best_length - 1];
    if (stop - at != best_length) {
        *spelling_differs = true;
        for (uint32_t r = at; r < stop;)
            r += read(r).extent;
    }
    return stop;
}

} // namespace cplusplus
