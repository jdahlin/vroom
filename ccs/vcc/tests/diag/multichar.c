/* -Wmultichar, too long, wide and Unicode with several characters; #if too; one snippet per location */
int a = 'ab';
int b = 'abcd';
int c = 'abcde';
int d = L'ab';
int e = '\n\t';
int g = u'ab';
int h = U'\x41b';
int i = '\377\377';
#if 'ab'
#endif
#if 'abcde'
#endif
