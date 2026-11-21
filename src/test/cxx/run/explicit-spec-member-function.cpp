// Explicit specialization of non-template member functions of a class template
// specialization ([temp.expl.spec]/1), declared via a type alias and defined
// out of line.

template <int Tag>
struct Box {
    int value;
    static Box make(int v);
    int get() const;
};

using BoxA = Box<1>;

template <> __attribute__((visibility("default"))) BoxA BoxA::make(int v);
template <> __attribute__((visibility("default"))) int BoxA::get() const;

template <> BoxA BoxA::make(int v) { return BoxA{v * 2}; }
template <> int BoxA::get() const { return value + 3; }

int main() {
    BoxA b = BoxA::make(5);
    return b.get() == 13 ? 0 : 1;
}
