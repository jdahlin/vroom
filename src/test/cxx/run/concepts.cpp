/* cxx-run-flags: -std=c++20 */
/* [temp.concept], [temp.names]/8, [temp.constr]: concept-ids are bool
   constants, conjunctions and disjunctions short-circuit, a substitution
   failure leaves an atomic constraint unsatisfied; [temp.deduct]/5: a
   function template whose requires-clause is not satisfied is not a
   candidate, and templates differing only in it are overloads */
template <class T, class U> struct same {
    static constexpr bool value = false;
};
template <class T> struct same<T, T> {
    static constexpr bool value = true;
};
template <class T> concept Small = sizeof(T) <= 4;
template <class T, class U> concept Same = same<T, U>::value;
template <class T> concept HasType = sizeof(typename T::type) > 0;
template <class T> concept Either = Small<T> || HasType<T>;
template <class T> concept Both = (Small<T>) && Same<T, int>;

struct with_type {
    typedef long type;
    char big[16];
};

static_assert(Small<int> && !Small<double>);
static_assert(Same<int, int> && !Same<int, long>);
static_assert(HasType<with_type> && !HasType<int>);
static_assert(Either<char> && Either<with_type> && !Either<double>);
static_assert(Both<int> && !Both<char>);

template <class T> requires Small<T> int f(T) { return 1; }
template <class T> requires (!Small<T>) int f(T) { return 2; }
template <class T> requires HasType<T> int g(T) { return 3; }
template <class T> requires (!HasType<T>) int g(T) { return 4; }

int main()
{
    if (f(1) != 1 || f(1.0) != 2 || f('c') != 1)
        return 1;
    if (g(with_type()) != 3 || g(2) != 4)
        return 2;
    return 0;
}
