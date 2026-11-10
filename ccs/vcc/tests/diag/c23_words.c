/* D5.5: C23 keywords used before C23: clang's error, with a help line saying what the
   earlier standards spell instead; strict -std=c11 has no 'typeof' */
/* diag-flags: -std=c11 */
int f(int n)
{
	bool b = 0;	/* (vcc declares b: no error at its uses, clang has one) */
	constexpr int k = 3;
	int *p = nullptr;
	static_assert(1);
	typeof(n) y = n;
	return k + y + (p != 0);
}
