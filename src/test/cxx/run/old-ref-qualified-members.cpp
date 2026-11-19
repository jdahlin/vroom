// ref-qualified member functions: f() & takes an lvalue object only, f() && an rvalue one (I().get(),
// static_cast<I &&>(i).get()); both are their own functions, mangled R and O; json's iterators'
// operator++(int) & makes (x++)++ ill-formed, which is_detected checks
template <class T> T &&declval();
struct I { int v = 0; I operator++(int) & { I o = *this; v++; return o; } I &operator++() & { v++; return *this; } int get() & { return 1; } int get() && { return 2; } };
template <class T, class = void> struct can_pp { static constexpr bool value = false; };
template <class T> struct can_pp<T, decltype((void)((declval<T>()++)++))> { static constexpr bool value = true; };
template <class T, class = void> struct can_pre { static constexpr bool value = false; };
template <class T> struct can_pre<T, decltype((void)(++(declval<T>()++)))> { static constexpr bool value = true; };
int main() {
	I i; i++; ++i;
	return !can_pp<I &>::value && !can_pre<I &>::value && i.v == 2 && i.get() == 1 && I().get() == 2 &&
			       static_cast<I &&>(i).get() == 2
		       ? 0
		       : 1;
}
