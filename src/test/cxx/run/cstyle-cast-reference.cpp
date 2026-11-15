// (T&)x of an lvalue of another type: reinterpret_cast<T&>(x), the same bytes as T (libc++'s
// operator>>(istream&, unsigned char&): is >> (char&)c)
void set(char &c) { c = 'z'; }
int main()
{
	unsigned char u = 0;
	set((char &)u);
	int i = 0x01020304;
	((unsigned char &)i) = 0xff;
	return u == 'z' && i == 0x010203ff ? 0 : 1;
}
