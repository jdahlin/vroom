// [expr.cond]/2.2: both operands of type void: the result is a void prvalue
// (vcc's own x86-64 assembler printer writes `c ? f() : (void)0;`)
static int log_count;
static int log_sum;
void note(int v) { log_count++; log_sum += v; }
void other(int v) { log_sum += 100 * v; }
int pick(bool b)
{
	b ? note(1) : other(2);
	b ? note(3) : (void)0;
	(void)(b ? other(1) : note(5));
	return 0;
}
template <class T> struct is_void_t { static constexpr bool value = false; };
template <> struct is_void_t<void> { static constexpr bool value = true; };
static_assert(is_void_t<decltype(true ? note(1) : other(1))>::value, "void result");
static_assert(is_void_t<decltype(false ? (void)0 : note(1))>::value, "void result");
int main()
{
	pick(true);
	pick(false);
	// true: note(1) note(3) other(1); false: other(2) note(5)
	// count 3, sum 1 + 3 + 100 + 200 + 5
	return (log_count == 3 && log_sum == 309) ? 0 : 1;
}
