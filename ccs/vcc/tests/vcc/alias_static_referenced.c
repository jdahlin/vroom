/* A file-scope static declared with alias("...") and no initializer, referenced
 * from another initializer (the kernel's aspeed pinctrl SIG_EXPR_LIST_ALIAS):
 * only the .set, never also storage of its own ("redefinition of" in the
 * assembler). */
struct e { int x; };
static const struct e ea = { 1 }, eb = { 2 };
static const struct e *list_a[] = { &ea, &eb, 0 };
static const struct e *alias_a[3] __attribute__((alias("list_a")));
static const struct e **pins[] = { alias_a, 0 };
static int counter;
extern int counter_alias __attribute__((alias("counter")));
static int seven(void) { return 7; }
static int seven_alias(void) __attribute__((alias("seven")));

int main(void)
{
    counter_alias = 5;
    if (pins[0][1]->x != 2 || alias_a[0]->x != 1) return 1;
    if (counter != 5) return 3;
    if (seven_alias() != 7) return 4;
    return 0;
}
