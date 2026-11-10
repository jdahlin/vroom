/* D6: -fdiagnostics-parseable-fixits: clang's fix-it:"file":{l:c-l:c}:"text" lines (tools
   parse them); escapes in the text, removals, none for a fix inside a macro's body */
/* diag-flags: -fdiagnostics-parseable-fixits -pedantic */
#define MEMBER p.a
struct S { int a; };;
int printf(const char *, ...);
int g(struct S *p) { return MEMBER; }
int h(struct S *p, long n, int x) {
	printf("a\"\\b\t%#d\n", n);
	if (x = 1) return p.a;
	x = 2);
	return 0;
}
