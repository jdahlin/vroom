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
#include "frontend/cplusplus/owned.h"

namespace cplusplus
{

class source_files;
class header_cache;

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

/* [intro.compliance]/8: the extensions, by the implementation they come
   from (frontend/cplusplus/ext/extensions.inc) */
enum class extension_vendor : uint8_t { gnu, clang, microsoft, vcc };
enum class extension : uint8_t {
#define EXTENSION(name, vendor, reference) name,
#include "../../../frontend/cplusplus/ext/extensions.inc"
#undef EXTENSION
    count
};
const char *extension_name(extension e);
extension_vendor extension_vendor_of(extension e);
const char *extension_reference(extension e);
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
    /* [lex.digraph]/2: and, or, ... are alternative tokens (clang's
       -fno-operator-names turns that off) */
    bool operator_names = true;
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
    /* the vendors whose extensions are on (clang and GNU's always, as in
       clang; Microsoft's with -fms-extensions), and those turned off one by
       one */
    uint8_t extension_vendors =
        1 << (int)extension_vendor::gnu | 1 << (int)extension_vendor::clang;
    uint64_t extensions_off = 0;
    /* whether an extension is on */
    bool has(extension e) const;
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
    /* [lex.phases]/1.1: the physical source file characters, followed by 32
       zero bytes so that a reader may look ahead without checking the end
       (lex.simd.h loads 32 at a time) */
    const unsigned char *physical_characters = nullptr;
    uint32_t size = 0;

    bool open(const char *path);
    void from_memory(const char *name, const char *text, size_t size);
    /* the position a diagnostic reports: physical line and column, or the
       presumed line and file name a line directive gave ([cpp.line]/3-4);
       the file name lives until the next add_line_directive */
    source_position position(source_location location) const;
    void add_line_directive(source_location next_line, uint32_t line,
                            const char *presumed_name, size_t length);

  private:
    owned_array<char> characters;
    owned_array<uint32_t> physical_line_starts;
    struct presumed_line {
        uint32_t physical_line, presumed_line;
        size_t name_offset; /* in presumed_names, NUL-terminated */
    };
    owned_array<presumed_line> presumed_lines;
    owned_array<char> presumed_names;
    void take(owned_array<char> text, size_t length);
    void index_lines();
};

/* ---- diagnostics: [intro.compliance] ---------------------------------- */

enum class diagnostic_id : uint16_t {
#define DIAGNOSTIC(name, class, mapping, group, format) name,
#include "../../../frontend/cplusplus/intro/diagnostic-messages.inc"
#include "../../../frontend/cplusplus/parse/diagnostic-messages.inc"
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
    char text[238];
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
    /* -W<group>, -Wno-<group>, -Werror=<group> (error 1), -Wno-error=<group>
       (error -1) */
    struct group_setting {
        const char *group;
        bool enabled;
        int8_t error;
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
    const diagnostic_options &options;
    /* where locations are: one file, or a translation unit's files */
    const source_file *file = nullptr;
    const source_files *files = nullptr;
    /* added to each location issued: a lexer's locations are offsets in its
       file, which source_files places in the translation unit's */
    source_location location_base = 0;
    owned_array<diagnostic_message> messages;
    size_t error_count = 0, warning_count = 0, fatal_count = 0;
    /* while positive, issue() discards: the lexer looks ahead without consuming */
    unsigned suppressed = 0;
    /* OPTIMIZATION: the header cache (cpp/cpp.header_cache.cpp): the
       diagnostics issued that another translation unit might not issue
       alike: every one kept, and those only this one's pragmas ignored */
    size_t issued = 0;

    /* #pragma clang diagnostic ([ext.gnu], ext/gnu/pragma_diagnostic.cpp):
       the severity of a -W group from here on; false if there is no such
       group */
    bool map_group(const char *group, diagnostic_severity severity);
    /* the severity a diagnostic issued now would have */
    diagnostic_severity severity_now(diagnostic_id id) const;
    /* a diagnostic with the severity it had when the condition for it was
       met (clang's -Wunused-macros, decided at the #define) */
    diagnostic_message &issue_as(diagnostic_id id, source_location location,
                                 diagnostic_severity severity);
    void push_mappings();
    bool pop_mappings();

    /* a diagnostic message to which the caller adds the arguments; a
       message that maps to "ignored" is discarded when the next is issued */
    diagnostic_message &issue(diagnostic_id id, source_location location);
    /* file:line:column: severity: text, as clang prints it */
    void render(const diagnostic_message &message, vbuffer *out) const;

  private:
    diagnostic_message discarded;
    struct group_mapping {
        char group[64];
        diagnostic_severity severity;
    };
    owned_array<group_mapping> mappings;
    owned_array<uint32_t> mapping_pushes;
};

const char *diagnostic_name(diagnostic_id id);
const char *diagnostic_group(diagnostic_id id);
/* whether -W<name> names a warning group (clang's __has_warning) */
bool diagnostic_group_exists(const char *name);
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
    /* [cpp.concat]/2: "a placemarker preprocessing token", which "exist[s]
       only within translation phase 4" */
    placemarker,
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
    /* [cpp.rescan]/3: a macro name that was not replaced because it named
       the macro being replaced: "no longer available for further
       replacement" */
    bool not_replaceable = false;
    encoding_prefix prefix = encoding_prefix::none;
    preprocessing_op_or_punc op_or_punc = preprocessing_op_or_punc::count;
    source_location location = 0; /* the first physical character */
    uint32_t extent = 0;          /* the number of physical characters */
    /* phase 4: where the macro invocation that produced the token is, the
       name of the outermost one (0: the token is not from a macro) */
    source_location expansion = 0;
    /* [lex.ext]: where the ud-suffix starts in the physical characters (0: none) */
    uint32_t ud_suffix_offset = 0;
};

struct lexer_options {
    /* return white space and comments as tokens (clang's -dump-raw-tokens) */
    bool retain_white_space = false, retain_comments = false;
    /* the input is phase 4's output (clang's -x c++-cpp-output, a .ii
       file): clang's extensions to white space are off */
    bool preprocessed_input = false;
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
    lexer(const lexer &) = delete;
    lexer &operator=(const lexer &) = delete;

    /* [lex.pptoken]/3: the next preprocessing token */
    preprocessing_token next();
    /* [lex.pptoken]/3.3: the same, but a header-name is formed where one
       can be (after #include or import, or in a has-include-expression) */
    preprocessing_token next_allowing_header_name();
    /* the spelling of a preprocessing token: its characters after phases 1
       and 2, reverted inside a raw string literal ([lex.pptoken]/3.1); with
       files set, the token's location is the translation unit's and its
       file may be another */
    size_t spelling(const preprocessing_token &token, vbuffer *out) const;
    size_t spelling_in_file(const preprocessing_token &token,
                            vbuffer *out) const;
    const source_files *files = nullptr;
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
        return code_units.data();
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
    owned_array<uint32_t> code_units;
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

/* ---- [lex.separate]/1: the source files of a translation unit ---------- */

/*
 * [lex.separate]/1: "A source file together with all the headers and source
 * files included via the preprocessing directive #include, less any source
 * lines skipped by any of the conditional inclusion preprocessing directives,
 * is called a translation unit."  Each source file read, and each piece of
 * text phase 4 makes ([cpp.stringize], [cpp.concat], __LINE__: clang's
 * "scratch space"), gets a range of one space of source_locations, so that a
 * location also names its file.  Location 0 is no place.  Each file has its
 * own lexer, whose locations are offsets in the file; next() moves them into
 * the translation unit's space.
 */
class source_files
{
  public:
    struct entry {
        entry(const implementation_defined_behavior &behavior,
              diagnostic_consumer &diagnostics, lexer_options options)
            : lex(file, behavior, diagnostics, options)
        {
        }
        source_file file;
        lexer lex;
        owned_string name;
        source_location base = 0;
        uint32_t index = 0;
        /* text made by phase 4, not read from a file */
        bool scratch = false;
        /* found in a system directory, or #pragma GCC system_header: clang
           issues no warnings there */
        bool system_header = false;
        /* OPTIMIZATION: the header cache's source of the same text */
        uint32_t cache_source = UINT32_MAX;
    };

    source_files(const implementation_defined_behavior &behavior,
                 diagnostic_consumer &diagnostics);
    source_files(const source_files &) = delete;
    source_files &operator=(const source_files &) = delete;

    /* the source file at path, or nullptr if it cannot be read */
    entry *open(const char *path);
    entry *open(const char *path, lexer_options options);
    /* the options of the files open() reads */
    lexer_options file_options;
    /* text that is not a file: the predefined macros, the command line's,
       or a preprocessing token phase 4 spelled ("<scratch space>") */
    entry &add_text(const char *name, const char *text, size_t length,
                    bool scratch, lexer_options options = {});
    /* [lex.pptoken]/3: the next preprocessing token of a file, located in
       the translation unit */
    preprocessing_token next(entry &e, bool header_name_permitted = false);

    const entry *find(source_location location) const;
    entry &operator[](size_t i)
    {
        return files[i];
    }
    size_t size() const
    {
        return files.size();
    }
    bool is_scratch(source_location location) const;
    source_position position(source_location location) const;
    /* where a token's first character is: after any splices before it */
    source_location first_character(const preprocessing_token &token) const;
    size_t spelling(const preprocessing_token &token, vbuffer *out) const;
    /* phase 7 of tokens from any of the files ([lex.phases]/1.5-1.7) */
    token convert(const preprocessing_token &token);
    token convert_string_literals(const preprocessing_token *pieces,
                                  size_t count);
    const lexer &phase7() const
    {
        return conversion.lex;
    }

    const implementation_defined_behavior &behavior;
    diagnostic_consumer &diagnostics;

    /* OPTIMIZATION: the header cache, if one is shared by the translation
       units; each source file and text placed is one of its sources, and
       cache_bases[source] is where its latest copy is here (0: none) */
    header_cache *cache = nullptr;
    owned_array<source_location> cache_bases;

  private:
    owned_objects<entry> files;
    entry conversion; /* an empty file: its lexer converts any file's tokens */
    source_location next_base = 1;
    void place(entry &e);
};

/* ---- [cpp]: preprocessing directives, translation phase 4 -------------- */

/* how the implementation-defined places of [cpp.include]/2-3 are given:
   clang's -iquote, -I, -isystem and -idirafter */
enum class include_search : uint8_t { quote, angled, system, after };

struct preprocessor_options {
    struct include_directory {
        const char *path;
        include_search search;
    };
    owned_array<include_directory> include_directories;
    /* [ext.clang] clang_embed: --embed-dir=, where #embed also looks */
    owned_array<const char *> embed_directories;
    /* -D and -U, in command line order, as the #define and #undef lines of
       clang's "<command line>" file */
    owned_array<char> command_line;
    /* clang's -undef: of the predefined macros only those the standards
       name (__cplusplus, __STDC*), and the dynamic ones (__FILE__, ...) */
    bool predefined_macros = true;
    /* [cpp.include]/6: "up to an implementation-defined nesting limit" */
    unsigned include_nesting_limit = 200;
    /* for clang's -dM: keep definitions in the order made */
    bool keep_definition_order = false;
    /* OPTIMIZATION: the header cache shared by the translation units
       preprocessed with these options, one after another (nullptr: none) */
    header_cache *cache = nullptr;
};

/* the macros an implementation defines whose replacement is computed where
   they are used ([cpp.predefined]/1 __FILE__, __LINE__, __DATE__, __TIME__),
   the operators that [cpp.cond]/7 treats as macro names, and clang's own */
enum class dynamic_macro : uint8_t {
    none,
    file,
    line,
    date,
    time,
    counter,
    include_level,
    base_file,
    file_name,
    timestamp,
    pragma_operator,
    has_include,
    has_include_next,
    has_cpp_attribute,
    has_attribute,
    has_c_attribute,
    has_declspec_attribute,
    has_builtin,
    has_feature,
    has_extension,
    has_warning,
    is_identifier,
    has_embed,
    is_target_arch,
    is_target_vendor,
    is_target_os,
    is_target_environment,
    building_module,
    is_target_variant,
    microsoft_pragma,
    flt_eval_method,
    has_constexpr_builtin,
};

/* [cpp.replace]: one element of a replacement list: a preprocessing token,
   and which parameter it names (-1: none) */
struct replacement_element {
    preprocessing_token token;
    int16_t parameter;
};

/* [cpp.import]/5: "Each #define directive encountered when preprocessing
   each translation unit in a program results in a distinct macro
   definition." */
struct macro_definition {
    uint32_t name = 0; /* the macro name's identifier ([cpp.replace]/8) */
    source_location location = 0; /* the point of definition */
    /* the replacement list: elements [first_element, first_element + length) */
    uint32_t first_element = 0, length = 0;
    /* the identifier-list's identifiers */
    uint32_t first_parameter = 0;
    uint16_t parameter_count = 0;
    bool function_like = false;
    /* [cpp.replace]/15: "If there is a ... immediately preceding the )" */
    bool variadic = false;
    /* GNU's named variable arguments: #define f(args...) */
    bool named_variadic = false;
    bool predefined = false;
    dynamic_macro dynamic = dynamic_macro::none;
    /* [cpp.rescan]/3: the macro is being replaced: its name is not */
    bool being_replaced = false;
    bool used = false;
    /* the definition this one replaced (#pragma push_macro), or -1 */
    int32_t pushed = -1;
};

/* [cpp.pre]: the directive names "appearing after a # in the syntax", and
   the conditionally-supported-directives this implementation supports */
enum class directive_kind : uint8_t {
    unknown,
    include,
    include_next, /* GNU */
    import,       /* Objective-C's and Microsoft's #import */
    define,
    undef,
    line,
    error,
    warning, /* C++23 ([cpp.error]) */
    pragma,
    if_,
    ifdef,
    ifndef,
    elif,
    elifdef,  /* C++23 */
    elifndef, /* C++23 */
    else_,
    endif,
    ident, /* GNU, as #sccs */
    embed, /* C23, an extension in C++ */
};

class preprocessor
{
  public:
    preprocessor(source_files &files, const preprocessor_options &options);
    preprocessor(const preprocessor &) = delete;
    preprocessor &operator=(const preprocessor &) = delete;

    /* the main source file, after the predefined macros and the command
       line's; false if it cannot be read */
    bool start(const char *path);
    /* [lex.phases]/1.4: the next preprocessing token after phase 4:
       directives executed, macros replaced, included files in place */
    preprocessing_token next();

    /* clang's -dM: the macro definitions in effect, one #define line each */
    void write_definitions(vbuffer *out);

    source_files &files;
    const preprocessor_options &options;
    const implementation_defined_behavior &behavior;
    diagnostic_consumer &diagnostics;

    /* the implementation, one file per subclause */

    /* ---- identifiers: their spellings, and the macro each one names */
    struct identifier_entry {
        uint32_t spelling_offset, length, hash;
        int32_t macro;
        bool poisoned;
        /* OPTIMIZATION: the header cache: the identifier's name there, and
           the latest recording that has the identifier's macro as an input
           or wrote it */
        uint32_t cache_name;
        uint32_t cache_serial;
    };
    owned_array<identifier_entry> identifiers;
    owned_array<char> identifier_spellings;
    owned_array<uint32_t> identifier_slots;
    owned_buffer spelling_buffer;
    uint32_t identifier(const char *spelling, size_t length);
    uint32_t identifier(const preprocessing_token &token);
    const char *identifier_spelling(uint32_t id, size_t *length) const;
    diagnostic_message &quoted(diagnostic_message &m,
                               const preprocessing_token &token);

    /* ---- [cpp.pre]: groups, and where the preprocessing tokens come from */
    struct inclusion {
        source_files::entry *file;
        /* the place in the search the file was found in (clang's
           #include_next), -1 if none */
        int32_t directory;
        /* the conditional nesting when the file was entered */
        uint32_t conditional_depth;
        source_location included_at;
        bool system_header;
        bool has_lookahead;
        preprocessing_token lookahead;
        /* OPTIMIZATION: the multiple-include guard (cpp.include.cpp) */
        uint8_t guard_state = 0;
        uint32_t guard_name = 0, guard_conditional = 0;
        uint64_t device = 0, inode = 0;
    };
    owned_array<inclusion> inclusions;
    source_files::entry *main_file = nullptr;
    uint32_t predefines_file = UINT32_MAX;
    /* the end of the directive's last token so far */
    source_location directive_end = 0;
    /* the last token read from a source file (__LINE__) */
    preprocessing_token last_file_token;
    unsigned collecting_arguments = 0, in_condition = 0;
    bool read_from_file = false;
    /* reading stops at the end of an included file, which is read again */
    bool stop_at_file_end = false;
    bool started = false;
    void execute_definitions(source_files::entry &file);
    preprocessing_token read_file_token(bool header_name_permitted = false);
    void unread_file_token(const preprocessing_token &token);
    bool at_directive(const preprocessing_token &token);
    void execute_directive(const preprocessing_token &hash);
    preprocessing_token directive_token(bool header_name_permitted = false);
    void skip_rest_of_directive();
    void expect_end_of_directive(const char *directive);
    void leave_file();
    bool system_header_at(source_location location) const;

    /* ---- [cpp.cond] */
    struct conditional {
        source_location location;
        bool group_taken; /* a group of the if-section was processed */
        bool else_seen;
    };
    owned_array<conditional> conditionals;
    implementation_defined_behavior condition_behavior;
    source_file condition_file;
    lexer condition_lexer; /* phase 7 of #if's literals, as intmax_t */
    struct value;
    struct expression_reader;
    void directive_if(const preprocessing_token &name, directive_kind kind);
    void directive_elif_else(const preprocessing_token &name,
                             directive_kind kind);
    void directive_endif(const preprocessing_token &name);
    void skip_group();
    preprocessing_token directive_token_or_line();
    void suggest_conditional_directive(const preprocessing_token &name);
    bool evaluate_condition(source_location at);
    bool evaluate_expression(const preprocessing_token *tokens, size_t count,
                             uint64_t *result, bool *negative = nullptr);
    bool evaluate_defined_condition(const preprocessing_token &name,
                                    bool negated);
    bool expansion_is_function_like(const preprocessing_token &token) const;
    bool has_include(expression_reader &reader, const preprocessing_token &name,
                     bool next, int64_t *result);

    /* ---- [cpp.include] */
    struct included_once {
        uint64_t device, inode;
    };
    owned_array<included_once> once_files;
    /* every file entered, for #import ([ext.clang]) */
    owned_array<included_once> entered_files;
    bool include_limit_reached = false;
    /* OPTIMIZATION: files whose contents are one #ifndef group: entered
       again only while the guard macro is not defined (cpp.include.cpp) */
    struct guarded_file {
        uint64_t device, inode;
        uint32_t name;
    };
    owned_array<guarded_file> guarded_files;
    int64_t defined_condition_operand = -1;
    void guard_token();
    void guard_directive(directive_kind kind);
    void guard_end_of_file();
    bool guard_skips(uint64_t device, uint64_t inode);
    void directive_include(const preprocessing_token &name,
                           directive_kind kind);
    bool header_name_from_tokens(const preprocessing_token &first,
                                 owned_buffer &header, source_location *at);
    bool find_header(const char *name, size_t length, bool angled,
                     int32_t after_directory, owned_buffer &path,
                     int32_t *directory, bool *system);
    int32_t include_next_start(const preprocessing_token &name);
    void include_once();

    /* ---- [cpp.replace], [cpp.scope] */
    owned_array<macro_definition> macros;
    owned_array<replacement_element> elements;
    owned_array<uint32_t> parameters;
    owned_array<int32_t> definition_order;
    /* -Wunused-macros: the macros of the main file to warn of if unused,
       with the severity at their definition */
    struct unused_candidate {
        int32_t macro;
        diagnostic_severity severity;
    };
    owned_array<unused_candidate> unused_candidates;
    void warn_unused_macros();
    void warn_unused_macro(int32_t macro);
    bool check_macro_name(const preprocessing_token &name, bool definition);
    bool is_language_defined(const macro_definition &m) const;
    bool identical_definitions(const macro_definition &a,
                               const macro_definition &b) const;
    int32_t add_definition(const macro_definition &m);
    void directive_define(const preprocessing_token &name);
    bool parameter_list(macro_definition &m, owned_array<uint32_t> &names);
    void directive_undef(const preprocessing_token &name);

    /* ---- [cpp.subst], [cpp.stringize], [cpp.concat], [cpp.rescan]: the
       replacement of macro invocations, as a stack of token contexts read
       before the rest of the source file */
    struct token_context {
        uint32_t begin, end, next;
        int32_t macro; /* being replaced while the context is read, or -1 */
        bool barrier;  /* reading stops at the end ([cpp.subst]/1) */
        /* white space after the replacement's last token, which the token
           after it takes ([impl.clang]) */
        bool trailing_space;
    };
    owned_array<token_context> contexts;
    owned_array<preprocessing_token> context_tokens;
    struct argument {
        uint32_t begin, end;                  /* the argument's tokens */
        int32_t expanded_begin, expanded_end; /* -1: not yet replaced */
    };
    struct substitution {
        const macro_definition &m;
        owned_array<argument> &arguments;
        owned_array<preprocessing_token> &tokens;
        uint32_t end; /* of the elements substituted */
        bool next_space;
        /* the variable arguments were left out of the invocation */
        bool variadic_elided;
    };
    bool variadic_elided = false;
    /* the white space and line start of a macro name replaced by nothing,
       which the next token takes */
    bool pending_flags = false, pending_start_of_line = false,
         pending_white_space = false, trailing_space = false;
    void push_tokens(const preprocessing_token *tokens, size_t count,
                     int32_t macro, bool barrier);
    void pop_context();
    bool read_unexpanded(preprocessing_token &token);
    bool read_expanded(preprocessing_token &token);
    bool peek_left_parenthesis();
    bool replace_macro(preprocessing_token &name, int32_t macro);
    void finish_replacement(const preprocessing_token &name,
                            owned_array<preprocessing_token> &result);
    bool collect_arguments(const preprocessing_token &name,
                           const macro_definition &m,
                           owned_array<argument> &arguments,
                           owned_array<preprocessing_token> &tokens,
                           preprocessing_token *right_parenthesis);
    void substitute(const preprocessing_token &name, const macro_definition &m,
                    owned_array<argument> &arguments,
                    owned_array<preprocessing_token> &tokens,
                    owned_array<preprocessing_token> &out);
    void emit(substitution &s, const preprocessing_token *tokens, size_t count,
              owned_array<preprocessing_token> &out);
    void substitute_range(substitution &s, uint32_t first,
                          owned_array<preprocessing_token> &out);
    void expand_argument(argument &a, owned_array<preprocessing_token> &tokens);
    uint32_t va_opt_end(uint32_t open, uint32_t end) const;
    bool is_va_opt(const macro_definition &m, const replacement_element &e);
    void va_opt(substitution &s, uint32_t at,
                owned_array<preprocessing_token> &out);
    uint32_t paste_operand(substitution &s, uint32_t at,
                           owned_array<preprocessing_token> &out);
    preprocessing_token stringize_operand(substitution &s, uint32_t at);
    preprocessing_token stringize(const preprocessing_token *tokens,
                                  size_t count, source_location at);
    bool concatenate(preprocessing_token &left,
                     const preprocessing_token &right, source_location at);
    bool spelled(const char *text, size_t length, preprocessing_token *out);

    /* ---- [cpp.line], [cpp.error], [cpp.pragma], [cpp.pragma.op] */
    bool line_number(const preprocessing_token &token, bool gnu_marker,
                     uint32_t *value);
    bool string_contents(const preprocessing_token &token, owned_buffer &out);
    void set_presumed_line(uint32_t line, const char *name, size_t length);
    void directive_line(const preprocessing_token &name);
    void rest_of_line(source_location from, owned_buffer &out);
    void directive_error(const preprocessing_token &name, bool warning);
    void directive_pragma(const preprocessing_token &name);
    void execute_pragma(const preprocessing_token *tokens, size_t count,
                        source_location at);
    void pragma_message(const preprocessing_token *tokens, size_t count,
                        int kind);
    bool pragma_operator(const preprocessing_token &name);
    struct pushed_macro {
        uint32_t name;
        int32_t macro;
    };
    owned_array<pushed_macro> pushed_macros;
    source_location pragma_end = 0;

    /* ---- the extensions (ext/VENDOR/) */
    void gnu_line_marker(const preprocessing_token &digits);
    void gnu_ident_directive(const preprocessing_token &name);
    struct embed_parameters {
        bool has_limit = false, has_offset = false, has_prefix = false,
             has_suffix = false, has_if_empty = false;
        uint64_t limit = 0, offset = 0;
        owned_array<preprocessing_token> prefix, suffix, if_empty;
    };
    void clang_embed_directive(const preprocessing_token &name);
    bool embed_continues_line = false;
    /* 1: parsed, 0: an unknown parameter (in __has_embed), -1: an error */
    int parse_embed_parameters(const preprocessing_token *tokens, size_t count,
                               embed_parameters &p, bool in_has_embed);
    bool find_embed(const char *name, size_t length, bool angled,
                    owned_buffer &path);
    bool embedded_size(const char *path, const embed_parameters &p,
                       uint64_t *size);
    uint32_t gnu_comma_paste(substitution &s, uint32_t at,
                             owned_array<preprocessing_token> &out);
    bool gnu_pragma(const preprocessing_token *tokens, size_t count);
    void gnu_pragma_diagnostic(const preprocessing_token *tokens, size_t count);
    void gnu_define_dynamic_macros();
    bool gnu_dynamic_macro(const macro_definition &m, owned_buffer &text);
    void clang_define_dynamic_macros();
    void microsoft_define_dynamic_macros();
    bool microsoft_pragma_operator(const preprocessing_token &name);
    /* 0: not clang's, 1: replaced by text, 2: diagnosed, nothing to read */
    int clang_dynamic_macro(preprocessing_token &name,
                            const macro_definition &m, owned_buffer &text);

    /* ---- [cpp.module], [cpp.import] */
    bool module_or_import_directive(preprocessing_token &first);
    bool begins_module_directive(const preprocessing_token &next);
    bool names_object_like_macro(const preprocessing_token &token);

    /* ---- OPTIMIZATION: the header cache (cpp/cpp.header_cache.cpp).  The
       hooks, each marked so where it is called: */
    /* every read of an identifier's macro */
    int32_t macro_of(uint32_t id);
    /* #define and #undef */
    void cache_write(uint32_t id, int32_t macro);
    /* an #include whose file is found: true if it was replayed */
    bool cache_replay(uint64_t device, uint64_t inode, int32_t directory,
                      bool system);
    /* the file just entered by an #include */
    void cache_record(int32_t directory, bool system);
    /* the end of an included file */
    void cache_file_end();
    /* a token next() returns */
    void cache_output(const preprocessing_token &t);
    /* what a recording cannot replay: it is not kept */
    void cache_spoil();
    /* the once and guard tables and #pragma diagnostic's mappings */
    void cache_once_read(uint64_t device, uint64_t inode, bool present);
    void cache_effect(uint8_t kind, uint64_t device, uint64_t inode,
                      uint32_t name, const char *text);
    struct cache_recording;
    owned_array<cache_recording> recordings;
    uint32_t cache_serial = 0;
    owned_array<uint32_t> cache_name_ids;
    owned_array<preprocessing_token> replay_tokens;
    size_t replay_next = 0;
    bool replay_pending_flags = false, replay_start_of_line = false,
         replay_white_space = false;
    bool cache_disabled = false;
    unsigned cache_applying = 0;
    bool cache_top_level() const;
    uint32_t cache_name(uint32_t id);
    uint32_t cache_identifier(uint32_t name);
    uint64_t cache_fingerprint(int32_t macro);
    bool cache_inputs_hold(const void *entry);
    void cache_apply(const void *entry);
    void cache_flatten(const void *entry);
    void cache_collect_sources(cache_recording &r, size_t end);
    void cache_child_done(const void *entry);

    /* ---- [cpp.predefined] */
    uint32_t counter = 0;
    char date[16] = "", time_of_day[16] = "";
    void write_predefined_macros(vbuffer *out) const;
    int32_t define_dynamic(const char *name, dynamic_macro dynamic,
                           bool function_like);
    void define_dynamic_macros();
    bool replace_dynamic(preprocessing_token &name, const macro_definition &m);
    bool name_is_has_cpp_attribute(const preprocessing_token &t);
    bool feature_operand(const preprocessing_token &name, owned_buffer &operand,
                         owned_buffer &scope);
};

/*
 * OPTIMIZATION: the header cache, after the old preprocessor's
 * (frontend/pp/header-cache-*.cpp): what an #include did, kept for the
 * translation units that come after, preprocessed one after another with
 * the same options.  An #include of the same file, found the same way,
 * whose inputs (the macros it read, the #pragma once files it looked at)
 * are as they were, has its effects applied and its tokens returned
 * without running it.  cpp/cpp.header_cache.cpp has the rules.
 */
class header_cache
{
  public:
    header_cache();
    ~header_cache();
    header_cache(const header_cache &) = delete;
    header_cache &operator=(const header_cache &) = delete;

    /* a source: a file (by path and size) or a text (by name and
       contents), with a place in the cache's own space of locations */
    struct source {
        uint32_t key_offset, key_length, size;
        uint64_t hash;
        source_location base;
        bool text;
    };
    owned_array<source> sources;
    uint32_t source_of(const char *name, size_t name_length,
                       const unsigned char *text, uint32_t size, bool is_text);

    /* identifiers, by spelling (1-based; 0: none) */
    uint32_t name_of(const char *spelling, size_t length);
    const char *name_spelling(uint32_t name, size_t *length) const;
    size_t name_count() const
    {
        return names.size();
    }

    struct entry;
    /* the entries recorded for an #include of a file (variants, the
       latest first) */
    entry *find(uint64_t device, uint64_t inode, int32_t directory,
                bool system);
    entry &make();
    void keep(entry &e, uint64_t device, uint64_t inode, int32_t directory,
              bool system);

    /* -stats: includes replayed, recorded, run but not kept */
    uint64_t hits = 0, misses = 0, recorded = 0, spoiled = 0;
    uint64_t tokens_replayed = 0;
    const char *debug_key(uint32_t offset) const
    {
        return keys.data() + offset;
    }

  private:
    owned_array<char> keys;
    owned_array<uint32_t> source_slots;
    struct name {
        uint32_t offset, length, hash;
    };
    owned_array<name> names;
    owned_array<char> name_characters;
    owned_array<uint32_t> name_slots;
    source_location next_base = 1;
    struct key {
        uint64_t device, inode;
        int32_t directory;
        bool system;
        entry *first;
    };
    owned_array<key> entry_keys;
    owned_objects<entry> entries;
};

} // namespace cplusplus

#endif
