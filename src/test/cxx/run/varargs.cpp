// calls through ... (default argument promotions: float to double, short to int, arrays decay) and string
// literals with escapes and concatenated pieces
extern "C" int snprintf(char *, unsigned long, const char *, ...);
extern "C" int strcmp(const char *, const char *);

int main()
{
	char buf[64];
	float f = 1.5f;
	short h = -7;
	const char *s = "ok";

	snprintf(buf, sizeof buf, "%d %g %d %s|" "\x41\101\t\\\"\?\0x", 3, f, h, s);
	if (strcmp(buf, "3 1.5 -7 ok|AA\t\\\"?"))
		return 1;
	if (sizeof("a\n" "b") != 4 || "\x7f"[0] != 127 || "\101"[0] != 'A')
		return 2;
	return 0;
}
