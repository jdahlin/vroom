// [dcl.fct.spec]/2, [expr.const]/11: the explicit-specifier's condition of a constructor
// template calls constexpr static member functions of a class template's specialization
// (a member and a member function template) declared before the class is complete
template <class T> struct Traits {
	static constexpr bool plain() { return sizeof(T) == 4; }
	template <class U> static constexpr bool implicit() { return sizeof(U) <= sizeof(T); }
};
struct Plain {
	template <class D = int> explicit(!Traits<int>::plain()) Plain(D d) : v(d) {}
	int v;
};
struct Templ {
	template <class D = int> explicit(!Traits<int>::implicit<char>()) Templ(D d) : v(d) {}
	int v;
};
struct Explicit {
	template <class D = int> explicit(!Traits<char>::implicit<long>()) Explicit(D d) : v(d) {}
	int v;
};
int g(Templ t) { return t.v; }
int main()
{
	Plain a(1);
	Templ b = 2;
	Explicit c(3);
	
	return a.v + b.v + c.v + g(4) - 10;
}
