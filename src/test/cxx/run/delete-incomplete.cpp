// [expr.delete]/5: deleting a pointer to an incomplete class type is
// well-formed (defined behavior when the complete class has a trivial
// destructor and no class-specific deallocation function).

struct Incomplete;

void delete_one(Incomplete *p)
{
    delete p;
}

void delete_arr(Incomplete *p)
{
    delete[] p;
}

struct PoolBase {
    virtual void clean() = 0;
    virtual ~PoolBase() = default;
};

template <typename T>
struct Pool : PoolBase {
    T *item = nullptr;
    void clean() override;
};

struct ForwardOnly;

template <typename T>
void Pool<T>::clean()
{
    delete item;
    item = nullptr;
}

void use_forward_only()
{
    Pool<ForwardOnly> unused_pool;
    unused_pool.clean();
}

struct Incomplete {
    int x;
    int y;
};

int main()
{
    Incomplete *p = new Incomplete{1, 2};
    delete_one(p);
    delete_one(nullptr);

    Incomplete *a = new Incomplete[3];
    delete_arr(a);
    delete_arr(nullptr);

    Pool<Incomplete> pool;
    pool.item = new Incomplete{3, 4};
    pool.clean();
    if (pool.item != nullptr)
        return 1;

    return 0;
}
