// [over.over]/1 for the operand of an operator function: a manipulator as std::endl is, a function template
// (and overloaded functions) whose address is the right operand of operator<<
template <class C> struct Out { int n = 0; C last = 0; };
template <class C> Out<C> &endl(Out<C> &o) { o.n += 10; return o; }
template <class C> Out<C> &flush(Out<C> &o) { o.n += 100; return o; }
Out<char> &ws(Out<char> &o) { o.n += 1000; return o; }
Out<wchar_t> &ws(Out<wchar_t> &o) { o.n += 5000; return o; }
struct Stream : Out<char> {
	Stream &operator<<(Out<char> &(*p)(Out<char> &)) { p(*this); return *this; }
	Stream &operator<<(int v) { n += v; return *this; }
};
// a free operator<<
struct Free { int n = 0; };
Free &operator<<(Free &f, Free &(*p)(Free &)) { return p(f); }
Free &operator<<(Free &f, int v) { f.n += v; return f; }
Free &bump(Free &f) { f.n += 7; return f; }
Free &bump(Free &f, int) { return f; }
int main()
{
	Stream s;
	s << 1 << endl << flush << 2 << ws << endl;  // 1 + 10 + 100 + 2 + 1000 + 10
	Free f;
	f << bump << 3 << bump;                      // 7 + 3 + 7
	return s.n == 1123 && f.n == 17 ? 0 : 1;
}
