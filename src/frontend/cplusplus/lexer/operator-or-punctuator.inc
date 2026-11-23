/*
 * [lex.operators]/1: preprocessing-op-or-punc, in the order of the grammar:
 *
 *     preprocessing-operator: one of
 *         '# ## %: %:%:'
 *     operator-or-punctuator: one of
 *         '{ } [ ] ( )'
 *         '<: :> <% %> ; : ...'
 *         '? :: . .* -> ->* ~'
 *         '! + - * / % ^ & |'
 *         '= += -= *= /= %= ^= &= |='
 *         '== != < > <= >= <=> && ||'
 *         '<< >> <<= >>= ++ -- ,'
 *         'and or xor not bitand bitor compl'
 *         'and_eq or_eq xor_eq not_eq'
 *
 * The alternative tokens spelled as identifiers (and, or, ...) are formed as
 * identifiers in phase 3 and become operators in phase 7 ([lex.key]/2), so
 * they are not here; keyword.inc maps them.  The digraphs of [lex.digraph]
 * Table 1 name their primary token.  The last name is clang's (TokenKinds.def)
 * for comparisons with clang -dump-tokens.
 *
 * PREPROCESSING_OP_OR_PUNC(name, spelling, primary name, clang name)
 */
PREPROCESSING_OP_OR_PUNC(HASH, "#", HASH, "hash")
PREPROCESSING_OP_OR_PUNC(HASH_HASH, "##", HASH_HASH, "hashhash")
PREPROCESSING_OP_OR_PUNC(PERCENT_COLON, "%:", HASH, "hash")
PREPROCESSING_OP_OR_PUNC(PERCENT_COLON_PERCENT_COLON, "%:%:", HASH_HASH, "hashhash")
PREPROCESSING_OP_OR_PUNC(LEFT_BRACE, "{", LEFT_BRACE, "l_brace")
PREPROCESSING_OP_OR_PUNC(RIGHT_BRACE, "}", RIGHT_BRACE, "r_brace")
PREPROCESSING_OP_OR_PUNC(LEFT_BRACKET, "[", LEFT_BRACKET, "l_square")
PREPROCESSING_OP_OR_PUNC(RIGHT_BRACKET, "]", RIGHT_BRACKET, "r_square")
PREPROCESSING_OP_OR_PUNC(LEFT_PARENTHESIS, "(", LEFT_PARENTHESIS, "l_paren")
PREPROCESSING_OP_OR_PUNC(RIGHT_PARENTHESIS, ")", RIGHT_PARENTHESIS, "r_paren")
PREPROCESSING_OP_OR_PUNC(LESS_COLON, "<:", LEFT_BRACKET, "l_square")
PREPROCESSING_OP_OR_PUNC(COLON_GREATER, ":>", RIGHT_BRACKET, "r_square")
PREPROCESSING_OP_OR_PUNC(LESS_PERCENT, "<%", LEFT_BRACE, "l_brace")
PREPROCESSING_OP_OR_PUNC(PERCENT_GREATER, "%>", RIGHT_BRACE, "r_brace")
PREPROCESSING_OP_OR_PUNC(SEMICOLON, ";", SEMICOLON, "semi")
PREPROCESSING_OP_OR_PUNC(COLON, ":", COLON, "colon")
PREPROCESSING_OP_OR_PUNC(ELLIPSIS, "...", ELLIPSIS, "ellipsis")
PREPROCESSING_OP_OR_PUNC(QUESTION, "?", QUESTION, "question")
PREPROCESSING_OP_OR_PUNC(COLON_COLON, "::", COLON_COLON, "coloncolon")
PREPROCESSING_OP_OR_PUNC(DOT, ".", DOT, "period")
PREPROCESSING_OP_OR_PUNC(DOT_STAR, ".*", DOT_STAR, "periodstar")
PREPROCESSING_OP_OR_PUNC(ARROW, "->", ARROW, "arrow")
PREPROCESSING_OP_OR_PUNC(ARROW_STAR, "->*", ARROW_STAR, "arrowstar")
PREPROCESSING_OP_OR_PUNC(TILDE, "~", TILDE, "tilde")
PREPROCESSING_OP_OR_PUNC(EXCLAMATION, "!", EXCLAMATION, "exclaim")
PREPROCESSING_OP_OR_PUNC(PLUS, "+", PLUS, "plus")
PREPROCESSING_OP_OR_PUNC(MINUS, "-", MINUS, "minus")
PREPROCESSING_OP_OR_PUNC(STAR, "*", STAR, "star")
PREPROCESSING_OP_OR_PUNC(SLASH, "/", SLASH, "slash")
PREPROCESSING_OP_OR_PUNC(PERCENT, "%", PERCENT, "percent")
PREPROCESSING_OP_OR_PUNC(CARET, "^", CARET, "caret")
PREPROCESSING_OP_OR_PUNC(AMPERSAND, "&", AMPERSAND, "amp")
PREPROCESSING_OP_OR_PUNC(BAR, "|", BAR, "pipe")
PREPROCESSING_OP_OR_PUNC(EQUAL, "=", EQUAL, "equal")
PREPROCESSING_OP_OR_PUNC(PLUS_EQUAL, "+=", PLUS_EQUAL, "plusequal")
PREPROCESSING_OP_OR_PUNC(MINUS_EQUAL, "-=", MINUS_EQUAL, "minusequal")
PREPROCESSING_OP_OR_PUNC(STAR_EQUAL, "*=", STAR_EQUAL, "starequal")
PREPROCESSING_OP_OR_PUNC(SLASH_EQUAL, "/=", SLASH_EQUAL, "slashequal")
PREPROCESSING_OP_OR_PUNC(PERCENT_EQUAL, "%=", PERCENT_EQUAL, "percentequal")
PREPROCESSING_OP_OR_PUNC(CARET_EQUAL, "^=", CARET_EQUAL, "caretequal")
PREPROCESSING_OP_OR_PUNC(AMPERSAND_EQUAL, "&=", AMPERSAND_EQUAL, "ampequal")
PREPROCESSING_OP_OR_PUNC(BAR_EQUAL, "|=", BAR_EQUAL, "pipeequal")
PREPROCESSING_OP_OR_PUNC(EQUAL_EQUAL, "==", EQUAL_EQUAL, "equalequal")
PREPROCESSING_OP_OR_PUNC(EXCLAMATION_EQUAL, "!=", EXCLAMATION_EQUAL, "exclaimequal")
PREPROCESSING_OP_OR_PUNC(LESS, "<", LESS, "less")
PREPROCESSING_OP_OR_PUNC(GREATER, ">", GREATER, "greater")
PREPROCESSING_OP_OR_PUNC(LESS_EQUAL, "<=", LESS_EQUAL, "lessequal")
PREPROCESSING_OP_OR_PUNC(GREATER_EQUAL, ">=", GREATER_EQUAL, "greaterequal")
PREPROCESSING_OP_OR_PUNC(SPACESHIP, "<=>", SPACESHIP, "spaceship")
PREPROCESSING_OP_OR_PUNC(AMPERSAND_AMPERSAND, "&&", AMPERSAND_AMPERSAND, "ampamp")
PREPROCESSING_OP_OR_PUNC(BAR_BAR, "||", BAR_BAR, "pipepipe")
PREPROCESSING_OP_OR_PUNC(LESS_LESS, "<<", LESS_LESS, "lessless")
PREPROCESSING_OP_OR_PUNC(GREATER_GREATER, ">>", GREATER_GREATER, "greatergreater")
PREPROCESSING_OP_OR_PUNC(LESS_LESS_EQUAL, "<<=", LESS_LESS_EQUAL, "lesslessequal")
PREPROCESSING_OP_OR_PUNC(GREATER_GREATER_EQUAL, ">>=", GREATER_GREATER_EQUAL, "greatergreaterequal")
PREPROCESSING_OP_OR_PUNC(PLUS_PLUS, "++", PLUS_PLUS, "plusplus")
PREPROCESSING_OP_OR_PUNC(MINUS_MINUS, "--", MINUS_MINUS, "minusminus")
PREPROCESSING_OP_OR_PUNC(COMMA, ",", COMMA, "comma")
/* clang extensions ([intro.compliance]/8): Objective-C's '@' as a token */
PREPROCESSING_OP_OR_PUNC(AT, "@", AT, "at")
