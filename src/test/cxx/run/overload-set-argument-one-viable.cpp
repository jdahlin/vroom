// [over.over]/1-2: the name of an overloaded function as an argument: the function whose type is the
// parameter's pointee type, when one is viable among the candidates ([over.match.viable], [over.ics.rank])
int g(int x) { return x + 1; }
double g(double x) { return x * 2; }
int call(int (*p)(int)) { return p(3); }
double calld(double (&p)(double)) { return p(1.5); }
// two candidates: the function parameter of one only accepts the set
int pick(int (*p)(int)) { return p(1); }
int pick(const char *) { return 100; }
// a pointer to the function that the overload set resolves to
int twice(int (*p)(int), int v) { return p(p(v)); }
int main()
{
	int r = 0;
	r += call(g);             // 4
	r += call(&g);            // 4
	r += (int)calld(g);       // 3
	r += pick(g);             // 2
	r += twice(g, 1);         // 3
	int (*q)(int) = g;
	r += q(0);                // 1
	return r == 17 ? 0 : 1;
}
