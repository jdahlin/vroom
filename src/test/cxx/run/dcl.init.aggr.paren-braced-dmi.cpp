// Parenthesized braced-init-list for an aggregate with default member
// initializers (e.g. CursorSprite : image({spr, pal}) in OpenTTD gfx_type.h:119).

struct PalSpriteID {
    unsigned int sprite{};
    unsigned int pal{};
};

struct Coord2D {
    int x = 0;
    int y = 0;
    constexpr Coord2D() = default;
    constexpr Coord2D(int x, int y) : x(x), y(y) {}
};

struct CursorSprite {
    PalSpriteID image;
    Coord2D pos;
    constexpr CursorSprite(unsigned int spr, unsigned int pal, int x, int y)
        : image({spr, pal}), pos({x, y})
    {
    }
};

int main()
{
    constexpr CursorSprite c(10, 20, 3, 4);
    static_assert(c.image.sprite == 10 && c.image.pal == 20 && c.pos.x == 3 &&
                  c.pos.y == 4);
    PalSpriteID p({30, 40});
    if (p.sprite != 30 || p.pal != 40)
        return 1;
    return 0;
}
