// [over.literal] Example 1 (paragraph 8), docs/cxx-spec/over/over.literal.md
// needs: P7
// status: annotated

void operator "" _km(long double);                  // OK
string operator "" _i18n(const char*, std::size_t); // OK
template <char...> double operator "" _\u03C0();    // OK: UCN for lowercase pi
float operator ""_e(const char*);                   // OK
float operator ""E(const char*);                    // error: reserved literal suffix ([usrlit.suffix], [lex.ext]) // syntax-error
double operator""_Bq(long double);                  // OK: does not use the reserved identifier _Bq ([lex.name])
double operator"" _Bq(long double);                 // uses the reserved identifier _Bq ([lex.name])
float operator " " B(const char*);                  // error: non-empty string-literal // syntax-error
string operator "" 5X(const char*, std::size_t);    // error: invalid literal suffix identifier // syntax-error
double operator "" _miles(double);                  // error: invalid parameter-declaration-clause // semantic
template <char...> int operator "" _j(const char*); // error: invalid parameter-declaration-clause // semantic
extern "C" void operator "" _m(long double);        // error: C language linkage // semantic
