// std: c++20
// a friend function named by a qualified name: it is the function of the namespace declared
// before ([class.friend]/1, [dcl.meaning]/1), and it may reach the class's private members
namespace ns {
	struct Box;
	int peek(const Box &);
	namespace inner {
		long twice(long);
	}
	struct Box {
		friend int ns::peek(const Box &);
		friend long ns::inner::twice(long);
		friend void touch(Box &);
	private:
		int secret = 41;
	};
	int peek(const Box &b) { return b.secret + 1; }
	namespace inner {
		long twice(long v) { return v * 2; }
	}
}
int main()
{
	ns::Box b;
	if (ns::peek(b) != 42 || ns::inner::twice(4) != 8)
		return 1;
	return 0;
}
