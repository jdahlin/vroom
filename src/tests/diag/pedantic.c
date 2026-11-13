/* D5.5: GNU and later-standard extensions under -pedantic (clang's ext_* warnings, off by
   default; -Wgnu, -Werror=pedantic and -pedantic-errors work on the groups) */
/* diag-flags: -std=c11 -pedantic */
struct S { int n; int a[0]; };
int g(int x)
{
	int y = ({ 1; });
	int z = x ?: 2;
	struct {} e;
	switch (x) { case 1 ... 3: break; }
	void *l = &&out;
	goto *l;
out:
	return y + z + (int)sizeof(e);
}
