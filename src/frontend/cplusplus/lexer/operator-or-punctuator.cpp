/*
 * operator-or-punctuator.cpp: [lex.operators] Operators and punctuators, with
 * the alternative tokens of [lex.digraph].
 *
 *     preprocessing-op-or-punc:
 *         preprocessing-operator
 *         operator-or-punctuator
 *
 * The table of spellings is operator-or-punctuator.inc.
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
#include "operator-or-punctuator.inc"
#undef PREPROCESSING_OP_OR_PUNC
};

/*
 * The rows of the table that start with one character, the longest spelling
 * first, so that the first row that matches the source is "the longest
 * sequence of characters that could constitute a preprocessing token"
 * ([lex.pptoken]/3.3).  Made from the table when this file is compiled: the
 * scanner runs for every operator and punctuator of a translation unit, and
 * comparing the source with all rows cost a third of a parse
 * (doc/RESEARCH-cxx-performance.md).
 */
struct op_or_punc_candidates {
    uint8_t count;
    uint8_t row[8];    /* indexes into op_or_punc_descriptions */
    uint8_t length[8]; /* of the row's spelling */
};

struct op_or_punc_index {
    op_or_punc_candidates starting_with[128]; /* by basic character */
    uint8_t longest;                          /* spelling of the table */
};

constexpr op_or_punc_index make_op_or_punc_index()
{
    op_or_punc_index index{};
    uint8_t rows = static_cast<uint8_t>(preprocessing_op_or_punc::count);
    for (uint8_t i = 0; i < rows; i++) {
        const char *spelling = op_or_punc_descriptions[i].spelling;
        uint8_t n = 0;
        while (spelling[n])
            n++;
        if (n > index.longest)
            index.longest = n;
        op_or_punc_candidates &c =
            index.starting_with[static_cast<unsigned char>(spelling[0])];
        /* keep the longest first; rows of one length keep the table's
           order (a row too many for row[] does not compile) */
        uint8_t at = c.count;
        while (at > 0 && c.length[at - 1] < n) {
            c.row[at] = c.row[at - 1];
            c.length[at] = c.length[at - 1];
            at--;
        }
        c.row[at] = i;
        c.length[at] = n;
        c.count++;
    }
    return index;
}

constexpr op_or_punc_index op_or_punc_by_first_character =
    make_op_or_punc_index();

/* scan_preprocessing_op_or_punc reads this many characters ahead */
constexpr uint8_t op_or_punc_lookahead = 4;
static_assert(
    op_or_punc_by_first_character.longest <= op_or_punc_lookahead,
    "a spelling of operator-or-punctuator.inc is longer than the scanner "
    "reads ahead");

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
 * Returns the end, 0 if no operator or punctuator starts at `at`.  The
 * candidates are the rows that start with the first character, tried longest
 * first against the translation characters, so a splice may fall anywhere
 * inside one.
 */
uint32_t lexer::scan_preprocessing_op_or_punc(uint32_t at,
                                              preprocessing_op_or_punc *op,
                                              bool *spelling_differs)
{
    /* the first character decides the candidates; most characters start
       no operator or punctuator, and most that do start only one */
    translation_character t = peek(at);
    if (!t.extent || t.character >= 128)
        return 0;
    const op_or_punc_candidates &candidates =
        op_or_punc_by_first_character.starting_with[t.character];
    if (!candidates.count)
        return 0;

    /* the characters a candidate could need, and where each ends; after <:
       the exception of [lex.pptoken]/3.2 looks at two more */
    int needed = candidates.length[0];
    if (t.character == '<')
        needed = op_or_punc_lookahead;
    unsigned char c[op_or_punc_lookahead] = {};
    uint32_t end[op_or_punc_lookahead] = {};
    uint32_t q = at;
    for (int i = 0; i < needed; i++) {
        if (i > 0)
            t = peek(q);
        if (t.extent)
            c[i] = t.character;
        q += t.extent;
        end[i] = q;
    }

    int best = -1;
    size_t best_length = 0;
    for (uint8_t k = 0; k < candidates.count; k++) {
        int i = candidates.row[k];
        size_t n = candidates.length[k];
        const char *s = op_or_punc_descriptions[i].spelling;
        bool matches = true;
        for (size_t j = 1; j < n; j++)
            if (s[j] != c[j])
                matches = false;
        if (!matches)
            continue;
        /* <=> is C++20's; before it, <= and > ([diff.cpp17.expr]) */
        if (i == static_cast<int>(preprocessing_op_or_punc::SPACESHIP) &&
            !behavior.has(language_feature::three_way_comparison_operator))
            continue;
        /* .* and ->* are operators, but clang's raw lexer forms them too */
        best = i;
        best_length = n;
        break;
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
