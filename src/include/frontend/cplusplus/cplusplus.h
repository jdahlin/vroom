/*
 * cplusplus.h: the C++ front end written against the standard.
 *
 * Every type, function and variable here is named after the term the
 * standard uses for it (docs/cxx-spec, n4861), and the code is organised as
 * the standard is: one directory per clause, one file per subclause
 * (src/frontend/cplusplus/lex/lex.icon.cpp is [lex.icon]).  A comment that
 * cites "[label]/N" refers to paragraph N of docs/cxx-spec/.../label.md;
 * "[label]/N.M" is item M of the list in that paragraph.  Grammar quoted in a
 * comment is the spec's, with "_opt" for its subscript opt.
 * (A checker of every citation and quoted production is planned:
 * doc/PLAN-cxx-spec.md.)
 *
 * Where clang (the reference implementation) chooses differently from n4861,
 * because of a later paper adopted as a defect report or an extension, the
 * comment says so and names the paper.
 *
 * C++20 without exceptions, RTTI or the standard library.
 */
#ifndef VCC_FRONTEND_CPLUSPLUS_H
#define VCC_FRONTEND_CPLUSPLUS_H

#include <stddef.h>
#include <stdint.h>

#include "common/buffer.h"

namespace cplusplus
{

/* ---- [intro.scope], [intro.defs], [intro.compliance] ------------------- */

/* [intro.scope]/1: "This document specifies requirements for implementations
   of the C++ programming language."  The editions, by year of publication. */
enum class standard_edition : uint16_t {
    cplusplus_98 = 1998,
    cplusplus_11 = 2011,
    cplusplus_14 = 2014,
    cplusplus_17 = 2017,
    cplusplus_20 = 2020,
    cplusplus_23 = 2023,
    cplusplus_26 = 2026,
    never = 0xFFFF,
};

/* [diff]: the changes between editions the front end asks about, named
   (frontend/cplusplus/diff/language-features.inc has each one's edition and
   where the standard records it) */
enum class language_feature : uint8_t {
#define LANGUAGE_FEATURE(name, since, until, reference) name,
#include "../../../frontend/cplusplus/diff/language-features.inc"
#undef LANGUAGE_FEATURE
    count
};
const char *language_feature_name(language_feature feature);
const char *language_feature_reference(language_feature feature);

/*
 * [intro.defs] "implementation-defined behavior: behavior, for a well-formed
 * program construct and correct data, that depends on the implementation and
 * that each implementation documents".  This structure is that documentation:
 * every choice the front end makes that the standard leaves to the
 * implementation, plus the extensions ([intro.compliance]/8) that are on.
 */
struct implementation_defined_behavior {
    standard_edition edition = standard_edition::cplusplus_17;
    /* [intro.compliance]/8 extensions: GNU (-std=gnu++NN), Microsoft */
    bool gnu_extensions = true;
    bool microsoft_extensions = false;
    bool microsoft_compatibility = false;
    /* [lex.phases]/1.1: trigraphs are part of the phase 1 mapping before C++17 */
    bool trigraphs = false;
    /* [lex.name]/1: '$' is an extended character allowed in identifiers */
    bool dollar_in_identifier = true;
    /* [basic.fundamental]/9: char8_t is a keyword from C++20, or by -fchar8_t */
    bool char8_t_keyword = false;
    /* [basic.fundamental]/7: whether plain char is signed */
    bool plain_char_is_signed = false;
    /* [basic.fundamental]/1: the widths in bits of the standard integer types */
    uint8_t int_width = 32, long_width = 64, long_long_width = 64;
    /* [basic.fundamental]/12: "The value representation of floating-point
       types is implementation-defined": IEEE binary32, binary64 and, for
       long double, binary128 (aarch64 Linux; 52 where it is binary64) */
    uint8_t long_double_fraction_bits = 112;
    /* the widest _BitInt(N) (C23's BITINT_MAXWIDTH), clang's extension */
    uint16_t bit_precise_maximum_width = 128;
    /* [basic.fundamental]/8: wchar_t's width and signedness */
    uint8_t wchar_t_width = 32;
    bool wchar_t_is_signed = false;

    /* the choices of clang -cc1 -triple aarch64-unknown-linux-gnu -std=... */
    static implementation_defined_behavior for_edition(standard_edition edition,
                                                       bool gnu_extensions);
    /* whether the edition in effect has a feature ([diff]) */
    bool has(language_feature feature) const;
};

/* ---- source files: [lex.separate], [lex.phases], [cpp.line] ------------ */

/* [lex.separate]/1: "The text of the program is kept in units called source
   files".  A source location is the offset of a physical source file
   character in its source file (one source file per translation for now). */
using source_location = uint32_t;

struct source_position {
    const char *file_name;
    uint32_t line, column;
};

struct source_file {
    const char *name = nullptr;
    /* [lex.phases]/1.1: the physical source file characters, followed by 16
       zero bytes so that a reader may look ahead without checking the end */
    const unsigned char *physical_characters = nullptr;
    uint32_t size = 0;

    bool open(const char *path);
    void from_memory(const char *name, const char *text, size_t size);
    void close();
    /* the position a diagnostic reports: physical line and column, or the
       presumed line and file name a line directive gave ([cpp.line]/3-4) */
    source_position position(source_location location) const;
    void add_line_directive(source_location next_line, uint32_t line,
                            const char *presumed_name);

  private:
    uint32_t *physical_line_starts = nullptr;
    uint32_t physical_line_count = 0;
    struct presumed_line {
        uint32_t physical_line, presumed_line;
        const char *presumed_name;
    } *presumed_lines = nullptr;
    uint32_t presumed_line_count = 0;
    char *owned = nullptr;
    void index_lines();
};

/* ---- diagnostics: [intro.compliance] ---------------------------------- */

enum class diagnostic_id : uint16_t {
#define DIAGNOSTIC(name, class, mapping, group, format) name,
#include "../../../frontend/cplusplus/intro/diagnostic-messages.inc"
#undef DIAGNOSTIC
    count
};

enum class diagnostic_severity : uint8_t {
    ignored,
    note,
    remark,
    warning,
    error,
    fatal
};

/* an argument of a message: an integer, or text the message keeps a copy of
   (the lexer's spellings are often in a buffer it reuses) */
struct diagnostic_argument {
    bool is_integer;
    uint8_t length;
    int64_t integer;
    char text[46];
};

/* [intro.defs] "diagnostic message: message belonging to an
   implementation-defined subset of the implementation's output messages" */
struct diagnostic_message {
    diagnostic_id id;
    diagnostic_severity severity;
    source_location location;
    uint8_t argument_count = 0;
    diagnostic_argument arguments[4];

    diagnostic_message &operator<<(int64_t value);
    diagnostic_message &operator<<(int value)
    {
        return *this << (int64_t)value;
    }
    diagnostic_message &text(const char *text, size_t length);
    diagnostic_message &operator<<(const char *text);
};

/* how the command line maps diagnostics to severities (clang's -W, -pedantic) */
struct diagnostic_options {
    bool pedantic = false, pedantic_errors = false, warnings_as_errors = false,
         no_warnings = false;
    /* -W<group> (true) and -Wno-<group> (false), in command line order */
    struct group_setting {
        const char *group;
        bool enabled;
    } group_settings[32];
    unsigned group_setting_count = 0;

    /* one command line argument; false if it is not a diagnostic option */
    bool parse(const char *argument);
    diagnostic_severity severity(diagnostic_id id) const;
};

/* where diagnostic messages go: [intro.compliance]/2.2 "a conforming
   implementation shall issue at least one diagnostic message" */
class diagnostic_consumer
{
  public:
    explicit diagnostic_consumer(const diagnostic_options &options)
        : options(options)
    {
    }
    ~diagnostic_consumer();
    const diagnostic_options &options;
    const source_file *file = nullptr;
    diagnostic_message *messages = nullptr;
    size_t message_count = 0, error_count = 0, warning_count = 0;
    /* while positive, issue() discards: the lexer looks ahead without consuming */
    unsigned suppressed = 0;

    /* a diagnostic message to which the caller adds the arguments; a
       message that maps to "ignored" is discarded when the next is issued */
    diagnostic_message &issue(diagnostic_id id, source_location location);
    /* file:line:column: severity: text, as clang prints it */
    void render(const diagnostic_message &message, vbuffer *out) const;

  private:
    size_t capacity = 0;
    diagnostic_message discarded;
    void count_last();
};

const char *diagnostic_name(diagnostic_id id);
const char *diagnostic_group(diagnostic_id id);
/* clang's format with the arguments substituted */
void diagnostic_text(const diagnostic_message &message, vbuffer *out);

/* ---- [lex.pptoken] ----------------------------------------------------- */

enum class preprocessing_token_kind : uint8_t {
    header_name,
    import_keyword,
    module_keyword,
    export_keyword,
    identifier,
    pp_number,
    character_literal,
    user_defined_character_literal,
    string_literal,
    user_defined_string_literal,
    preprocessing_op_or_punc,
    /* "each non-white-space character that cannot be one of the above" */
    other_character,
    /* not preprocessing tokens; returned only when asked for
       ([lex.phases]/1.3 leaves retaining white space unspecified) */
    white_space,
    comment,
    end_of_file,
};

/* [lex.ccon]/1, [lex.string]: encoding-prefix */
enum class encoding_prefix : uint8_t { none, u8, u, U, L };

/* [lex.operators]: preprocessing-op-or-punc, in the order of the grammar; a
   digraph of [lex.digraph] Table 1 is an entry of its own whose primary token
   the table records */
enum class preprocessing_op_or_punc : uint8_t {
#define PREPROCESSING_OP_OR_PUNC(name, spelling, primary, clang_name) name,
#include "../../../frontend/cplusplus/lex/lex.operators.inc"
#undef PREPROCESSING_OP_OR_PUNC
    count
};

struct preprocessing_token {
    preprocessing_token_kind kind = preprocessing_token_kind::end_of_file;
    /* the first preprocessing token on its logical source line ([cpp.pre]/1) */
    bool at_start_of_line = false;
    /* preceded by white space ([cpp.stringize]/2) */
    bool after_white_space = false;
    /* the physical characters differ from the spelling: a splice
       ([lex.phases]/1.2) or a trigraph ([lex.phases]/1.1) lies inside */
    bool spelling_differs = false;
    /* [lex.string]/1: "A string-literal that has an R in the prefix is a raw
       string literal" */
    bool raw = false;
    encoding_prefix prefix = encoding_prefix::none;
    preprocessing_op_or_punc op_or_punc = preprocessing_op_or_punc::count;
    source_location location = 0; /* the first physical character */
    uint32_t extent = 0;          /* the number of physical characters */
    /* [lex.ext]: where the ud-suffix starts in the physical characters (0: none) */
    uint32_t ud_suffix_offset = 0;
};

struct lexer_options {
    /* return white space and comments as tokens (clang's -dump-raw-tokens) */
    bool retain_white_space = false, retain_comments = false;
};

/* ---- [lex.token], phase 7 --------------------------------------------- */

/* [basic.fundamental]: the types a literal can have */
enum class fundamental_type : uint8_t {
    none,
    bool_,
    char_,
    char8_t_,
    char16_t_,
    char32_t_,
    wchar_t_,
    int_,
    unsigned_int,
    long_int,
    unsigned_long_int,
    long_long_int,
    unsigned_long_long_int,
    float_,
    double_,
    long_double,
    std_nullptr_t,
    /* an extended floating-point type ([basic.fundamental]/12): _Float16 */
    float16,
    /* an extended integer type ([basic.fundamental]/5): C23's _BitInt(N),
       clang's extension in C++; N is in the token */
    bit_precise_int,
    unsigned_bit_precise_int,
};

const char *fundamental_type_name(fundamental_type type);
unsigned fundamental_type_width(const implementation_defined_behavior &behavior,
                                fundamental_type type);
bool fundamental_type_is_signed(const implementation_defined_behavior &behavior,
                                fundamental_type type);

/* [lex.token]/1: "There are five kinds of tokens: identifiers, keywords,
   literals, operators, and other separators." */
enum class token_kind : uint8_t {
    identifier,
    keyword,
    literal,
    operator_or_punctuator,
    /* [lex.pptoken]/1 is violated: the preprocessing token has none of the forms */
    invalid,
    end_of_file,
};

/* [lex.literal.kinds]/1 */
enum class literal_kind : uint8_t {
    none,
    integer_literal,
    character_literal,
    floating_point_literal,
    string_literal,
    boolean_literal,
    pointer_literal,
    user_defined_literal,
};

struct token {
    token_kind kind = token_kind::end_of_file;
    literal_kind literal = literal_kind::none;
    fundamental_type type = fundamental_type::none; /* of a literal */
    preprocessing_op_or_punc op_or_punc = preprocessing_op_or_punc::count;
    int16_t keyword = -1; /* index in the keyword table ([lex.key]) */
    preprocessing_token
        preprocessing; /* the preprocessing token it came from */
    /* a string literal: its code units in lexer::string_code_units(),
       without the terminating '\0' of [lex.string]/12 */
    uint32_t string_offset = 0, string_length = 0;
    /* the bits of a floating-point literal's value in its format
       (binary16/32/64/128), when value_known */
    uint64_t floating_bits_low = 0, floating_bits_high = 0;
    /* N of _BitInt(N) */
    uint16_t bit_precise_width = 0;
    /* the value of an integer or boolean literal, when value_known */
    uint64_t value = 0;
    bool value_known = false;
};

/* [lex.phases]/1.3: decomposes a source file into preprocessing tokens */
class lexer
{
  public:
    lexer(const source_file &file,
          const implementation_defined_behavior &behavior,
          diagnostic_consumer &diagnostics, lexer_options options = {});
    ~lexer();
    lexer(const lexer &) = delete;
    lexer &operator=(const lexer &) = delete;

    /* [lex.pptoken]/3: the next preprocessing token */
    preprocessing_token next();
    /* [lex.pptoken]/3.3: the same, but a header-name is formed where one
       can be (after #include or import, or in a has-include-expression) */
    preprocessing_token next_allowing_header_name();
    /* the spelling of a preprocessing token: its characters after phases 1
       and 2, reverted inside a raw string literal ([lex.pptoken]/3.1) */
    size_t spelling(const preprocessing_token &token, vbuffer *out) const;
    /* [lex.phases]/1.7: "Each preprocessing token is converted into a token" */
    token convert(const preprocessing_token &token);
    /* [lex.phases]/1.5-1.6: adjacent string-literal preprocessing tokens
       (user-defined ones too) concatenated into one token */
    token convert_string_literals(const preprocessing_token *pieces,
                                  size_t count);

    const source_file &file;
    const implementation_defined_behavior &behavior;
    diagnostic_consumer &diagnostics;
    const lexer_options options;

    /* the implementation, one file per subclause (cplusplus-internal.h) */
    struct translation_character;
    translation_character peek(uint32_t at) const;
    translation_character read(uint32_t at);
    void diagnose_ignored_trigraph(uint32_t at);
    uint32_t scan_universal_character_name(uint32_t at, uint32_t *code_point,
                                           bool issue);
    uint32_t scan_identifier(uint32_t at, bool *spelling_differs);
    uint32_t scan_white_space(uint32_t at);
    uint32_t scan_comment(uint32_t at);
    uint32_t scan_pp_number(uint32_t at, bool *spelling_differs);
    uint32_t scan_preprocessing_op_or_punc(uint32_t at,
                                           preprocessing_op_or_punc *op,
                                           bool *spelling_differs);
    uint32_t scan_character_or_string_literal(uint32_t at,
                                              preprocessing_token &token);
    uint32_t scan_raw_string(uint32_t quote, preprocessing_token &token,
                             bool *ok);
    uint32_t scan_header_name(uint32_t at, bool *ok);
    uint32_t scan_ud_suffix(uint32_t at, preprocessing_token &token);
    bool is_identifier_start(uint32_t code_point) const;
    bool is_identifier_continue(uint32_t code_point) const;
    bool accept_extended_identifier_character(uint32_t code_point, bool first,
                                              source_location at,
                                              bool written_in_utf8);
    struct c_char;
    c_char evaluate_c_char(const char *spelling, size_t length, size_t *i,
                           source_location at, encoding_prefix prefix);
    void convert_character_literal(const char *spelling, size_t length,
                                   token &out);
    bool append_string_literal_piece(const char *spelling, size_t length,
                                     const preprocessing_token &piece,
                                     encoding_prefix prefix);
    /* [lex.string]: the code units of the string literals converted so far */
    const uint32_t *string_code_units() const
    {
        return code_units;
    }
    struct pp_number_analysis;
    bool analyze_pp_number(const char *spelling, size_t length,
                           source_location at, pp_number_analysis &a);
    void convert_integer_literal(const pp_number_analysis &a, token &out);
    void convert_floating_point_literal(const pp_number_analysis &a,
                                        token &out);

  private:
    preprocessing_token lex(bool header_name_permitted);
    void append_code_unit(uint32_t unit);
    void append_encoded(uint32_t code_point, encoding_prefix prefix);
    uint32_t *code_units = nullptr;
    size_t code_unit_count = 0, code_unit_capacity = 0;
    uint32_t position = 0;
    bool at_start_of_line = true, after_white_space = false;
    /* where the previous preprocessing token ended, and whether it was an
       identifier: a character that cannot continue it is diagnosed so */
    uint32_t previous_end = UINT32_MAX;
    bool previous_identifier = false;
};

const char *preprocessing_op_or_punc_spelling(preprocessing_op_or_punc op);
preprocessing_op_or_punc
preprocessing_op_or_punc_primary(preprocessing_op_or_punc op);
/* clang's token names (tok::getTokenName), for comparisons with clang's
   -dump-raw-tokens (preprocessing tokens) and -dump-tokens (tokens) */
const char *clang_token_name(const lexer &l, const preprocessing_token &token);
const char *clang_token_name(const lexer &l, const token &token);

} // namespace cplusplus

#endif
