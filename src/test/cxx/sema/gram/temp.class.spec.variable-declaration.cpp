// [temp.class.spec]/1: a partial specialization of a variable template declared without an initializer, then an explicit specialization and instantiation of the template (declarations)
namespace P {
template <typename T> int mt;
template <typename T> int mt<T *>;
template <> int mt<int>;
template int mt<int *>;
}
namespace Q {
template <typename T> int nt;
}
template <typename T> int Q::nt<T *>;
