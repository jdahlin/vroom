/* enumerations and switch */
enum Color { Red, Green = 5, Blue };
enum class Op : unsigned char { Add, Sub, Mul };
namespace n { enum class Level { Low = 1, High = 10 }; int weight(Level l) { return l == Level::High ? 100 : 1; } }
int apply(Op op, int a, int b) {
	switch (op) {
	case Op::Add: return a + b;
	case Op::Sub: return a - b;
	case Op::Mul: return a * b;
	}
	return 0;
}
int classify(int x) {
	int r = 0;
	switch (x) {
	case 0: r = 10; break;
	case 1:
	case 2: r = 20; break;
	case 3 ... 9: r = 30;
	case 100: r += 1; break;
	default: r = -1;
	}
	return r;
}
int color_value(Color c) { return c; }
Color next(Color c) { switch (c) { case Red: return Green; case Green: return Blue; default: return Red; } }
Op parse(char ch) { if (ch == '+') return Op::Add; if (ch == '-') return Op::Sub; return Op::Mul; }
int main() {
	if (apply(Op::Add, 2, 3) != 5 || apply(Op::Sub, 2, 3) != -1 || apply(Op::Mul, 2, 3) != 6) return 1;
	if (classify(0) != 10 || classify(2) != 20 || classify(5) != 31 || classify(100) != 1 || classify(50) != -1) return 2;
	if (color_value(Blue) != 6 || next(Red) != Green || next(Blue) != Red) return 3;
	if (apply(parse('-'), 9, 4) != 5) return 4;
	if (n::weight(n::Level::High) != 100 || n::weight(n::Level::Low) != 1) return 5;
	Color c = Green; if (c != 5) return 6;
	return 0;
}
