// std: c++17
// a program's own functions named sqrt and ceill, called with literals, are called: only
// __builtin_sqrt and __builtin_ceill fold ([ext:gnu_library_builtins]); lower/ once folded any
// function spelled sqrt (my::sqrt(4.0) gave 2)
namespace my {
	double sqrt(double x) { return x + 1; }
	long double ceill(long double x) { return x * 2; }
}
double sqrt(double x, int) { return x; }
int main()
{
	if (my::sqrt(4.0) != 5.0 || my::ceill(2.5L) != 5.0L)
		return 1;
	if (__builtin_sqrt(16.0) != 4.0 || __builtin_ceill(2.5L) != 3.0L)
		return 2;
	return 0;
}
