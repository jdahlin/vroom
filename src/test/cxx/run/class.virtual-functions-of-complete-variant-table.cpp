/* [class.virtual]: virtual functions named only by the tables that an
   inline complete object constructor stores are defined. */
struct surface {
    long d = 0;
    surface(int) {}
    virtual int format() const { return 1; }
};
struct target : virtual surface {
    long w = 0;
    inline target();
    virtual int stencil() const { return 5; }

private:
    virtual void label() {}
};
target::target() : surface(1) {}
struct gpu {
    target *make();
};
target *gpu::make() { return new target; }
int main()
{
    gpu g;
    target *p = g.make();
    return p->format() + p->stencil() - 6;
}
