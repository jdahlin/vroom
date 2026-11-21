// Member function returning a class template specialization of the enclosing
// class itself ([temp.inst]/2): std::optional<Self>-style traits must not
// be instantiated until Self's layout is complete.

template <class T, bool Trivial = __is_trivially_destructible(T)>
struct OptBase {
    T val;
    bool engaged;
    constexpr T value() const { return val; }
};

template <class T>
struct Opt : OptBase<T> {
    constexpr Opt(T v) : OptBase<T>{v, true} {}
    constexpr explicit operator bool() const { return this->engaged; }
};

struct Rect {
    int w;
    int h;

    static Opt<Rect> make(int w, int h);

    static Rect makeOrEmpty(int w, int h) {
        if (auto r = make(w, h))
            return r.value();
        return Rect{0, 0};
    }
};

Opt<Rect> Rect::make(int w, int h) { return Opt<Rect>(Rect{w, h}); }

int main() {
    Rect r = Rect::makeOrEmpty(3, 4);
    return (r.w == 3 && r.h == 4) ? 0 : 1;
}
