/* a missing ')' of a pair (sizeof, a cast, static_assert, switch):
   at the token, with "to match this '('" (clang's BalancedDelimiterTracker) */
/* diag-differs: "int\n(int z)": clang's second "expected ')'" at the same place is left out */
int a = sizeof(int;
int b = (int 3;
_Static_assert(1, "one";
int f(int x)
{
	switch (x { default: break; }
	return x;
}
int g(void) { return (int x)3 + sizeof(int y); }
/* the name of a declarator that starts with '(' and no name: after the '(' */
int
(int z);
