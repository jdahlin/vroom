/* Unknown escapes warn but retain the escaped character, as clang does. */
int main(void)
{
    const char *s = "\j\q\z";
    const __WCHAR_TYPE__ *w = L"\j\q";
    return s[0] != 'j' || s[1] != 'q' || s[2] != 'z' || s[3] ||
           '\j' != 'j' || L'\q' != L'q' || w[0] != L'j' || w[1] != L'q' || w[2];
}
