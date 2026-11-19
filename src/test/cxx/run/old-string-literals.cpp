// string literals of every kind: escapes, \u, raw (with a delimiter), wide/u16/u32/u8, concatenation,
// their types (const E[N]) and user-defined string literals (operator""_x(str, len))
#include <stddef.h>
#include <string.h>
struct X { size_t n; char first; };
X operator""_x(const char *s, size_t n) { return X{n, s[0]}; }
size_t operator""_w(const wchar_t *s, size_t n) { return n * 10 + (s[0] == L'q'); }
int main() {
	const char *r = R"x(a)"b)x";
	const wchar_t *w = L"hé";
	const char16_t *u = u"\U0001F600";
	const char32_t *U = U"z";
	X a = "abc"_x, b = R"(q"z)"_x, d = "ab" "cd"_x;
	int ok = !strcmp(r, "a)\"b") && w[1] == 0xe9 && u[0] == 0xD83D && u[1] == 0xDE00 && U[0] == 'z' &&
		 sizeof("ab\n") == 4 && sizeof(u"ab") == 6 && !strcmp("é", "\xc3\xa9") && sizeof(R"(\n)") == 3;
	return ok && L'q' == 0x71 && u'\u00e9' == 0xe9 && U'z' == 0x7a && sizeof(L'a') == sizeof(wchar_t) && '\n' == 10 && a.n == 3 && a.first == 'a' && b.n == 3 && b.first == 'q' && d.n == 4 && L"qw"_w == 21 ? 0 : 1;
}
