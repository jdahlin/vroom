/* [temp.class.spec]/1: prior instantiations for other arguments do not
   prevent a variable template's partial specializations. */
struct PlusTag {};
struct MinusTag {};
template<class T> struct Plus {};
template<class T> struct Minus {};

template<class Tag, class Op, class L, class R>
inline constexpr bool desugars_to = false;

template<class T>
inline constexpr bool desugars_to<MinusTag, Minus<T>, T, T> = true;

static_assert(!desugars_to<PlusTag, Plus<int>, int, long>);
static_assert(desugars_to<MinusTag, Minus<int>, int, int>);

template<class T>
inline constexpr bool desugars_to<PlusTag, Plus<T>, T, T> = true;

template<class L, class R>
inline constexpr bool desugars_to<PlusTag, Plus<void>, L, R> = true;

static_assert(!desugars_to<PlusTag, Plus<int>, int, long>);
static_assert(desugars_to<MinusTag, Minus<int>, int, int>);
static_assert(desugars_to<PlusTag, Plus<int>, int, int>);
static_assert(desugars_to<PlusTag, Plus<void>, int, long>);

int main() {}
