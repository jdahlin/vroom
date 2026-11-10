/* D5.5: clang's on-by-default pointer conversion warnings: qualifiers dropped at the first
   level or changed deeper (-Wincompatible-pointer-types-discards-qualifiers), integers of
   another sign or plain char (-Wpointer-sign) */
void g(int *);
void ch(char *);
int *h(const int *c)
{
	const char *const *pp = 0;
	const char **qq = 0;
	char **q = qq;
	unsigned u;
	unsigned char uc;
	void *vp = c;
	g(c);
	g(&u);
	ch(&uc);
	(void)pp, (void)q, (void)vp;
	return c;
}
