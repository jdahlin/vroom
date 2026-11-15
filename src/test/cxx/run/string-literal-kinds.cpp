// raw, wide, UTF-16 and UTF-32 literals and their character constants: element types and sizes, \u and UTF-8
// decoded into wider elements, surrogate pairs
int f(const char *p) { return *p == 'x' ? 0 : 1; }
int g(const wchar_t *p) { return *p == L'y' ? 0 : 1; }
static_assert(sizeof(L"ab") == 12, "");
static_assert(sizeof(u"a\U0001F600") == 8, "");
static_assert(sizeof(R"x(a")b)x") == 5, "");
int k()
{
	const char16_t *p = u"é";
	const char32_t *q = U"é";
	const wchar_t *w = LR"(z)";
	const char *r = R"({"a": 1})";
	return (*p == 0xe9 && *q == 0xe9 && *w == L'z' && sizeof("é") == 3 && r[1] == '"' && u'x' == 120) ? 0 : 1;
}
int main() { return k() + f(R"(x)") + g(L"y"); }
