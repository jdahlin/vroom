/* a missing '(' after a statement keyword: clang's "expected '(' after 'if'", at the token */
int f(int x)
{
	if x > 0)
		return 1;
	while x)
		x--;
	for x = 0; x < 3; x++)
		;
	return x;
}
int g(int x)
{
	switch x) { default: break; }
	do x++; while x < 9);
	return x;
}
