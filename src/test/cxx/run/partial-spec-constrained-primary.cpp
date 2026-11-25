template <class T>
concept box_obj = sizeof(T) > 0;

template <class T>
concept small_obj = sizeof(T) <= 4;

template <box_obj T>
struct box {
    static constexpr int kind = 1;
};

template <box_obj T>
requires small_obj<T>
struct box<T> {
    static constexpr int kind = 2;
};

int main() {
    if (box<int>::kind != 2)
        return 1;
    if (box<double>::kind != 1)
        return 2;
    return 0;
}
