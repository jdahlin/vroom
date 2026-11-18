/* cxx-run-flags: -std=c++17 */
/* C++17's [except.spec]/3: "The noexcept-specifier throw() is equivalent
   to noexcept(true)", part of the type: a pointer to it converts to a
   pointer to function ([conv.fctptr]); a deallocation function declared
   without a noexcept-specifier is non-throwing ([except.spec]/12) */
int empty_throw() throw() { return 3; }
int call(int (*f)() noexcept) { return f(); }
void operator delete(void *) noexcept;
int main()
{
    int (*p)() = empty_throw;
    return call(empty_throw) + p() - 6;
}
