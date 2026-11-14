// Type traits vcc evaluates: each static_assert is true in vcc (VCC_CXX_STATIC_ASSERT=1), clang accepts.
struct P { int a; char b; };
struct E { };
enum C { c0 };
static_assert(__is_trivially_copyable(int) && __is_trivially_copyable(char *) && __is_trivially_copyable(int[4]), "");
static_assert(__is_trivially_copyable(P) && __is_standard_layout(P) && __is_trivial(P) && __is_pod(P), "");
static_assert(__is_trivially_copyable(C) && __is_trivially_destructible(C), "");
static_assert(__is_empty(E) && !__is_empty(P) && !__is_empty(int), "");
static_assert(!__is_polymorphic(int) && !__is_abstract(C), "");
static_assert(__is_same(int, int) && !__is_same(int, const int), "");
static_assert(__is_pointer(int *) && !__is_pointer(int &) && __is_lvalue_reference(int &) && __is_rvalue_reference(int &&), "");
static_assert(__is_const(const int) && !__is_const(const int *) && __is_enum(C) && __is_class(P) && !__is_class(C), "");
static_assert(__is_signed(int) && !__is_signed(unsigned) && __is_unsigned(unsigned long) && __is_floating_point(double), "");
static_assert(__is_array(int[2]) && !__is_array(int *) && __is_function(void(int)) && __is_void(void), "");

// enumerators: the enumeration's type, their values
enum class V : unsigned char { null, object, array = 7, string, last = string + 2 };
enum U { u0, u1 = u0 + 4, u2 };
template<int N> struct A { char c[N]; };
static_assert(sizeof(V::object) == 1 && sizeof(u1) == 4, "");
static_assert(V::null == V::null && V::object != V::array && V::array < V::string, "");
static_assert(static_cast<int>(V::string) == 8 && static_cast<int>(V::last) == 10 && u2 == 5 && u1 + 1 == 5, "");
static_assert(sizeof(A<u2>) == 5 && sizeof(A<static_cast<int>(V::array)>) == 7, "");
char pick(V);
long pick(int);
static_assert(sizeof(pick(V::object)) == 1 && sizeof(pick(u1)) == 8, "");

// how two types relate; packs in builtin calls; an explicit pack of a function template
struct Base { }; struct Derived : Base { }; struct Other { };
static_assert(__is_base_of(Base, Derived) && __is_base_of(Base, Base) && !__is_base_of(Derived, Base) && !__is_base_of(Base, Other), "");
static_assert(__is_convertible(int, long) && __is_convertible(Derived *, Base *) && !__is_convertible(Base *, Derived *), "");
static_assert(!__is_convertible(int *, long *) && __is_convertible(int *, const int *) && __is_convertible(int *, void *), "");
static_assert(__is_constructible(int, long) && __is_constructible(int) && !__is_constructible(int &) && !__is_constructible(int *, long *), "");
template<class T, T v> struct ic { static constexpr T value = v; };
template<class T, class... Args> struct constructible : ic<bool, __is_constructible(T, Args...)> { };
template<class... P> struct count : ic<int, sizeof...(P)> { };
static_assert(constructible<int, int>::value && constructible<int>::value && !constructible<int *, long>::value, "");
static_assert(count<>::value == 0 && count<int, long, char>::value == 3, "");
template<bool, class T = void> struct en { };
template<class T> struct en<true, T> { typedef T type; };
struct yes { static constexpr bool value = true; }; struct no { static constexpr bool value = false; };
template<class...> using to_yes = yes;
template<class... P> to_yes<typename en<P::value>::type...> all_helper(int);
template<class...> no all_helper(...);
template<class... P> using all = decltype(all_helper<P...>(0));
static_assert(all<yes, yes>::value && !all<yes, no>::value && all<>::value, "");

/* a class from an object of its own type: declared, deleted, defaulted and implicit special members */
struct sp_plain { int a; };
struct sp_nocopy { sp_nocopy(const sp_nocopy &) = delete; sp_nocopy &operator=(const sp_nocopy &) = delete; };
struct sp_moveonly { sp_moveonly(sp_moveonly &&); sp_moveonly &operator=(sp_moveonly &&); };
struct sp_holds { sp_nocopy m; };
struct sp_derived : sp_moveonly { };
struct sp_constmember { const int c; };
struct sp_user { sp_user(const sp_user &); sp_user &operator=(const sp_user &); };
static_assert(__is_constructible(sp_plain, sp_plain &&), "");
static_assert(__is_constructible(sp_plain, const sp_plain &), "");
static_assert(__is_assignable(sp_plain &, sp_plain &&), "");
static_assert(!__is_constructible(sp_nocopy, sp_nocopy &&), "");
static_assert(!__is_assignable(sp_nocopy &, const sp_nocopy &), "");
static_assert(__is_constructible(sp_moveonly, sp_moveonly &&), "");
static_assert(!__is_constructible(sp_moveonly, const sp_moveonly &), "");
static_assert(!__is_assignable(sp_moveonly &, const sp_moveonly &), "");
static_assert(!__is_constructible(sp_holds, sp_holds &&), "");
static_assert(__is_constructible(sp_derived, sp_derived &&), "");
static_assert(!__is_constructible(sp_derived, const sp_derived &), "");
static_assert(__is_constructible(sp_constmember, sp_constmember &&), "");
static_assert(!__is_assignable(sp_constmember &, sp_constmember &&), "");
static_assert(__is_constructible(sp_user, sp_user &&), "");
static_assert(__is_assignable(sp_user &, sp_user &&), "");
static_assert(__is_assignable(int &, int), "");
static_assert(!__is_assignable(const int &, int), "");
static_assert(!__is_assignable(int, int), "");
