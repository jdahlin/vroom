// std: c++20
// this in an out-of-class member's body points to its own class, not the class whose inline body
// was parsed last (Skia's SkPixmap::getColor: switch (this->colorType()))
enum CT : int { kA, kB };
struct CI {
	CT ct = kB;
	CT colorType() const { return ct; }
};
struct PM {
	int pad = 7;
	CI info;
	CT colorType() const { return info.colorType(); }
	int getColor(int x) const;
	int bump();
};
struct Other {
	long a = 1, b = 2;
	long sum() const { return this->a + this->b; }
};
int PM::getColor(int x) const
{
	switch (this->colorType()) {
	case kA:
		return 1;
	default:
		return x + (*this).pad;
	}
}
int PM::bump() { return ++this->pad; }
int main()
{
	PM p;
	Other o;
	if (p.getColor(3) != 10 || p.bump() != 8 || o.sum() != 3)
		return 1;
	return 0;
}
