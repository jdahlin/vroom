// while (T x = e) declares x again each iteration; switch (T x = e) too (libc++'s typeinfo __hash)
int sum(const char *p) {
	int n = 0;
	while (unsigned char c = static_cast<unsigned char>(*p++)) {
		if (c == 'x')
			continue;
		n += c;
	}
	return n;
}
int pick(int k) {
	switch (int v = k * 2) {
	case 4: return v + 1;
	default: return v;
	}
}
int main() { return sum("abxc") == 294 && pick(2) == 5 && pick(3) == 6 ? 0 : 1; }
