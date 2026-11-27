/* [class.pre]/2: a class's own injected-class-name hides a base's of the
   same name (SkBitmapCache::Rec : SkResourceCache::Rec). */
extern "C" int printf(const char *, ...);

struct Cache {
    struct Rec {
        int base = 1;
    };
};
class Bitmap
{
  public:
    class Rec;
};
class Bitmap::Rec : public Cache::Rec
{
  public:
    static int read(void *context)
    {
        Rec *r = static_cast<Rec *>(context);
        return r->own * 10 + r->base;
    }
    int own = 2;
};

int main()
{
    Bitmap::Rec r;
    printf("%d\n", Bitmap::Rec::read(&r));
    return 0;
}
