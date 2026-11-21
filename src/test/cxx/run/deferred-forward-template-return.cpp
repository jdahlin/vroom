// [temp.inst]/2: a member declaration can name an incomplete specialization.
template<class T, bool = __is_trivially_destructible(T)>
struct Storage { T value; };

template<class T>
struct Box : Storage<T> {};

template<class T>
struct Handle { T *ptr; };

struct Forward;
struct Factory {
    Box<Forward> make() const;
    Handle<Forward> handle;
};

struct Forward { int value; };

Box<Forward> Factory::make() const { return {{42}}; }

int main()
{
    Factory factory{};
    return factory.make().value.value == 42 ? 0 : 1;
}
