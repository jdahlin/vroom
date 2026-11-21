// [basic.lval]/7, [expr.ref]/6: member access and member function calls on
// an aggregate prvalue temporary (T{...}.member(), T{}.field).
struct Rect {
    int left, top, right, bottom;
    int width() const { return right - left; }
    int intersect_sum(int a, int b) {
        left = a;
        right = b;
        return right - left;
    }
    template <class T> T add(T x) const { return static_cast<T>(left) + x; }
};

int main() {
    int a = Rect{1, 2, 10, 20}.width();
    int b = Rect{}.intersect_sum(3, 8);
    int c = Rect{4, 0, 0, 0}.left;
    int d = Rect{6, 0, 0, 0}.add(5);
    return a != 9 || b != 5 || c != 4 || d != 11;
}
