/* [abi:expressions]: a member template named in its class's signatures
   (Highway's Status::IsStatusEnum<Enum>()) */
template<bool B> struct En{}; template<> struct En<true>{using type=void;};
struct S { template<class T> static constexpr bool Is(){ return true; }
  template<class E, typename En<Is<E>()>::type* = nullptr> S(E){}
  template<class E, typename En<Is<E>()>::type* = nullptr> static void f(E){} };
int main() { S s(1); S::f(2); return 0; }
