/*
 * The changes between editions that the front end asks about, each with
 * the edition that brought it, the edition that removed it (never: none), and
 * where it is recorded: Annex C ([diff]) when it changed the meaning of an
 * earlier edition's program, else the paper that added it.
 *
 * LANGUAGE_FEATURE(name, since, until, reference)
 */

/* C++11 */
LANGUAGE_FEATURE(unicode_and_raw_string_literals, cplusplus_11, never,
                 "[diff.cpp03.lex]/1: New kinds of string-literals")
LANGUAGE_FEATURE(user_defined_literals, cplusplus_11, never,
                 "[diff.cpp03.lex]/2: User-defined literal string support")
LANGUAGE_FEATURE(cplusplus_11_keywords, cplusplus_11, never, "[diff.cpp03.lex]/3: New keywords")
LANGUAGE_FEATURE(less_colon_colon_is_less, cplusplus_11, never,
                 "[lex.pptoken]/3.2: <:: not followed by : or > is < ::, new in C++11")
/* C++14 */
LANGUAGE_FEATURE(digit_separators, cplusplus_14, never,
                 "[diff.cpp11.lex]/1: pp-number can contain one or more single quotes")
LANGUAGE_FEATURE(binary_literals, cplusplus_14, never, "N3472: binary-literal")
LANGUAGE_FEATURE(library_ud_suffixes, cplusplus_14, never,
                 "N3642, N3660: the library's s, h, min, ms, us, ns, i, il, if")
/* C++17 */
LANGUAGE_FEATURE(trigraphs, cplusplus_98, cplusplus_17,
                 "[diff.cpp14.lex]/1: Removal of trigraph support as a required feature")
LANGUAGE_FEATURE(hexadecimal_floating_point_literals, cplusplus_17, never,
                 "[diff.cpp14.lex]/2: pp-number can contain p sign and P sign")
LANGUAGE_FEATURE(utf8_character_literals, cplusplus_17, never, "N4267: u8 character-literal")
/* C++20 */
LANGUAGE_FEATURE(cplusplus_20_keywords, cplusplus_20, never, "[diff.cpp17.lex]/3: New keywords")
LANGUAGE_FEATURE(three_way_comparison_operator, cplusplus_20, never, "[diff.cpp17.lex]/4: New operator <=>")
LANGUAGE_FEATURE(char8_t_type, cplusplus_20, never,
                 "[diff.cpp17.lex]/5: Type of UTF-8 string and character literals")
LANGUAGE_FEATURE(chrono_ud_suffixes, cplusplus_20, never, "P0355R7: the library's d and y")
/* C++23 */
LANGUAGE_FEATURE(size_t_suffix, cplusplus_23, never, "P0330R8: literal suffix for size_t")
LANGUAGE_FEATURE(delimited_escape_sequences, cplusplus_23, never, "P2290R3: delimited escape sequences")
/* C++26 */
LANGUAGE_FEATURE(raw_string_delimiter_dollar_at_grave, cplusplus_26, never,
                 "P2558R2: $ @ ` in the basic character set")
