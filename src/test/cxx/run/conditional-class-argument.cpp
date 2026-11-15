// a class argument passed by value (a temporary with a destructor, converted from another type) in an operand
// that may not run (json's strict ? parser(..., nullptr, ...).sax_parse(...) : ...): built and destroyed only
// when its branch runs
int made, gone;
struct F { int k; F(int x) : k(x) { made++; } F(const F &o) : k(o.k) { made++; } ~F() { gone++; } };
int take(F f) { return f.k; }
int pick(bool b) { return b ? take(7) : 0; }
int main()
{
	int r = 0;
	{
		r = pick(true) + pick(false) + pick(true);
	}
	return r == 14 && made == gone && made == 2 ? 0 : 1;
}
