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
static ctype types[AST_TYPE_KIND_COUNT+2];
static sym symbols[5];
static attrs attributes[5];
static node nodes[100];
static uint8_t flags[5];
static cc_options options;
static cc context;
static codegen_unit emit;
static codegen_context state;
static unsigned count;
bool codegen_symbol_is_local_variable(cc *c, uint32_t s)
{
    const sym *y=&c->syms[s];
    return y->kind==AST_SYMBOL_VARIABLE && y->depth && (y->sc==AST_STORAGE_AUTO || y->sc==AST_STORAGE_REGISTER ||
        (y->flags&AST_SYMBOL_FLAG_PARAMETER) || (y->sc==AST_STORAGE_NONE && !(y->flags&AST_SYMBOL_FLAG_GLOBAL)));
}
const attrs *codegen_symbol_attributes(cc *c, uint32_t s) { return &c->attr[c->syms[s].attrs]; }
static void reset(void)
{
    memset(types,0,sizeof types); memset(symbols,0,sizeof symbols);
    memset(attributes,0,sizeof attributes); memset(nodes,0,sizeof nodes);
    memset(flags,0,sizeof flags); count=0;
    for(unsigned k=0;k<AST_TYPE_KIND_COUNT;k++) types[k]=(ctype){.kind=k,.size=4,.canon=k};
    types[AST_TYPE_LONG_LONG].size=8; types[AST_TYPE_INT128].size=16;
    types[AST_TYPE_KIND_COUNT]=(ctype){.kind=AST_TYPE_INT,.size=4,.flags=AST_TYPE_FLAG_ATOMIC,.canon=AST_TYPE_KIND_COUNT};
    types[AST_TYPE_KIND_COUNT+1]=(ctype){.kind=AST_TYPE_INT,.size=4,.n=24,.canon=AST_TYPE_KIND_COUNT+1};
    for(unsigned s=1;s<5;s++) symbols[s]=(sym){.kind=AST_SYMBOL_VARIABLE,.sc=AST_STORAGE_AUTO,.depth=1,.ty=AST_QUALIFIED_TYPE(AST_TYPE_INT,0),.attrs=s};
    options=(cc_options){.opt=1};
    context=(cc){.ty=types,.syms=symbols,.nodes=nodes,.attr=attributes,.o=&options};
    emit=(codegen_unit){.options=&options,.symbol_flags=flags};
    state=(codegen_context){.compile_context=&context,.unit=&emit};
}
static unsigned make(unsigned op, unsigned aux, unsigned a, unsigned b, unsigned c, ast_qualified_type type)
{
    assert(++count<100);
    nodes[count]=(node){.op=op,.aux=aux,.a=a,.b=b,.c=c,.ty=type};
    return count;
}
static unsigned recipe(unsigned initializer, unsigned budget)
{
    node decl={.op=AST_NODE_DECLARATION,.a=1,.b=initializer};
    return em_unused_init(&state,&decl,3,budget);
}
#define INT AST_QUALIFIED_TYPE(AST_TYPE_INT,0)
int main(void)
{
    reset();
    unsigned literal=make(AST_NODE_NUMBER,0,0,0,0,INT);
    assert(recipe(literal,0)==EM_UNUSED_EVAL);
    assert(recipe(literal,1)==EM_UNUSED_OMIT);
    unsigned source=make(AST_NODE_VARIABLE,0,2,0,0,INT);
    unsigned add=make(AST_NODE_BINARY,LEX_TOKEN_PLUS,source,literal,0,INT);
    assert(recipe(add,2)==EM_UNUSED_EVAL);
    assert(recipe(add,3)==EM_UNUSED_OMIT);
    assert(recipe(0,0)==EM_UNUSED_OMIT);
    unsigned boolean=make(AST_NODE_CAST,AST_CAST_BOOL,source,0,0,INT);
    assert(recipe(boolean,2)==EM_UNUSED_OMIT);
    unsigned division=make(AST_NODE_BINARY,LEX_TOKEN_SLASH,source,literal,0,INT);
    assert(recipe(division,64)==EM_UNUSED_EVAL);
    unsigned shift=make(AST_NODE_BINARY,LEX_TOKEN_LESS_LESS,source,literal,0,INT);
    assert(recipe(shift,64)==EM_UNUSED_EVAL);
    unsigned load=make(AST_NODE_UNARY,LEX_TOKEN_STAR,source,0,0,INT);
    assert(recipe(load,64)==EM_UNUSED_EVAL);
    unsigned call=make(AST_NODE_CALL,0,0,0,0,INT);
    unsigned unknown=make(AST_NODE_KIND_COUNT,0,0,0,0,INT);
    assert(recipe(call,64)==EM_UNUSED_EVAL && recipe(unknown,64)==EM_UNUSED_EVAL);
    unsigned fp=make(AST_NODE_FLOAT_NUMBER,0,0,0,0,AST_QUALIFIED_TYPE(AST_TYPE_DOUBLE,0));
    unsigned conversion=make(AST_NODE_CAST,AST_CAST_INTEGER,fp,0,0,INT);
    assert(recipe(fp,64)==EM_UNUSED_KEEP && recipe(conversion,64)==EM_UNUSED_EVAL);
    const unsigned initforms[]={AST_NODE_INITIALIZER,AST_NODE_ZERO,AST_NODE_STRING};
    for(unsigned k=0;k<3;k++) assert(recipe(make(initforms[k],0,0,0,0,INT),64)==EM_UNUSED_KEEP);
    symbols[2].ty=AST_QUALIFIED_TYPE(AST_TYPE_INT,AST_QUALIFIER_VOLATILE); assert(recipe(source,64)==EM_UNUSED_EVAL);
    symbols[2].ty=AST_QUALIFIED_TYPE(AST_TYPE_KIND_COUNT,0); assert(recipe(source,64)==EM_UNUSED_EVAL);
    symbols[2].ty=INT; attributes[2].asmname=1; assert(recipe(source,64)==EM_UNUSED_EVAL);
    attributes[2].asmname=0; symbols[2].sc=AST_STORAGE_STATIC; assert(recipe(source,64)==EM_UNUSED_EVAL);
    symbols[2].sc=AST_STORAGE_AUTO;
    flags[1]=CODEGEN_SYMBOL_USED; assert(recipe(literal,64)==EM_UNUSED_KEEP); flags[1]=0;
    options.opt=0; assert(recipe(literal,64)==EM_UNUSED_KEEP); options.opt=1;
    emit.cleanups_disabled=true; assert(recipe(literal,64)==EM_UNUSED_KEEP); emit.cleanups_disabled=false;
    attributes[3].flags=CC_ATTRIBUTE_FLAG_OPTNONE; assert(recipe(literal,64)==EM_UNUSED_KEEP); attributes[3].flags=0;
    state.top_symbol=4; attributes[4].flags=CC_ATTRIBUTE_FLAG_OPTNONE; assert(recipe(literal,64)==EM_UNUSED_KEEP);
    state.top_symbol=0; attributes[4].flags=0;
    attributes[1].cleanup=2; assert(recipe(literal,64)==EM_UNUSED_KEEP); attributes[1].cleanup=0;
    attributes[1].used=1; assert(recipe(literal,64)==EM_UNUSED_KEEP); attributes[1].used=0;
    attributes[1].asmname=1; assert(recipe(literal,64)==EM_UNUSED_KEEP); attributes[1].asmname=0;
    symbols[1].flags=AST_SYMBOL_FLAG_ADDRESS_TAKEN; assert(recipe(literal,64)==EM_UNUSED_KEEP); symbols[1].flags=0;
    const unsigned storage[]={AST_STORAGE_STATIC,AST_STORAGE_EXTERN,AST_STORAGE_TYPEDEF};
    for(unsigned k=0;k<3;k++){symbols[1].sc=storage[k];assert(recipe(literal,64)==EM_UNUSED_KEEP);}
    symbols[1].sc=AST_STORAGE_AUTO;
    const ast_qualified_type excluded[]={AST_QUALIFIED_TYPE(AST_TYPE_INT,AST_QUALIFIER_VOLATILE),AST_QUALIFIED_TYPE(AST_TYPE_KIND_COUNT,0),AST_QUALIFIED_TYPE(AST_TYPE_KIND_COUNT+1,0),
        AST_QUALIFIED_TYPE(AST_TYPE_INT128,0),AST_QUALIFIED_TYPE(AST_TYPE_DOUBLE,0),AST_QUALIFIED_TYPE(AST_TYPE_POINTER,0)};
    for(unsigned k=0;k<sizeof(excluded)/sizeof(excluded[0]);k++) {
        symbols[1].ty=excluded[k]; assert(recipe(literal,64)==EM_UNUSED_KEEP);
    }
    reset();
    unsigned chain=make(AST_NODE_NUMBER,0,0,0,0,INT);
    for(unsigned k=0;k<63;k++) chain=make(AST_NODE_CAST,AST_CAST_INTEGER,chain,0,0,INT);
    assert(recipe(chain,63)==EM_UNUSED_EVAL && recipe(chain,64)==EM_UNUSED_OMIT);
    chain=make(AST_NODE_CAST,AST_CAST_INTEGER,chain,0,0,INT);
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
