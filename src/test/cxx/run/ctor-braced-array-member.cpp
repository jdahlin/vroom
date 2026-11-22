// [class.base.init]/7: brace-initialize an array member in a constructor.
struct Matrix {
    float values[3];
    int marker;
    constexpr Matrix(float x, float y) : values{x, y}, marker(7) {}
};

int main()
{
    Matrix m(2.5f, 4.5f);
    return m.values[0] == 2.5f && m.values[1] == 4.5f &&
                   m.values[2] == 0.0f && m.marker == 7
               ? 0
               : 1;
}
