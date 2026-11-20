// [temp.deduct.type]/5.5: a value-dependent argument of a variable template's partial specialization
// (<T, __is_integral(T)>) is a non-deduced context; the match is the substituted arguments'
template <class T, bool B> constexpr int kind = 0;
template <class T> constexpr int kind<T, __is_integral(T)> = 1;
int main() {
	int r = 0;
	if (kind<int, true> != 1) r |= 1;
	if (kind<double, false> != 0) r |= 2;
	return r;
}
