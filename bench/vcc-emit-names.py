#!/usr/bin/env python3
"""Check interned literal comparisons and x86 inline-asm register decoding."""
import argparse
import platform
from pathlib import Path
import subprocess
import tempfile

from hostenv import R, llvm

SOURCE = r'''
#include <assert.h>
#include <stdio.h>
#include "vcc/arch/x64/emit/asm.c"

vident *vintern_ents;
static vident names[2];
static void setname(const char *s)
{
    size_t n = strlen(s);
    names[1] = (vident){.s=s, .len=(uint32_t)n};
    memcpy(&names[1].w0, s, n < 8 ? n : 8);
    if (n > 8) memcpy(&names[1].w1, s + 8, n - 8);
}
int main(void)
{
    static const struct { const char *name; int reg; } legacy[] = {
        {"rax",RAX},{"eax",RAX},{"ax",RAX},{"al",RAX},{"ah",RAX},
        {"rcx",RCX},{"ecx",RCX},{"cx",RCX},{"cl",RCX},{"ch",RCX},
        {"rdx",RDX},{"edx",RDX},{"dx",RDX},{"dl",RDX},{"dh",RDX},
        {"rbx",RBX},{"ebx",RBX},{"bx",RBX},{"bl",RBX},{"bh",RBX},
        {"rsp",RSP},{"esp",RSP},{"sp",RSP},{"spl",RSP},
        {"rbp",RBP},{"ebp",RBP},{"bp",RBP},{"bpl",RBP},
        {"rsi",RSI},{"esi",RSI},{"si",RSI},{"sil",RSI},
        {"rdi",RDI},{"edi",RDI},{"di",RDI},{"dil",RDI},
    };
    char spelling[32];
    for (unsigned i = 0; i < sizeof legacy / sizeof *legacy; i++) {
        assert(x64_rname(legacy[i].name) == legacy[i].reg);
        snprintf(spelling, sizeof spelling, "%%%s", legacy[i].name);
        assert(x64_rname(spelling) == legacy[i].reg);
    }
    for (int i = 8; i <= 15; i++) {
        const char *suffixes[] = {"", "d", "w", "b"};
        for (unsigned j = 0; j < 4; j++) {
            snprintf(spelling, sizeof spelling, "r%d%s", i, suffixes[j]);
            assert(x64_rname(spelling) == i);
        }
    }
    for (int i = 0; i <= 15; i++) {
        snprintf(spelling, sizeof spelling, "%%xmm%d", i);
        assert(x64_rname(spelling) == XMM0 + i);
    }
    const char *invalid[] = {"", "a", "r", "e", "x", "rip", "r7", "r16", "xmm16", "raxz", "dp", "bil"};
    for (unsigned i = 0; i < sizeof invalid / sizeof *invalid; i++)
        assert(x64_rname(invalid[i]) == -1);
    /* Preserve existing atoi-based acceptance of suffixes on numbered names. */
    assert(x64_rname("r8junk") == R8 && x64_rname("xmm") == XMM0);

    vintern_ents = names;
    assert(!EM_IDENT_IS(0, "main"));
    setname("main"); assert(EM_IDENT_IS(1, "main") && !EM_IDENT_IS(1, "mains"));
    setname("setjmp"); assert(EM_IDENT_IS(1, "setjmp") && !EM_IDENT_IS(1, "longjmp"));
    setname("12345678"); assert(EM_IDENT_IS(1, "12345678") && !EM_IDENT_IS(1, "12345679"));
    setname("123456789"); assert(EM_IDENT_IS(1, "123456789") && !EM_IDENT_IS(1, "123456780"));
    setname("123456789abcdefg"); assert(EM_IDENT_IS(1, "123456789abcdefg") && !EM_IDENT_IS(1, "123456789abcdefh"));
    assert(!EM_IDENT_IS(1, "123456789abcdefgh"));
    puts("emitter identifier and x86 register-name contracts pass");
    return 0;
}
'''

BUILTINS = r'''
#include <assert.h>
#include "vcc/cc/builtin.c"
int (*pp_builtin_known)(const char *);
bool lib_builtin_known(const char *name) { (void)name; return false; }
int main(void) {
 unsigned count = 0;
 for (int i = 0; cc_builtin_name(i); i++) {
  const char *s = cc_builtin_name(i);
  int want = !strcmp(s,"__builtin_memcpy") ? CC_LIB_MEMCPY :
             !strcmp(s,"__builtin_memmove") ? CC_LIB_MEMMOVE :
             !strcmp(s,"__builtin_memset") ? CC_LIB_MEMSET :
             !strcmp(s,"__builtin_strcmp") ? CC_LIB_STRCMP :
             !strcmp(s,"__builtin_fabsl") ? CC_LIB_FABSL : CC_LIB_OTHER;
  assert(cc_builtin_libkind(i) == want);
  if (want) { count++; assert((builtins[i].flags & 1) == (want == CC_LIB_FABSL)); }
 }
 assert(count == 5);
 return 0;
}
'''

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--sanitize', action='store_true')
args = parser.parse_args()
with tempfile.TemporaryDirectory(prefix='vcc-emit-names-') as tmp:
    source = Path(tmp) / 'test.c'
    binary = Path(tmp) / 'test'
    flags = ['-fsanitize=address,undefined', '-fno-omit-frame-pointer'] if args.sanitize else []
    for program in (SOURCE, BUILTINS):
        source.write_text(program)
        command = [llvm('clang'), '-std=c11', '-D_GNU_SOURCE', '-O1', '-ffunction-sections',
                   '-fdata-sections', '-I' + str(Path(R) / 'ccs'), *flags, str(source),
                   ('-Wl,-dead_strip' if platform.system() == 'Darwin' else '-Wl,--gc-sections'),
                   '-o', str(binary)]
        subprocess.run(command, check=True)
        subprocess.run([binary], check=True)
    print('builtin dispatch metadata contracts pass')
