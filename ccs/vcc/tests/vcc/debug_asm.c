/* vcc-flags: -g */
/* The textual assembly path must use correctly sized DWARF directives on
   both x86-64 and arm64, including the compile-unit offset and addresses. */
static int square(int x)
{
	return x * x;
}

int main(void)
{
	volatile int x = 7;
	return square(x) != 49;
}
