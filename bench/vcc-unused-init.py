#!/usr/bin/env python3
"""Host contracts for bounded unused integer initializer recipes."""
import argparse
from pathlib import Path
import subprocess
import tempfile

from hostenv import R, llvm

SOURCE = r'''
#include <assert.h>
#include "codegen/opt/unusedinit.c"
static ctype types[TY_NKINDS+2];
static sym symbols[5];
static attrs attributes[5];
static node nodes[100];
static uint8_t flags[5];
static cc_opts options;
static cc context;
static emitter emit;
static ecx state;
static unsigned count;
bool is_local_var(cc *c, uint32_t s)
{
    const sym *y=&c->syms[s];
    return y->kind==S_VAR && y->depth && (y->sc==SC_AUTO || y->sc==SC_REGISTER ||
        (y->flags&SF_PARAM) || (y->sc==SC_NONE && !(y->flags&SF_GLOBAL)));
}
const attrs *sym_attrs(cc *c, uint32_t s) { return &c->attr[c->syms[s].attrs]; }
static void reset(void)
{
    memset(types,0,sizeof types); memset(symbols,0,sizeof symbols);
    memset(attributes,0,sizeof attributes); memset(nodes,0,sizeof nodes);
    memset(flags,0,sizeof flags); count=0;
    for(unsigned k=0;k<TY_NKINDS;k++) types[k]=(ctype){.kind=k,.size=4,.canon=k};
    types[TY_LLONG].size=8; types[TY_INT128].size=16;
    types[TY_NKINDS]=(ctype){.kind=TY_INT,.size=4,.flags=TF_ATOMIC,.canon=TY_NKINDS};
    types[TY_NKINDS+1]=(ctype){.kind=TY_INT,.size=4,.n=24,.canon=TY_NKINDS+1};
    for(unsigned s=1;s<5;s++) symbols[s]=(sym){.kind=S_VAR,.sc=SC_AUTO,.depth=1,.ty=QT(TY_INT,0),.attrs=s};
    options=(cc_opts){.opt=1};
    context=(cc){.ty=types,.syms=symbols,.nodes=nodes,.attr=attributes,.o=&options};
    emit=(emitter){.o=&options,.sflags=flags};
    state=(ecx){.c=&context,.E=&emit};
}
static unsigned make(unsigned op, unsigned aux, unsigned a, unsigned b, unsigned c, qty type)
{
    assert(++count<100);
    nodes[count]=(node){.op=op,.aux=aux,.a=a,.b=b,.c=c,.ty=type};
    return count;
}
static unsigned recipe(unsigned initializer, unsigned budget)
{
    node decl={.op=N_DECL,.a=1,.b=initializer};
    return em_unused_init(&state,&decl,3,budget);
}
#define INT QT(TY_INT,0)
int main(void)
{
    reset();
    unsigned literal=make(N_NUM,0,0,0,0,INT);
    assert(recipe(literal,0)==EM_UNUSED_EVAL);
    assert(recipe(literal,1)==EM_UNUSED_OMIT);
    unsigned source=make(N_VAR,0,2,0,0,INT);
    unsigned add=make(N_BINARY,LEX_TOKEN_PLUS,source,literal,0,INT);
    assert(recipe(add,2)==EM_UNUSED_EVAL);
    assert(recipe(add,3)==EM_UNUSED_OMIT);
    assert(recipe(0,0)==EM_UNUSED_OMIT);
    unsigned boolean=make(N_CAST,CK_BOOL,source,0,0,INT);
    assert(recipe(boolean,2)==EM_UNUSED_OMIT);
    unsigned division=make(N_BINARY,LEX_TOKEN_SLASH,source,literal,0,INT);
    assert(recipe(division,64)==EM_UNUSED_EVAL);
    unsigned shift=make(N_BINARY,LEX_TOKEN_LESS_LESS,source,literal,0,INT);
    assert(recipe(shift,64)==EM_UNUSED_EVAL);
    unsigned load=make(N_UNARY,LEX_TOKEN_STAR,source,0,0,INT);
    assert(recipe(load,64)==EM_UNUSED_EVAL);
    unsigned call=make(N_CALL,0,0,0,0,INT);
    unsigned unknown=make(N_NKINDS,0,0,0,0,INT);
    assert(recipe(call,64)==EM_UNUSED_EVAL && recipe(unknown,64)==EM_UNUSED_EVAL);
    unsigned fp=make(N_FNUM,0,0,0,0,QT(TY_DOUBLE,0));
    unsigned conversion=make(N_CAST,CK_INT,fp,0,0,INT);
    assert(recipe(fp,64)==EM_UNUSED_KEEP && recipe(conversion,64)==EM_UNUSED_EVAL);
    const unsigned initforms[]={N_INIT,N_ZERO,N_STR};
    for(unsigned k=0;k<3;k++) assert(recipe(make(initforms[k],0,0,0,0,INT),64)==EM_UNUSED_KEEP);
    symbols[2].ty=QT(TY_INT,Q_VOLATILE); assert(recipe(source,64)==EM_UNUSED_EVAL);
    symbols[2].ty=QT(TY_NKINDS,0); assert(recipe(source,64)==EM_UNUSED_EVAL);
    symbols[2].ty=INT; attributes[2].asmname=1; assert(recipe(source,64)==EM_UNUSED_EVAL);
    attributes[2].asmname=0; symbols[2].sc=SC_STATIC; assert(recipe(source,64)==EM_UNUSED_EVAL);
    symbols[2].sc=SC_AUTO;
    flags[1]=SA_USED; assert(recipe(literal,64)==EM_UNUSED_KEEP); flags[1]=0;
    options.opt=0; assert(recipe(literal,64)==EM_UNUSED_KEEP); options.opt=1;
    emit.noopt=true; assert(recipe(literal,64)==EM_UNUSED_KEEP); emit.noopt=false;
    attributes[3].flags=AF_OPTNONE; assert(recipe(literal,64)==EM_UNUSED_KEEP); attributes[3].flags=0;
    state.top=4; attributes[4].flags=AF_OPTNONE; assert(recipe(literal,64)==EM_UNUSED_KEEP);
    state.top=0; attributes[4].flags=0;
    attributes[1].cleanup=2; assert(recipe(literal,64)==EM_UNUSED_KEEP); attributes[1].cleanup=0;
    attributes[1].used=1; assert(recipe(literal,64)==EM_UNUSED_KEEP); attributes[1].used=0;
    attributes[1].asmname=1; assert(recipe(literal,64)==EM_UNUSED_KEEP); attributes[1].asmname=0;
    symbols[1].flags=SF_ADDR_TAKEN; assert(recipe(literal,64)==EM_UNUSED_KEEP); symbols[1].flags=0;
    const unsigned storage[]={SC_STATIC,SC_EXTERN,SC_TYPEDEF};
    for(unsigned k=0;k<3;k++){symbols[1].sc=storage[k];assert(recipe(literal,64)==EM_UNUSED_KEEP);}
    symbols[1].sc=SC_AUTO;
    const qty excluded[]={QT(TY_INT,Q_VOLATILE),QT(TY_NKINDS,0),QT(TY_NKINDS+1,0),
        QT(TY_INT128,0),QT(TY_DOUBLE,0),QT(TY_PTR,0)};
    for(unsigned k=0;k<sizeof(excluded)/sizeof(excluded[0]);k++) {
        symbols[1].ty=excluded[k]; assert(recipe(literal,64)==EM_UNUSED_KEEP);
    }
    reset();
    unsigned chain=make(N_NUM,0,0,0,0,INT);
    for(unsigned k=0;k<63;k++) chain=make(N_CAST,CK_INT,chain,0,0,INT);
    assert(recipe(chain,63)==EM_UNUSED_EVAL && recipe(chain,64)==EM_UNUSED_OMIT);
    chain=make(N_CAST,CK_INT,chain,0,0,INT);
    assert(recipe(chain,64)==EM_UNUSED_EVAL && recipe(chain,65)==EM_UNUSED_OMIT);
    return 0;
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', default=llvm('clang'))
    parser.add_argument('--sanitize', action='store_true')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='vcc-unused-init-') as tmp:
        source = Path(tmp) / 'unused.c'
        binary = Path(tmp) / 'unused'
        source.write_text(SOURCE)
        flags = ['-fsanitize=address,undefined', '-fno-omit-frame-pointer'] if args.sanitize else []
        subprocess.run([args.cc, '-std=gnu11', '-O1', '-g', '-I', str(Path(R) / 'src'), '-I', str(Path(R) / 'src' / 'include'),
                        *flags, str(source), '-o', str(binary)], check=True, timeout=30)
        subprocess.run([str(binary)], check=True, timeout=10)
    print('unused integer initializer contracts: PASS')


if __name__ == '__main__':
    main()
