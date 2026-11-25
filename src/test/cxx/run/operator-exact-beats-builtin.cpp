struct box {
    int v;
    template <class T>
    operator T() const {
        return static_cast<T>(v);
    }
    int operator[](const char *s) const {
        return v + s[0];
    }
};

int main() {
    box b{10};
    return b["A"] == 75 ? 0 : 1;
}
