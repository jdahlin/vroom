/* diag-flags: -std=c89 -Wno-deprecated-non-prototype */
extern int unprototyped();
extern int variadic(int, ...);
extern void value(void);
struct incomplete;
extern struct incomplete missing;
void test(void)
{
    unprototyped(value());
    variadic(1, value());
    unprototyped(missing);
    variadic(1, missing);
}
