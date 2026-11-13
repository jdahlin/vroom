/* vcc-flags: -std=gnu99 */
/* GNU ranges share their initial values, not subsequent writes or the
   positional continuation after the last designated subobject. */
#define ROWS { [0 ... 1][1 ... 2] = 7, [0] = 2, [0][1] = 3 }
#define CUBE { [0 ... 1][0 ... 1][0 ... 1] = 4, 5 }
#define SPLIT { [0 ... 2] = { [0 ... 2] = 1 }, [1][1] = 2 }
#define RECORDS { [0 ... 1].v = { [0 ... 1] = 4 }, [1].v[1] = 5, 6 }
#define RESET { [0 ... 1][1 ... 2] = 7, [0] = { 2 } }
#define ANONYMOUS { [0 ... 1].v[0 ... 1] = 3, 4, 5 }
struct Record { int v[2], tail; };
struct Anon { union { struct { int v[2], tail; }; }; int after; };
static int rows[2][3] = ROWS, cube[2][2][3] = CUBE;
static int split[3][3] = SPLIT, reset[2][3] = RESET;
static struct Record records[2] = RECORDS;
static struct Anon anonymous[2] = ANONYMOUS;
static int check(int r[2][3], int c[2][2][3], int s[3][3], int z[2][3], struct Record *p, struct Anon *a)
{
    for (int i=0;i<2;i++) for (int j=0;j<3;j++) {
        int want=i ? (j ? 7:0) : (j==0 ? 2:j==1 ? 3:7);
        if(r[i][j]!=want) return 1;
        if(z[i][j]!=(i ? (j ? 7:0):(j==0 ? 2:0))) return 2;
    }
    for(int i=0;i<2;i++) for(int j=0;j<2;j++) for(int k=0;k<3;k++)
        if(c[i][j][k]!=(k<2 ? 4:(i==1 && j==1 ? 5:0))) return 3;
    for(int i=0;i<3;i++) for(int j=0;j<3;j++)
        if(s[i][j]!=(i==1 && j==1 ? 2:1)) return 4;
    for(int i=0;i<2;i++) {
        if(p[i].v[0]!=4 || p[i].v[1]!=(i ? 5:4) || p[i].tail!=(i ? 6:0)) return 5;
        if(a[i].v[0]!=3 || a[i].v[1]!=3 || a[i].tail!=(i ? 4:0) || a[i].after!=(i ? 5:0)) return 6;
    }
    return 0;
}
int main(void)
{
    int r[2][3]=ROWS,c[2][2][3]=CUBE,s[3][3]=SPLIT,z[2][3]=RESET;
    struct Record p[2]=RECORDS;
    struct Anon a[2]=ANONYMOUS;
    int result=check(rows,cube,split,reset,records,anonymous);
    return result ? result : check(r,c,s,z,p,a);
}
