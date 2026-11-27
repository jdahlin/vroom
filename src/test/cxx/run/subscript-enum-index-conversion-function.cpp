// [over.built]/14, [over.match.best]: a member operator[] beats the built-in
// subscript through a conversion function for an unscoped enumeration index too.
class Array
{
  public:
    operator int *() { return p; }
    operator const int *() const { return p; }
    int &operator[](int i) { return p[i]; }
    const int &operator[](int i) const { return p[i]; }

  private:
    int p[3] = {1, 2, 3};
};

enum Index { first, second, third };
enum Fixed : unsigned char { last = 2 };

static int get(const Array &a) { return a[second] + a[last]; }

int main()
{
    Array a;
    a[first] = 10;
    return a[first] + get(a) - 15;
}
