/* [class.access]/6: a member defined outside its class names the class's
   private types in the decl-specifier-seq before its qualified
   declarator-id as the class's members do. */
class Registry {
    struct Entry {
        int key;
        int value;
    };
    Entry entries[2];
    typedef Entry *EntryPtr;

  public:
    Registry()
    {
        entries[0].key = 1;
        entries[0].value = 10;
        entries[1].key = 2;
        entries[1].value = 20;
    }
    const Entry *find(int key) const;
    static EntryPtr none();
    int sum(const Entry &extra) const;
    int total() const;
};

const Registry::Entry *Registry::find(int key) const
{
    for (const Entry &e : entries)
        if (e.key == key)
            return &e;
    return 0;
}

Registry::EntryPtr Registry::none()
{
    return 0;
}

int Registry::sum(const Entry &extra) const
{
    return entries[0].value + entries[1].value + extra.value;
}

int Registry::total() const
{
    const Entry *e = find(2);
    if (!e || none())
        return -1;
    Entry extra = {0, 5};
    return sum(extra) + e->value;
}

int main()
{
    Registry r;
    if (r.total() != 55)
        return 1;
    return 0;
}
