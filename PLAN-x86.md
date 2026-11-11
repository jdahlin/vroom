# x86-64 Linux som mål: ny arch i vcc

**Mål:** `vcc --target=x86_64-linux-gnu -c x.c` ger ett ELF-objekt som länkas mot
glibc och körs med samma resultat som clangs. `-S` ger AT&T-syntax som clangs.
Slutmålet är en x86-64-kärna (defconfig + vroom-tillägg) byggd av vcc som bootar
Debian i `qemu-system-x86_64 -enable-kvm`, och samma kalla byggtid som på arm64.

**Varför:** Maskinen är nu en x86-64 Linux-värd. Med x86-64 som mål kör testerna
nativt (ingen qemu-user), och vcc får ett andra registermaskin-mål. Det avgör vad
i `arch/a64/emit/` som egentligen är generiskt och vad i Linux-koden som är per OS
och vad som är per CPU (TODO.md: "Target-OS code out of the generic files").

**Regler:** samma som fas 2 och PLAN-wasm.md. Clang är referensen
(`--target=x86_64-linux-gnu`), inget från clang/LLVM vid körning; tabeller tas ur
clang en gång (predefines, `__has_*`, kodartabeller). arm64- och wasm-utdata får
inte ändras av något steg: varje commit verifieras med `bench/vcc-same.py` mot en
bas byggd före steget, och `bench/vbench.py gate`.
x86_64-apple-darwin ingår inte.

## Utgångsläge (2026-11-10)

- Värden: x86_64, Linux 7.0, gcc och GNU binutils 2.46. **clang, lld och llvm-mc
  saknas**, liksom qemu. De behövs som referens (se Risker: installation).
- **vcc bygger inte på x86-64**: lexern är NEON (`lex/lex.c:4`, med en skalär väg
  bakom `VLEX_SCALAR`), interneringen använder ACLE-CRC32C (`lib/intern.c:1`,
  `lib/intern.h:86`), `lib/ldhash.c:184` har aarch64-grenar och Makefile väljer
  `-mcpu=…` (TODO.md: "x86-64 host build"). `ccs/wasm/compat/` visar redan hur
  ACLE-CRC och NEON mappas när vcc byggs för ett annat mål.
- Målvalet är `po.target` (`pp/pp.h:82`: `PP_LINUX_AARCH64, PP_DARWIN, PP_WASM64,
  PP_WASM32`), med makron `DARWIN(c)`/`WASM(c)` i `emit/emitimpl.h` och
  `CC_ILP32(c)` i cc. Emittern väljer generator per funktion i `emit/tu.c:178`
  (`wasm_emit_function` eller `emit_function`), data-direktiv i `emit/data.c`.
- arm64-generatorn (`arch/a64/emit/`, ~10 300 rader) bygger instruktionsposter
  (`ins.h`) direkt från AST:t, med temporärer ur en pool (`treg`/`tfree`),
  lokala variabler i register (`B_LREG`) eller stackplatser (`B_SLOT`), en
  postoptimerare (`opt.c`), en skrivare (`print.c`) och en kodare direkt till
  objektmodellen (`obj.c`). Vikning, nåbarhet, inlining och `__builtin_constant_p`
  ligger kvar där (W1 i PLAN-wasm.md lyfte inte ut dem).
- Objektlagret är arm64-bundet: `obj/obj.h:54` inkluderar `arch/a64/reloc.h`, och
  ELF-skrivaren skriver `e_machine = 183` (`platform/linux/elf.c:279`) och har
  AArch64-relokeringar i sin GOT/TLS-logik (`:42`).
- Länkaren (`ld/ld.c`) är en TU med arm64-fragment (`arch/a64/ld/*.inc`).

## Datamodellen

| | aarch64-linux | x86_64-linux |
|---|---|---|
| `long`, pekare, `size_t` | 8 | 8 |
| `char` | unsigned | **signed** |
| `wchar_t` | `unsigned int` | **`int`** |
| `long double` | binary128, 16/16 | **x87 80 bitar**, storlek 16, align 16 |
| `__float128` | – (= `long double`) | **binary128, egen typ** |
| `va_list` | AAPCS64-struct | **`struct __va_list_tag[1]`** (`gp_offset`, `fp_offset`, `overflow_arg_area`, `reg_save_area`; 24 byte, arraytyp) |
| `_Float16`, `__bf16` | ja | ja (SSE2) |
| största alignment | 16 | 16 |

LP64 som arm64, så typtabellen kan stå kvar. Det nya är x87-`long double`:
konstantvikning, literaler och utdata (`.quad`+`.short`, 6 byte utfyllnad) behöver
en `lib/f80.h` bredvid `lib/f128.h`, och `__float128` blir en egen typ.

## Arkitektur

| Del | Fil(er) | Innehåll |
|---|---|---|
| Värdbygge | `lex/lex.c`, `lib/intern.*`, `lib/ldhash.c`, `ccs/Makefile` | SSE2/AVX2-lexer (skalär först), CRC32C via SSE4.2 (`_mm_crc32_u64`: samma polynom, samma hashar), `pause` i stället för `yield`, `-march=native` |
| Målval | `pp/pp.h`, `driver/ppdriver.c`, `vccd/caps.c` | `PP_LINUX_X86_64`; `--target=x86_64-*-linux-gnu`, `-m64`; utan `--target` på en x86-64-värd: x86-64 (som arm64-värden ger aarch64) |
| Predefines, `__has_*` | `pp/ppdata-predef.inc`, `gen-ppdata.sh` | En kolumn till ur clang; `-march=`/`-msse4.2`/`-mavx2`/`-mno-sse`/`-mgeneral-regs-only`-deltan som tabell |
| Layout | `cc/type.c`, `cc/tu.c`, `cc/layout.c` | `char` signed, `wchar_t` int, x87 `long double`, `__float128`, `va_list` |
| AST-vandrare | `emit/walk/` | Lager 1 nedan: vikning, nåbarhet, inlining, `__builtin_constant_p`, switch-strategi, stackplatser, `val`/`lval`, anropssekvensen; sänkningsgränssnittet `walk/lower.h` |
| Poster och pass | `emit/rec/` | Lager 3: gemensamt posthuvud, op-tabell per CPU, liveness, kopiepropagering, döda resultat, stackplatser, hopp |
| Måltabell | `emit/tu.c`, `emit/data.c` | Funktion, huvud, svans och data-direktiv per mål; x86-64 läggs till bredvid `WASM(c)`-grenarna, som städas när arm64 och wasm flyttas |
| Sänkning | `arch/x64/emit/` | Lager 2: `lower.c` (heltal, minne), `fp.c` (SSE), `x87.c`, `abi.c` (SysV), `builtin.c`, `atomic.c`, `asm.c`; op-tabellen i `x64impl.h`; `peep.c`, `twoaddr.c`, `func.c` (ram, prolog) |
| Textutdata | `arch/x64/emit/print.c` | `-S` i clangs AT&T-syntax (`.p2align 4, 0x90`, `.type`, `.size`, `#APP`) |
| Kodare | `arch/x64/as/enc.c`, `x64.h` | REX/ModRM/SIB/disp/imm, VEX/EVEX senare; tabelldriven, tabellen genererad en gång ur LLVM:s X86-tabeller |
| Assembler | `arch/x64/as/parse.c` | AT&T-syntax för `vas`, inline asm och kärnans `.S` |
| Fixups | `arch/x64/reloc.h`, `arch/x64/obj/fixup.c` | `R_X86_64_PC32/PLT32/32/32S/64/PC64/GOTPCRELX/REX_GOTPCRELX/TPOFF32/GOTTPOFF/TLSGD/TLSLD/DTPOFF32`; hoppavslappning rel8↔rel32 som MC |
| Objektskrivare | `platform/linux/elf.c` | `e_machine` och relokeringsregler från arch i stället för hårdkodat |
| Länkare | `arch/x64/ld/*` | Relokeringar, GOT/PLT, TLS- och GOTPCRELX-avslappning (inga thunks) |

### Kodgeneratorns lager: AST-vandrare, sänkning per CPU, gemensamma pass

En IR kostar inte genom att finnas utan genom tre saker: en graf med pekare och
användarlistor (allokering, cachemissar), ett extra sänkningspass mellan IR och
maskin, och SSA-konstruktion. vcc undviker alla tre men skriver ändå varje
optimering en gång. Det sker genom tre lager:

```
cc/:s AST ──▶ 1. AST-vandrare (emit/walk/) ──LO()-anrop──▶ 2. sänkning (arch/x64/emit/)
                                                                │ maskinposter, 3-adress
                                                                ▼
                         3. gemensamma pass (emit/rec/) ◀── op-tabell per CPU
                                                                │
                                         tvåadress, titthål, skrivare/kodare (arch/x64/emit/)
```

**Avgränsning:** x86-64 är första och enda användaren. arm64 och wasm behåller sina
generatorer tills de flyttas, och det är en egen arbetspunkt (TODO.md: "arm64 and
wasm on the shared walker"). Gränssnitten ritas för det x86-64 behöver. Två billiga förberedelser
görs redan nu, eftersom flytten annars blir dyrare: anropen går genom `LO()`, och
posthuvudet har arm64:s layout. Inget annat byggs i förväg.

#### Lager 1: AST-vandraren, `emit/walk/`

Här fattas alla beslut på C-nivå, en gång för alla mål. Filerna följer
`arch/a64/emit/`:s ämnen, och reglerna kopieras därifrån (vikning,
`__builtin_constant_p`, inlining). Det ger dem en gemensam form som arm64 senare
kan gå över till:

| Fil | Innehåll |
|---|---|
| `walk.h` | Internt huvud: `val`, `lval`, `bind`, `fnctx`, vandrarens del av `ecx`; deklarationer av `LO()`-gränssnittet |
| `expr.c`, `cond.c` | Uttryck, vikning medan koden genereras (`val` är konstant, adress+offset eller register), villkor till hopp, `cond_fold`/intervall, pekararitmetik, `__int128` i registerpar |
| `lval.c`, `bind.c` | Lvärden (ram, symbol, register, bit-fält), bindning av locals till stackplatser eller register (`B_LREG` ur prepass), stackplatser och deras återanvändning |
| `stmt.c`, `label.c` | Satser, nåbarhetsflaggan, kända värden över kanter (`kv_*`), cleanups, VLA, switch-strategi (tabell, binärsökning, kedja: tröskeln är en målparameter) |
| `call.c`, `inline.c` | Anropssekvensen kring målets ABI-klassning, inlining av `__always_inline` med konstanta argument, `need_fold`, error()-anrop |
| `builtin.c`, `init.c` | Måloberoende inbyggda (`constant_p`, `expect`, `object_size`, `choose_expr`, overflow, bitoperationer via `LO`), initierare, aggregatkopior, mjuka flyttal via `FN_*`-anrop (vilka typer som är mjuka är en måluppgift: x86 `__float128`, arm64 `long double`) |

Värden är **fysiska register** som i dag: målet lämnar ut temporärer (`LO(treg)`)
och vandraren frigör dem. Det behövs ingen registerallokering. Spill sker som i
arm64: runt anrop (`spill_live`) och ett nytt försök för funktionen med
`e->spill` när temporärerna tar slut. Den logiken ligger i vandraren och gäller
alla mål. Laddningar sammanfogas inte med operationer i vandraren (inga
`V_MEM`-värden). Det gör titthålspasset efteråt, och då hålls vandraren enkel.

#### Lager 2: sänkningsgränssnittet, `emit/walk/lower.h`

Ett trettiotal operationer som `arch/x64/emit/` definierar. Det är instruktionsval
och ingenting annat:

| Grupp | Operationer |
|---|---|
| Register | `treg`, `treg_fixed(R)` (flyttar undan den som håller R: `div`, skift, `rep movs`, argument), `tfree`, `mov` |
| Värden | `iconst(rd, i128, bredd)`, `addr(rd, ref, off)` (rip-relativ, GOT, TLS-modell: målets sak), `ext(rd, rs, från, till, sgn)` |
| Minne | `load(rd, lval, n, ext)`, `store(rs, lval, n)`, `lea(rd, lval)`, `copy(dst, src, n)`, `zero(dst, n)` |
| Heltal | `bin(op, bredd, rd, val a, val b)` (b får vara konstant), `un(op, …)`, `bin2`/`cmp2` för registerpar, `cmp(op, sgn, bredd, a, b) → cc`, `setcc`, `select` |
| Flyttal | `fbin`, `fcmp → cc` (paritet för NaN är målets sak), `fconv` (heltal↔flyttal, mellan bredder), x87 för `long double` göms här |
| Kontroll | `label`, `jump`, `branch(cc, lbl)`, `branch_zero(val, nz, lbl)`, `jumptable(reg, lbls, n)` |
| Anrop och ram | `abi_classify(fntyp) → abiarg[]` (SysV-åttabyteklassning), `call(ref eller reg, abiarg[])`, `ret`, `va_start`, `va_arg`, `prolog`/`epilog` (läggs in när funktionen är klar och ramen känd) |
| Målspecifikt | `atomic(op, ordning, lval, …)`, `builtin(id, args) → bool` (resten: `__builtin_ia32_*`, `cpu_supports`, `pause`), `inline_asm(nod)` (begränsningar och modifierare är målets; vandraren räknar fram operanderna) |

Anropen går genom ett makro, `LO(name)` → `x64_name`. I unity-TU:n inlinas de, så
det blir inga indirekta anrop per AST-nod. *Förberedelse för flytten:* när arm64
blir en andra användare kompileras vandraren en gång per mål (`#include
"walk/*.c"` med ett annat prefix i `LO`, som en mall). Det kräver att anropen
redan går genom `LO()`, och det är allt som behövs nu.

#### Lager 3: maskinposter och gemensamma pass, `emit/rec/`

Posterna är en platt array per funktion, som i dag, med ett gemensamt huvud i
`emit/rec/rec.h`:

```c
typedef struct ins {
	uint8_t op, sz;		/* op: målets opkod; sz: bredd */
	uint8_t r[4];		/* registerfält på fasta platser (x86: rd, bas, index, rs) */
	uint8_t cond, sh;	/* villkor; skala/skift */
	union { int64_t imm; struct { int32_t off; uint32_t ref; }; uint32_t label; };
} ins;			/* 16 byte, som arm64:s */
```

Posten är arm64:s med fälten `rd, rn, rm, ra` som `r[0..3]`. *Förberedelse för
flytten:* arm64 byter namn på fälten och får samma huvud, och ingen layout ändras.

Varje mål ger en **op-tabell** (`opdesc[op]`): vilka `r[i]` som definieras och
läses, implicita register (x86: `div` → `rax rdx`, `rep movsb` → `rdi rsi rcx`,
anrop → de anroparsparade), hoppslag, `PURE`, läser/skriver minne, läser/skriver
`FLAGS`. Ops vars roller beror på operanderna får en krok (som `roles_sw` i dag).
`rmask` är 64 bitar: 16 GPR, 16 XMM, `FLAGS` och `FRAME` ryms.

Gemensamma pass, generiska över tabellen och flyttade i form från
`arch/a64/emit/opt.c`:

| Pass | Fil |
|---|---|
| Liveness över grundblock (delade vid etiketter och efter hopp) | `rec/live.c` |
| Kopiepropagering framåt och bakåt, döda `mov` | `rec/copy.c` |
| Döda resultat (rena poster vars utdata inte lever; `FLAGS` räknas) | `rec/dead.c` |
| Stackplatser: delning, laddning från register i stället för plats, döda spill (`SPILL_REC`) | `rec/slots.c` |
| Hopp till nästa post, hopp till hopp | `rec/branch.c` |

Posterna är **treadress** (`rd = a op b`) ända fram till ett sent x86-pass,
`arch/x64/emit/twoaddr.c`, som skriver om till tvåadress. Där sätts `mov` in bara
när `rd ≠ a` och `a` lever efteråt, och kommutativa operationer byts för att slippa
en `mov`. Kopiepropageringen kan därför vara densamma som arm64:s, utan regler för
bundna operander. Det är LLVM:s TwoAddressInstructionPass i ett linjärt pass.

Målspecifikt efter de gemensamma passen: `arch/x64/emit/peep.c` (laddning in i
operationen när temporären dör: `add 8(%rbp), %eax`; `lea` för add/skift;
`test r,r` mot 0; `xor r,r` för 0; kortaste `mov $imm`; `cmov`), sedan
`twoaddr.c`, ram och prolog (`func.c`), skrivare (`print.c`) och kodare (`obj.c`).
Kodningen av utvalda poster sker via objektlagrets frag- och fixup-replay
(`IT_CODE`), som är generisk redan i dag.

Utvidgningspasset (`exts` i opt.c) flyttas inte nu: x86:s 32-bitarsoperationer
nollställer också övre halvan, men det ger lite på `-O1`. Det tas när mätningar
visar att det behövs.

#### Vad som inte går utan SSA

Globala optimeringar som GVN, LICM och registerallokering med graffärgning. Om
`-O2` någon gång behöver dem är vägen ett valfritt SSA-pass ovanpå samma platta
poster (virtuella register i stället för fysiska), bara för `-O2` och bara för
funktioner med loopar. Det hör inte till den här planen. `-O0`/`-O1`, som
kärnbygget använder, behåller dagens kostnad.

#### Två kopior av reglerna under övergången

Tills arm64 är flyttad finns vikning, inlining och `constant_p`-reglerna både i
`arch/a64/emit/` och i `emit/walk/`. Att de inte glider isär kontrolleras av
`VCC_WALKLOG=1`. Den skriver per funktion vad generatorn beslutade: inlinade
anrop, vikta villkor, kvarvarande error()-anrop, värdet av `__builtin_constant_p`
per anrop. Ett nytt harness, `bench/vcc-walkcmp.py`, jämför x86-64 med arm64 på
C-sviterna (gcc-torture, tcc, vcc-tester). Skillnader godtas bara där
datamodellen förklarar dem (`char`, `wchar_t`, `long double`), och de listas i
harnessets undantagsfil.

### x86-64 i sänkningen

Det som skiljer x86-64 och som lager 2 och de x86-specifika passen tar hand om:

- **Register:** 16 GPR. Temporärer ur de anroparsparade (`rax rcx rdx rsi rdi
  r8–r11`), lokala variabler i registren `rbx r12–r15` (`B_LREG`). Operationer med
  fasta register — `div`/`idiv` (`rdx:rax`), skift (`cl`), breddad `mul`,
  `rep movsb`/`stosb` (`rdi rsi rcx`), argumentregistren — tar sitt register via en
  `treg_fixed(e, R)` som flyttar undan den som håller det. Det är den stora
  skillnaden mot arm64:s gott om register; spill till stackplatser när poolen tar slut.
- **Tvåadress och minnesoperander:** posterna är treadress tills `twoaddr.c`
  skriver `rd = a op b` som `mov a, rd; op b, rd`. `peep.c` slår ihop laddning+operation
  (`add 8(%rbp), %eax`), använder `lea` för add/skift-kombinationer, `test` för
  jämförelse med 0, `xor r,r` för 0 och `cmov`/`setcc` för villkor.
- **32-bitarsoperationer nollställer övre halvan**, som arm64:s w-register: samma
  utvidgningsregler kan återanvändas.
- **Flyttal:** `float`/`double` i SSE2 skalärt (`addsd`, `cvtsi2sd`, `ucomisd`).
  `==` och `!=` behöver paritetsflaggan (NaN). Osignerade 64-bitarskonverteringar
  med clangs sekvenser. `long double` i x87 via minnesplatser (`fldt`/`fstpt`), utan
  registerallokering för x87-stacken; `__float128` via compiler-rt (`__addtf3` …).
  `-mno-sse`/`-mgeneral-regs-only`: inga SSE-instruktioner (`bench/vcc-fpcheck.py`).
- **ABI (System V):** klassning per åttabyte (INTEGER/SSE/SSEUP/X87/X87UP/MEMORY),
  aggregat ≤ 16 byte i upp till två register (även blandat), större via minne;
  sret i `rdi`, återlämnad i `rax`; `long double` i minne som argument och i
  `st(0)` som returvärde; `__int128` i registerpar. Varargs: `al` = antal
  vektorregister, `va_start` fyller register-sparområdet, `va_arg` som clangs.
  Stacken 16-justerad vid anrop. Röd zon (128 byte) för lövfunktioner om inte
  `-mno-red-zone`.
- **Adressering:** `x(%rip)` som standard (small code model); `-fPIC`/`-fPIE`:
  `x@GOTPCREL(%rip)` och `call x@PLT` som clang. TLS: LE `%fs:x@tpoff`, IE
  `x@gottpoff(%rip)`, GD/LD med clangs exakta utfyllnad (`data16 leaq …;
  data16 data16 rex64 call __tls_get_addr@PLT`) så att länkaren kan slappa av.
- **Hopptabeller:** `switch` med täta fall som clangs (`.rodata`, 32-bitars
  relativa poster i PIC, absoluta 64-bitars annars).
- **Atomics:** TSO gör laddningar och lagringar vanliga `mov`; seq_cst-lagring som
  `xchg`; RMW med `lock`-prefix eller `cmpxchg`-slinga; 16 byte via `cmpxchg16b`
  under `-mcx16`, annars `__atomic_*`-anrop som clang.
- **Inbyggda:** `clz`/`ctz` (`bsr`/`bsf`, eller `lzcnt`/`tzcnt` med `-mlzcnt`/`-mbmi`),
  `popcount` (`popcnt` under `-mpopcnt`, annars clangs bitsekvens), `bswap`,
  overflow-aritmetik via `seto`/`setc`, `__builtin_cpu_supports`/`__builtin_cpu_init`
  (`__cpu_model` i compiler-rt), `__builtin_ia32_pause`/`rdtsc`/… som kärnan använder.
- **Inline asm:** AT&T-text med `%k0 %w0 %b0 %h0 %q0 %P0 %c0 %a0`, begränsningar
  `r q Q a b c d S D A m i n e Z x t u`, flaggutdata `=@ccz` (kärnan använder dem),
  `asm goto` med utdata.

## Steg

| # | Leverans | Kriterium |
|---|----------|-----------|
| X0 | **vcc bygger och kör på x86-64 Linux** (fortfarande aarch64 som mål). Skalär lexer via `VLEX_SCALAR`, CRC32C med SSE4.2, värd-`#ifdef`s i `ldhash.c`, Makefile per värd-CPU. SSE/AVX2-lexer efteråt som prestandasteg | `make -C ccs vcc` och `fuzz-regress` gröna; `vcc-same.py` mot utdata gjorda på Mac:en (arm64-utdata från x86-värd byte-identiska); paritet `vcc-diff.py cmp`; stänger TODO "x86-64 host build" |
| X1 | **Målval och front end.** `PP_LINUX_X86_64`, predefines ur clang, layout (tabellen ovan), `lib/f80.h` för x87-konstanter, `__float128` | `vpp -dM -E` = `clang -dM -E` (även `-march`-deltan, `bench/vcc-predef.py`); `vcc-sema.py layout` med x86-64 som mål; `vcc-ldconst.py` för x87; `vcc-bflayout.py` för x86-64; arm64/wasm byte-identiska |
| X2 | **Lagren på plats, heltals-`-S`.** `emit/rec/rec.h` och x86:s op-tabell; `emit/walk/` med `lower.h` och `LO()`; x86-64 i måltabellen (`emit/tu.c` väljer vandraren för x86-64); `arch/x64/emit/` för heltal, locals i stackplatser, parametrar, direkta anrop, `if`/loopar/`break`/`continue`/`switch` (kedja), `return`; prolog/epilog med `rbp`; `twoaddr.c` (behövs på alla nivåer: posterna är treadress), `print.c`. Bara `-O0`, inga optimeringspass | `-S` assembleras av `clang -c`, länkas mot glibc och körs nativt med samma resultat som clangs; nytt `bench/vcc-x86.py` (som `vcc-wasm.py`: testhuvudena mot glibc, nativ körning, cache); arm64/wasm byte-identiska (`vcc-same.py`) |
| X3 | **Hela C i vandraren och sänkningen.** Aggregat och initierare, SysV-klassning (`abi.c`), varargs, hopptabeller, flyttal (SSE, x87), `__int128`, `__float128` via `FN_*`, bit-fält, VLA/`alloca`, atomics, TLS, PIC/PIE, inbyggda, inlining och `need_fold`, kända värden, spill; `VCC_WALKLOG` | gcc-torture och tcc-tests2 vid `-O0`: samma godkända som clang; `vcc-abi.py --target x86_64` (clang/vcc × anropare/anropad, gcc som tredje part); `vcc-pic.py`, `vcc-fpcheck.py` för x86-64; `bench/vcc-walkcmp.py` mot arm64 grönt |
| X4 | **Gemensamma pass och `-O1`.** `emit/rec/` (liveness, kopiepropagering, döda resultat, stackplatser, hopp) över op-tabellen; `B_LREG` i `rbx r12–r15`; `peep.c`; `twoaddr.c` byter kommutativa operander | Sviterna ovan vid `-O1` och `-O2`; kodstorlek per funktion mot `clang -O1` på kärn-TU:er (mål: inom arm64:s kvot mot clang); tid per funktion mot arm64 med `vbench.py` |
| X5 | **Objektfiler.** Kodare för det generatorn skriver, fixups, ELF-maskin 62, hoppavslappning; `-c` direkt från posterna utan text; CFI och DWARF med x86-registernummer | `vas-obj.py`: vcc:s `-c` byte-identiskt med `clang -c` av samma `.s`; `vas-enc.py` mot llvm-mc; X4:s sviter med `-c`; `vcc-cfi.py` och `vcc-dwarf.py` för x86-64 |
| X6 | **Inline asm och `vas` för x86-64.** AT&T-parser, begränsningar och modifierare, flaggutdata, `asm goto`; hela ISA:n inklusive SSE/AVX/AVX-512 (kärnans krypto-`.S`) | `vas-corpus.py` för x86-64 (`clang -S` av testfilerna, `clang -c` mot `vas`); kärnans `.S` byte-identiska med clangs objekt |
| X7 | **vlink för x86-64.** Relokeringar, GOT/PLT, TLS-avslappning (GD/IE→LE), GOTPCRELX-avslappning | `bench/vcc-x86.py` med vlink: laddad bild byte-identisk med lld:s; `vlink-opts.py` |
| X8 | **Kärnan.** `-mcmodel=kernel`, `-mno-red-zone`, `-mno-sse -mno-mmx -mno-80387`, `-mstack-alignment=8`, `-mfentry -mrecord-mcount`, retpoline/rethunk (`-mretpoline-external-thunk`, `-mfunction-return=thunk-extern`, `-mindirect-branch-cs-prefix`), IBT (`-fcf-protection=branch`, `endbr64`), `-mharden-sls=all`, `-fpatchable-function-entry`, stackskydd via `%gs` (`-mstack-protector-guard-*`) | objtool utan varningar på alla objekt; `vcc-diff.py`/`vcc-sema.py` på x86-64-trädet; bootar Debian i `qemu-system-x86_64 -enable-kvm`; vccd-bygge med tider |
| X9 | **Intrinsics.** `__builtin_ia32_*` med vektortyper för clangs `immintrin.h` (SSE4.2-CRC, `popcnt`, AVX2) | `vcc-builtins.py` för x86-64; PostgreSQL byggt och testat på x86-64 Linux (`bench/pg/`) |

Ordningen gör att varje steg syns: X2 ger körbara program via clangs assembler och
alla tre lagren i minsta form, X4 optimerad kod, X5 egna objekt, X8 kärnan. X0 är förutsättningen för allt (inget går att köra här
annars) och X1 kan påbörjas parallellt med X0:s lexerdel. X9 behövs inte för
kärnan och kan vänta.

## Validering

- **Referens:** clang med `--target=x86_64-linux-gnu`. `-S` jämförs inte rad för rad
  (instruktionsvalet skiljer); det som jämförs är beteende (nativ körning), ABI
  (anrop mellan clang-, gcc- och vcc-objekt), layout (`vcc-sema.py layout`) och
  objekt för samma `.s` (byte för byte, som `vas-obj.py`).
- **Körning:** nativt på värden, ingen emulering. Samma cache som
  `vcc-ctests.py`: ett objekt byte-identiskt med ett som gått igenom körs inte igen.
- **arm64 och wasm oförändrade:** `vcc-same.py` med en bas från `vbench.py mkbase`
  i varje commit (x86-64-koden ligger bredvid, men `emit/tu.c`, `emit/data.c`, `cc/`
  och `obj/` delas); `vbench.py gate`.
  arm64-körningarna (ctests med qemu-aarch64, `vcc-abi.py`) görs på Mac:en eller i
  `env/linux/`-containern tills qemu-user finns här.
- **Prestanda:** x86-64-utdata ska kosta ungefär som arm64 per funktion. Mäts med
  `vbench.py` på x86-64-kärnans TU:er när X5 är klart; kodaren är det som kan
  kosta (variabel längd, avslappning).

## Risker och öppna frågor

- **Clang-version:** värden har clang/lld/llvm-mc 22 (`/usr/lib/llvm-22/bin`),
  qemu-system 10.2 med KVM och qemu-user med aarch64-sysroot (2026-11-10).
  arm64-tabellerna kommer från clang 23. x86-64:s predefines och `__has_*` (X1)
  bör tas från samma version, annars syns versionsskillnader som målskillnader.
  clang 22 räcker för X0 och för beteendetesterna.
- **Registerbrist och fasta register:** 16 GPR, varav några bundna av `div`,
  skift och argument. Vandrarens spill (runt anrop, och ett nytt försök med
  `e->spill`) måste räcka utan att x86-sänkningen spillar på egen hand. Testas i
  X3 med gcc-torture:s djupa uttryck. Räcker det inte, är nästa steg spill per
  operand i vandraren, inte i `arch/x64/`.
- **Byte-identiska objekt kräver MC:s avslappning exakt:** samma fixpunktsordning
  för `jmp`/`jcc` rel8/rel32, samma flerbyte-`nop` för `.p2align`, samma val mellan
  likvärdiga kodningar (t.ex. `mov` med `89` eller `8b`). Kodartabellen tas ur LLVM
  en gång, så valen följer med.
- **Reglerna glider isär** mellan `emit/walk/` och `arch/a64/emit/` under
  övergången: `vcc-walkcmp.py` i X3 och X4. En regeländring görs i båda till
  arm64 är flyttad.
- **Gränssnittet passar inte arm64 senare:** det ritas bara för x86-64. Risken
  hålls nere genom att posthuvudet är arm64:s och lagren följer arm64:s
  filindelning. Det som ändå inte passar ändras när arm64 flyttas.
- **Kärnan:** `__seg_gs` (percpu med named address spaces) om
  `CONFIG_USE_X86_SEG_SUPPORT` slås på för clang; objtool kräver att vcc:s
  kontrollflöde går att följa (inga hopptabeller objtool inte känner igen, rätt
  `.discard.*`/annotation-sektioner, `ud2` efter `noreturn`-anrop som clang).
- **x87:** vikning av 80-bitarsvärden måste avrunda exakt som clangs APFloat
  (precision 64 bitar, denormaler); `vcc-ldconst.py` med x86-64 som mål avgör.
- **OS mot CPU:** det som visar sig vara gemensamt för arm64- och x86-64-Linux
  (TLS-modellval, GOT-regler, sektionsnamn) flyttas till `platform/linux/`; det
  stänger TODO-punkten om Darwin-grenarna.

## Status

### X3 (2026-11-10): pågår, i gren `claude/x86-x3-main` (PR jdahlin/vroom#11; #9 hamnade i #4:s gren efter att #4 slagits ihop)
- Klart: biblioteksinbyggda som anrop; aggregat (initierare, kopior, sammansatta
  literaler, medlemmar av rvärden); bit-fält; System V-anrop (klassning,
  poster som argument och resultat, dold resultatpekare, varargs med
  registersparområde); flyttal (SSE för float/double, x87 för long double via
  minnesplatser, `st(0)`-resultat, alla konverteringar); `__int128` (16-byte
  objekt via adress, libgcc för `/ % <<` och flyttalskonvertering); inbyggda
  (bitoperationer, overflow via 128 bitar, `alloca`, `constant_p` utan inlining,
  `signbit` ...); VLA (storlekar i platser, arrayen under sp, sp återställs vid
  blockslut och när ett `goto` når deklarationen igen); TLS (local-exec,
  initial-exec, general dynamic under `-fPIC`); PIC/PIE (GOT-laddning via r11
  enligt arm64:s `sym_needs_got`, `@PLT`-anrop); atomics (`__atomic_*`/`__sync_*`
  via `LO(atomic)`: mov, xchg, lock xadd, lock cmpxchg, mfence; and/or/xor och
  `_Atomic`-objektens sammansatta tilldelningar som CAS-slinga i vandraren);
  komplexa tal (`N_CPLX`; `_Complex long double` i st(0)/st(1)); GNU-vektorer
  (via adress, lane för lane i vandraren; SysV-klasser som clang, SSEUP i ett
  xmm, 32 byte tillbaka i xmm0:xmm1 som clang utan AVX); etikettadresser
  (`.Ltmp.lbl.N` som datainitierarna) och beräknat `goto`; cleanup-variabler
  (blockslut, `break`/`continue`, `return` med värdet parkerat, `goto` ut ur
  scopes via ett träd från förskanningen, `for`-satsens första led); alignment
  över 16 (lokaler från rbx = justerad ram, stackargument via sparad och
  justerad rsp); bit-fält över en 8-bytegräns (delat vid gränsen).
- `bench/vcc-x86.py`: 1667/1770, lika i `--pie` och `--pic` (vcc-sidan
  `-fPIE`/`-fPIC`, länkad `-pie`). Körfel bara där clangs optimerare krävs
  (`20030125-1`, `bcp-1`, `builtin-constant`) och 11 länkfel av samma slag eller
  `__builtin_setjmp`. `vcc-abi.py --target x86_64`: 12/12, med tre nya tester
  (`int_records`, `fp_records`, `complex_args`).
- Kvar i X3: vektorer av `__int128`, bit-fält bredare än 64 bitar, `__float128`-aritmetik (libgcc `__addtf3` ...), inlining av
  `__always_inline` med `need_fold` och `constant_p` efter inlining,
  `-mgeneral-regs-only`/`-mno-sse`, UTF-8-namn citerade som clang, `VCC_WALKLOG`.

### X2 (2026-11-10): påbörjat, i gren `claude/x86-x2-walker`
- Lagren finns i minsta form: `emit/rec/rec.h` (posthuvudet, `opdesc`),
  `emit/walk/` (vandraren: uttryck med vikning av konstanter, lvärden, villkor
  som hopp, satser, switch som jämförelsekedja, anropssekvensen; `walk.h` med
  `LO()`-gränssnittet), `arch/x64/emit/` (`lower.c`: fysiska register, `div` och
  skift sparar sina fasta register, anrop sparar levande temporärer;
  `print.c`: `twoaddr` och AT&T-skrivaren). `emit/tu.c` väljer vandraren för
  x86-64, `data.c` skriver `.long`/`.quad` och x87-byte. `-c` för x86-64 är ett fel
  tills X5.
- `bench/vcc-x86.py`: 777/1768 godkända (gcc 696, tcc 62, vcc 19), inga
  körfel; resten avvisas med "not supported yet (PLAN-x86.md X3)" (inbyggda,
  aggregat, flyttal, bit-fält, varargs ...) och 5 `link_error`-tester som kräver
  clangs -O2-vikning. Under ASan/UBSan 774: de tre skillnaderna är fel i
  front end som finns för arm64 också (`cc/record.c:136`, `cc/expr.c:36`,
  `cc/func.c:7`).
- Kvar i X2: op-tabellen fylld för x86 och `VCC_WALKLOG`/`vcc-walkcmp.py`
  (behövs först när passen och inliningen kommer, X3/X4).

### X1 (2026-11-10): front end klar, x86-data och vcc-ldconst väntar på kodgeneratorn
- `PP_LINUX_X86_64`: `--target=x86_64-*`, och standard utan `--target` på en
  x86-64-värd (glibcs `x86_64-linux-gnu`). Clang-data i egna filer från
  `pp/gen-x86data.sh` (clang 23.1.6): predefines och `-dD`-ordning
  (`pp/ppdata-x86.inc`), `__has_*` (`has_tab_x86`) och attributnamn
  (`cc/attrnames-x86.inc`). De andra målens tabeller (23.1.4) rörs inte.
- `va_list` som `struct __va_list_tag[1]`; `long double` som x87 i konstanter och
  vikning (`lib/f128.h`: binary128-layout avrundad till 63 bitar, `q_*p`);
  `__float128` som egen typ (`TY_FLOAT128`, suffix `q`). `char` signed, `wchar_t`
  int. glibcs huvudfiler går igenom hostat.
- Verifierat: `-dM` = clang för 29 flaggset, `-dD`-ordning för 17;
  `vcc-bflayout` 6604 påståenden utan avslag; `bench/vcc-f80.sh` (x87-vikning mot
  värdens FPU) 16,3M kontroller utan fel; `long double`/`__float128`-vikning och
  `va_list`-diagnostik som clangs; `-fsyntax-only` över 1903 testfiler som clang
  utom där aarch64 avviker likadant. arm64 oförändrat: `vcc-same` (enda skillnad:
  sökvägen till baskopians include-katalog), `vcc-ldconst` 6000/6000.
- Kvar till senare steg: `vcc-ldconst` och `vcc-sema.py layout` för x86-64 (behöver
  `-c` resp. ett x86-kärnträd), x87-byte i data (`q_to_x87` finns), inline asm för
  x86 (X6), `vpp -dM` för `-march`-deltan (`-msse4.2`, `-mavx2`, ...).

### X0 (2026-11-10): bygger och kör, SIMD-lexer och kärnparitet kvar
- `make -C ccs vcc` på x86-64 Linux med clang 22: `classify`, `find_cend`,
  `find_nl` och `find_quote` i `lex/lex.c` har en skalär version utan NEON
  (samma masker bit för bit), CRC32C via SSE4.2 (`lib/hostcrc.h`, samma polynom),
  `pause` i spinnslingan, `-march=native -msse4.2`. Fuzz-målen tar värdens clang
  och libFuzzer (LeakSanitizer är på som standard på Linux: `ASAN_OPTIONS=detect_leaks=0`
  för regress-körningen, eftersom den rapporterar läckor redan på tom indata).
- Verifierat: `fuzz-regress` grönt; `fuzz-lex` ~1M körningar (fönstervägen med
  skalära masker mot referenslexern) utan fel; `-S -O2`, `-c -O2`, `-S -O0`, `-E` av
  vcc-, gcc-torture- och tcc-sviterna (1903 filer) byte-identiska mellan x86-64-bygget
  och ett arm64-korsbygge (NEON) som körs i qemu-aarch64.
- Kvar: SSE2/AVX2-`classify` (den skalära tar en byte i taget), `vcc-diff.py cmp`
  och `vcc-ctests.py` här (inget `work/`-träd; qemu-system-aarch64 saknas).

Övriga steg inte påbörjade. Lagerindelningen (vandrare, sänkning, gemensamma
pass) beslutad 2026-11-10; arm64 och wasm flyttas senare (TODO.md).
