// [over.over]/2-5, [temp.deduct.funcaddr]: a function template's name (or the set of it and non-templates)
// as an argument: the specialization deduced from the parameter's function type
template <class T> T tg(T x) { return x + 1; }
template <class T> T tg(T x, T y) { return x + y; }
int plain(int x) { return x * 10; }
template <class T> T mixed(T x) { return x - 1; }
int mixed(int x) { return x + 100; }
int call(int (*p)(int)) { return p(3); }
double calld(double (&p)(double)) { return p(1.5); }
int binary(int (*p)(int, int)) { return p(2, 5); }
int pick(int (*p)(int)) { return p(1); }
int pick(const char *) { return 1000; }
int main()
{
	int r = 0;
	r += call(tg);            // tg<int>(3): 4
	r += call(&tg);           // 4
	r += (int)calld(tg);      // tg<double>(1.5): 2.5 -> 2
	r += binary(tg);          // tg<int>(2, 5): 7
	r += pick(tg);            // tg<int>(1): 2
	r += call(mixed);         // the non-template wins over the specialization ([over.over]/5): 103
	return r == 4 + 4 + 2 + 7 + 2 + 103 ? 0 : 1;
}
