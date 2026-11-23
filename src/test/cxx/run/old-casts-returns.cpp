// const_cast to a reference (a class reached through a typedef too) and return f(); in a void function
template <class P> struct End { P left; };
template <class T> struct Tree {
	using end_t = End<T *>;
	end_t e_;
	end_t *end() const { return &const_cast<end_t &>(e_); }
};

static int n;
static void g() { n++; }
static void f() { return g(); }
int main() { Tree<int> t; t.e_.left = nullptr; f(); return t.end() == &t.e_ && n == 1 ? 0 : 1; }
