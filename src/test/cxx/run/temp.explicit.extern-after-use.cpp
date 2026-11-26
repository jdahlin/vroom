// [temp.explicit]/11: an explicit instantiation declaration (extern template)
// after a call to an undefined function template specialization.
template <typename T>
T f(T x);

inline int call_f(int x)
{
    return f<int>(x);
}

extern template int f<int>(int);

template <typename T>
T f(T x)
{
    return x + 1;
}

template int f<int>(int);

int main()
{
    return call_f(41) == 42 ? 0 : 1;
}
