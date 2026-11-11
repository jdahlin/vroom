const _BitInt(256) ordinary=123;
constexpr _BitInt(256) invalid=ordinary+1;
constexpr int invalid_compare=ordinary==123;
constexpr _BitInt(256) huge=0x100000000000000000000000000000000000000000000000001wb;
constexpr unsigned small=huge;
constexpr double imprecise=huge;
constexpr _BitInt(129) maximum=0xffffffffffffffffffffffffffffffffuwb;
constexpr _BitInt(129) overflow=maximum+1;
