/* #pragma clang fp contract and reassociate change no constant's value:
   floating arithmetic is still a constant expression (Highway's
   constexpr double kOld = 1.0 - kNew) */
#pragma clang fp contract(off)
#pragma clang fp reassociate(off)
#pragma STDC FP_CONTRACT ON

int main()
{
    constexpr double kNew = 0.2;
    constexpr double kOld = 1.0 - kNew;
    static_assert(kOld > 0.79 && kOld < 0.81, "folded");
    constexpr float third = 1.0f / 3;
    return kOld != 0.8 || third * 3 != 1.0f;
}
