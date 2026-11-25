struct root {
    int x = 42;
};

template <class T>
struct middle : public root {
    int get() const { return x; }
};

template <class Base>
struct derived : public Base {
    int own() const { return 1; }
};

int check(derived<middle<int>> *p) {
    return p->own() + p->get();
}

int main() {
    derived<middle<int>> d;
    return check(&d) == 43 ? 0 : 1;
}
