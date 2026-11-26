// [temp.expl.spec]: explicit specialization of a const-qualified member
// function template overloaded with a non-const member function template
// (e.g. Window::GetWidget<NWidgetBase>(WidgetID) const in OpenTTD).

struct Base {
    int id;
};

struct Window {
    Base b{42};
    template <class T> T *Get(int) { return nullptr; }
    template <class T> const T *Get(int) const { return nullptr; }
};

template <> inline const Base *Window::Get<Base>(int) const { return &this->b; }

int main()
{
    const Window w;
    const Base *p = w.Get<Base>(0);
    if (!p || p->id != 42)
        return 1;
    return 0;
}
