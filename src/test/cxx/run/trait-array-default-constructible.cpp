// __is_constructible(T[N]) is as T's (char[8] in json's is_getable<json, char[8]>, j.value("k", "default"));
// an array of unknown bound is not default-constructible
struct D { D() {} };
struct N { N(int) {} };
template <bool B> struct W { static constexpr int v = B ? 1 : 2; };
int main()
{
	return W<__is_constructible(char[8])>::v == 1 && W<__is_constructible(int[2][3])>::v == 1 && W<__is_constructible(D[4])>::v == 1 &&
	       W<__is_constructible(N[4])>::v == 2 && W<__is_constructible(int[])>::v == 2 ? 0 : 1;
}
