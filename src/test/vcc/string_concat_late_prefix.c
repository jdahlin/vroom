/* A late encoding prefix applies to all preceding tokens and escapes. */
#define TEXT_IMPL(x) #x
#define TEXT(x) TEXT_IMPL(x)
static const unsigned short u16[] = "\xfff" "a" "b" "c" u"\U0001f642";
static const unsigned int u32[] = "\x12345678" "a" "b" "c" U"\U0001f642";
static const __WCHAR_TYPE__ wide[] = "\xfff" "a" "b" "c" L"d";
static const unsigned short utf[] = "Привет" " " "wō" "rld" u"!";
/* More than the stack token buffer, including preprocessor-made spellings. */
static const unsigned short many[] = TEXT(a) TEXT(b) TEXT(c) TEXT(d) TEXT(e)
    TEXT(f) TEXT(g) TEXT(h) TEXT(i) TEXT(j) TEXT(k) u"l";
static const char bytes[] = "a" "b" "c" "d" u8"é";
int main(void)
{
    if (sizeof u16 != 14 || u16[0] != 0xfff || u16[3] != 'c' ||
        u16[4] != 0xd83d || u16[5] != 0xde42 || u16[6]) return 1;
    if (sizeof u32 != 24 || u32[0] != 0x12345678 || u32[4] != 0x1f642 || u32[5]) return 2;
    if (wide[0] != 0xfff || wide[4] != 'd' || wide[5]) return 3;
    if (utf[0] != 0x41f || utf[5] != 0x442 || utf[8] != 0x14d || utf[12] != '!') return 4;
    for (unsigned i = 0; i < 12; i++) if (many[i] != 'a' + i) return 5;
    if (many[12] || sizeof many != 26) return 6;
    if ((unsigned char)bytes[4] != 0xc3 || (unsigned char)bytes[5] != 0xa9 || bytes[6]) return 7;
    return 0;
}
