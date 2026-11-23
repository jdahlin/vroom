/* [ext:gnu_attributes] in a lambda-declarator, right after its parameters
   (libc++'s __load_vector, with always_inline: [=] in a function template,
   invoked at once) */
template <class T> int load(T x)
{
    return [=](int a) __attribute__((__unused__)) noexcept {
        return a + x;
    }(2);
}

int main()
{
    return load(3) == 5 ? 0 : 1;
}
