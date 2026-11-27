/* [class.member.lookup]/6.1: an override in one base hides the virtual
   base's declaration the other base reaches (GrTextureRenderTargetProxy). */
extern "C" int printf(const char *, ...);

struct Surface {
    virtual ~Surface() {}
    virtual int key() const { return 1; }
    int plain() const { return five; }
    int five = 5;
};
struct Target : virtual Surface {};
struct Texture : virtual Surface {
    int key() const override { return 2; }
};
struct Both : Target, Texture {
    int read() const { return this->key() * 10 + plain(); }
};
struct Reversed : Texture, Target {
    int read() const { return key(); }
};

int main()
{
    Both b;
    Reversed r;
    printf("%d %d %d\n", b.read(), r.read(), b.key());
    return 0;
}
