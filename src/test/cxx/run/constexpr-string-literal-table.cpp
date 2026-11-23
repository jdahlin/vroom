/* [lex.string]/9, [expr.const]: a constexpr array of pointers to string
   literals (alone or as members of aggregates) is a constant-initialized
   object whose bound an unknown-bound declarator takes from its
   initializer, [dcl.array]/3; sizeof and range-for see the complete type
   ([expr.sizeof]/1, [stmt.ranged]/1), and the characters read in constant
   expressions */
struct directive {
    const char *name;
    int kind;
};
static constexpr directive table[] = {
    {"include", 1},
    {"define", 2},
    {"undef", 3},
};
constexpr const char *words[] = {"x", "yz"};
constexpr unsigned long table_count = sizeof table / sizeof *table;
static_assert(table_count == 3, "");
static_assert(sizeof words / sizeof *words == 2, "");
static_assert(table[1].name[2] == 'f', "");
static_assert(words[1][1] == 'z', "");
static_assert(words[0][1] == 0, "");

/* a reference member bound to a character of a literal */
struct view {
    const char &c;
    int n;
};
constexpr view v = {"hello"[1]};
static_assert(v.c == 'e' && v.n == 0, "");

static int length(const char *s)
{
    int n = 0;
    while (s[n])
        n++;
    return n;
}

int main()
{
    int sum = 0;
    for (const directive &d : table)
        sum += d.kind * 100 + length(d.name);
    for (const char *w : words)
        sum += w[0];
    return sum - (100 * 6 + 7 + 6 + 5 + 'x' + 'y');
}
