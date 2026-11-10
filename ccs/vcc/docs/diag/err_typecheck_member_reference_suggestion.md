# `.` used on a pointer, or `->` on a structure

`a.b` names a member of the structure `a`; `p->b` names a member of the structure that `p`
points to, and is `(*p).b`.  The operator must match the operand: a pointer needs `->`,
a structure (or union) value needs `.`.

The fix-it changes the operator; it is safe to apply (`--fix` does), since the operand's
type decides which one is meant.

```c wrong
struct point { int x, y; };
int sum(struct point *p) { return p.x + p.y; }
```

```c fixed
struct point { int x, y; };
int sum(struct point *p) { return p->x + p->y; }
```
