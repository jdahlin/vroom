/* an inherited constexpr member called on a parameter of an empty class
   (Highway's st.LanesPerKey()) */
struct Base { constexpr unsigned long LanesPerKey() const { return 1; } };
struct Mid : Base { static constexpr unsigned long Static() { return 2; } };
template <class T> struct Shared : T {};
template <class Traits> unsigned long f(Traits st) {
    constexpr unsigned long N1 = st.LanesPerKey();
    constexpr unsigned long N2 = st.Static();
    return N1 + N2;
}
int main() { return f(Shared<Mid>()) != 3; }
