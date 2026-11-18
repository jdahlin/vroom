/* [class.mem]/6: a member function's noexcept-specifier is a complete-class
   context, so its constant-expression may name a member declared after it;
   deferring it to the class's end is not implemented, so the expression
   form is refused in a class (noexcept alone is analysed). */
struct A {
    void f() noexcept(A::value);
    void g() noexcept;
    static constexpr bool value = true;
};
