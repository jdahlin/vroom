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

**Gemensam implementationsplan (2026-11-11):** se
`PLAN-fas2.md`, **Gemensam optimerare utan SSA**, OPT0–OPT8. C-semantik,
strategier och postalgoritmer ägs där; X4 ansluter x86 via målbeskrivningar.
Generella regler skall användas av x86, arm64 och wasm, inte utvecklas
som nya x86-specialfall. Arm64:s befintliga optimerare är källan till
delade record-pass. Rena extraktioner bevarar utdata bytevis; när en
gemensam regel aktiveras är dess avsedda ändringar på alla mål tillåtna
och verifieras enligt den gemensamma planen. Detta preciserar den äldre
regeln ovan om oförändrade arm64/wasm-utdata.

**Nuvarande avgränsning:** x86-64 är första och enda användaren. arm64 och wasm behåller sina
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
| X4 | **Inkoppling av gemensam optimerare och `-O1`.** `PLAN-fas2.md` OPT0–OPT8 äger regler/algoritmer; x86 fyller op-/effekttabeller och ansluter `emit/rec/`, lokala register och måloperationer. SIB/LEA, SSE2, målfeatures, prolog och tvåadress förblir x86. Ingen egen kopia av generisk optimeringspolicy. | C-regressioner på alla tre backends, x86-kodkrav och ABI vid O0–O3; ren extraktion byte-identisk på arm64. Mät kompilatorns kostnad och genererad kod enligt OPT-planen; bekräfta x86-stöd innan `vbench.py` anges som x86-grind. |
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

### X5 (2026-11-11): direktkodning från posterna, plan

Mätt: kernel/fork.c `-c` 0,15 s mot `-S` 0,11 s; assemblern på texten är ~25 %
av TU:n.  Väg, steg för steg, varje steg byte-identiskt (`vcc-x86.py --objcmp`):
1. En assemblersession utan text (`as_open/as_close` på ElfObj, som `as_run`
   gör men utan källa): sektioner, symboler, etiketter (`as_label`), hopp via
   `as_branch` (samma relaxering), och `as_line` för det som får vara text
   (data, direktiv, CFI, `.loc`, inline asm) i samma session.
2. `x64_encode(e, rec)`: posterna X_MOV/X_LD/X_ST/X_LEA/ALU/X_CMP/X_TEST/
   X_SETCC/X_CALL/X_JMP/X_JCC/X_PUSH/X_POP direkt till `XForm` + `x64_emit`
   (assemblerns kodare: MC:s val ärvs); övriga poster som en rad text.
3. Andelen textrader mot noll: SSE/x87-posterna, prolog/epilog, CFI (`cfi_*`
   anrop i stället för `.cfi_*`-text), `.loc` (dw_row direkt).
4. Data och sektionshuvuden direkt (som arm64:s IT_SYMHDR/IT_DATA).
Validering per steg: --objcmp 100 %, -g/-fasynchronous-unwind-tables, kärnans
TU:er (`vcc -c` mot `vcc -S | vasm`: samma objekt), tid per TU (vbench).

### X8 (2026-11-11): x86-kärnan byggs med vpp, påbörjat

- `work/vcc-x86-cc.sh` (vpp som CC, bara versionsfrågorna till clang),
  `make O=work/build-x86 ARCH=x86_64 LLVM=1 CC=... defconfig vmlinux`.
  Kbuilds sonder ger nu samma `.config` som clang:s (utom CC_CAN_LINK):
  okända -W och gcc-flaggor avvisas som clang, -D som omdefinierar ett
  fördefinierat makro varnar.
- Härdningen som kärnan begär, som clang skriver den: `-fcf-protection=branch`
  (endbr64, IBT-noten), `-fpatchable-function-entry=N,M` (nop:ar, sektionen
  `__patchable_function_entries` med SHF_LINK_ORDER per funktion),
  `-mfunction-return=thunk-extern`, `-mretpoline-external-thunk` med
  `-mindirect-branch-cs-prefix`, `-fstack-protector*` med
  `-mstack-protector-guard-reg/-symbol`, `-ftrivial-auto-var-init=zero`.
- Rättat på vägen: statiska grenar (adress-argument bundna vid inlining,
  "i" från frontändens konstant), ASM_CALL_CONSTRAINT (%rsp), cpuid (utdata
  via pekare), call_on_stack (callee-saved operander), rip_rel_ptr,
  `#pragma GCC visibility`, döda &&-operander, asm goto med utdata,
  ALTERNATIVE (.skip/.fill som layouten avgör), x86-assemblern (SSE4, AES,
  SHA, CRC32, AVX/AVX2 via VEX, system- och CET-instruktioner, kontroll-
  register, registeralias), x86-assemblern delad per instruktionsfamilj.
- Kvar (24 objekt av ~2950): `.code32`/`.code16` (realmode, efi-mixed,
  vdso32, kompat-entry), AVX-512 (EVEX) i krypto, `.org`-mål, några
  direktiv; sedan länkning (vlink), objtool och boot under qemu.

### X4 (2026-11-11): påbörjat, vikningar i vandraren

- `wk_cond` och `if`/`?:` använder arm64:s formvikningar (`cond_fold`: x >> 0 != x,
  x < y && x > y, intervall); ett villkor som alltid hoppar gör grenen död
  (`wkst.cjump`), och `?:` med känt villkor (efter inlining) genererar en arm.
- 0 << x, 0 >> x, -1 >> x viks; laster av const-globaler (heltal, och (int) av
  en const double) viks som arm64:s `const_global`.
- a[K] = konstant för lokala fält som bara används som a[KONST] (ingen SA_AESC)
  följer med till laster i rak kod (`wkst.ael`; en etikett glömmer dem), som
  arm64:s ael: pure-1.
- (float)floor((double)f) → floorf(f) (även ceil, trunc, round, rint,
  nearbyint, fabs) med optimering, som clang -O2: 20030125-1.
- `vcc-x86.py -j 6`: 1776/1776; --pic, --obj och -g lika; --objcmp 1846/1846,
  --fpcheck 1908/1908, --vlink 1758/1758, vcc-abi x86_64 12/12; arm64 oförändrad.
- Kvar i X4: ett riktigt pass på records (registerallokering, kopie-
  propagering, som arm64:s opt.c) och -O1 som eget läge.

### X7 (2026-11-11): vlink för x86-64, statiska länkar klara

- `arch/x64/ld/x64.inc` (inkluderas av `ld/ld.c`): relokeringsskanning och
  -tillämpning, GOTPCRELX-avslappning, GD/IE→LE, TLS variant 2
  (`tls_tp` = blockets slut), 4K sidstorlek som standard, `SHT_X86_64_UNWIND`
  som indata (utdata PROGBITS som lld), int3-utfyllnad i körbara sektioner,
  tom `.got` kvar efter avslappad GD/LD, `__tls_get_addr` ej odefinierad.
- `.note.gnu.property`: när AND av FEATURE_1_AND är 0 släpps alla (som lld);
  annars ligger indata kvar (sammanslagen not: kvar att göra).
- Testkörtid: `tests/rt/start-x86.S`, `link-x86.ld` (statiskt, eget TLS).
- `bench/vcc-x86.py -j 6 --vlink`: 1750/1751 laddade bilder byte-identiska
  med lld:s och körs; kvar 20030125-1 (floor→floorf, kräver optimeraren).
  arm64 oförändrad: `VCC_LDCMP=1 vcc-ctests.py` 1780/1780, vlink-opts, vlink-bad.
- Statisk PIE (`--vlink --pie`: rt med -fPIE, `-static -pie --no-dynamic-linker`,
  start-x86.S tillämpar R_X86_64_RELATIVE): 1749/1750 byte-identiska och körs.
  Tom `.rela.dyn` som skriptet namnger blir RELA som lld:s (bara x86 än så länge).
- Kvar: delade objekt och dynamiskt länkade program (PLT, .dynamic) för x86.

### X5/X6 (2026-11-11): assembler och objekt, påbörjat
- `arch/x64/as/asm.c`: AT&T-parser och kodare (heltal, SSE, x87, prefix, `@PLT`/`@GOTPCREL`/
  TLS-modifierare), med MC:s val av form (ackumulatorformer, imm8/disp8, `c7`/`b8`, `d1` för
  skift med 1); hopp till etiketter som relaxerade poster i `as.c` (`VI_X86BR`, kort tills
  avståndet eller målet kräver nära form med `PC32`/`PLT32`). `arch/x64/obj/fixup.c`:
  x86:s fixups (lösta på plats som MC, GOT/TLS behåller symbolen); `ElfObj.machine`
  (EM_X86_64: inga mappningssymboler, MC:s nop-utfyllnad); `#`-kommentarer, citerade
  etiketter. `vasm --target=x86_64-linux-gnu`; `-c` för x86-64 går genom texten
  (`E.objtext`) och assemblern.
- `vcc-x86.py --objcmp`: vcc:s `.s` genom vasm och genom `clang -c`, objekten byte för
  byte: 1846/1846. `vcc-x86.py --obj` (vcc:s sida med `vpp -c`): 1768/1776, som med `-S`;
  lika med `--pic`. arm64: `vas-obj.py` 45/45.
- `vas-corpus.py --target x86_64-linux-gnu` (clangs `-S` av testfilerna vid `-O0`/`-O2`,
  vasm mot `clang -c`): 3679/3680 identiska (SSE2-heltal, x87-former, `shld`/`bt`/`prefetch`,
  CFI med x86:s CIE och `.eh_frame` som `SHT_X86_64_UNWIND`, MC:s relaxering mot förra
  passets layout). Kvar: `20050224-1 -O2`, ett relaxeringsfall vid en `.prefalign`.
- CFI (`-fasynchronous-unwind-tables`: prolog och epilog som clang, sparade callee-saved)
  och `-g` (radtabell från vandraren, `.loc` före posterna, `twoaddr` flyttar raderna) i
  x86-skrivaren; `.word` är 2 byte på x86; DWARF-katalogen för en delad `.file`-sökväg som
  MC; `.quad x@DTPOFF` m.fl. i data. Med `VCC_CFLAGS='-g -fasynchronous-unwind-tables'`:
  sviten 1767/1775 med `-S` och `--obj`, `--objcmp` 1846/1846; `backtrace()` och gdb går
  genom vcc:s funktioner; `vas-corpus` med `-g` 3679/3680.
- Kvar: kodning direkt från posterna (utan text), hela ISA:n (AVX, AVX-512 för kärnans
  krypto) och `vas-enc.py` för x86-64.
### X3 (2026-11-11): interrupt-anropskonvention

- `interrupt` får hårdvaruram/error-parameter från stacken, sparar alla GPR,
  rensar DF under handlern och återvänder med IRETQ. `-muintr` ger UIRET och
  `__UINTR__`. Felaktiga signaturer och direkta C-anrop diagnostiseras.
- Utan `-mgeneral-regs-only` sparas x87/MXCSR/XMM med FXSAVE64/FXRSTOR64;
  en privat tom x87-stack gör även en full avbruten stack säker. YMM/ZMM-
  överdelar används inte av vcc; extern kod/inline asm som ändrar dem måste
  bevara dem själv. Kärnkod utan aktiverat FP-tillstånd använder som vanligt
  `-mgeneral-regs-only`.
- `bench/vcc-interrupt.py`: faktiska syntetiska IRETQ-returer, med/utan
  error-kod, alla GPR/XMM, full x87-stack, DF och överjusterade lokaler;
  `-O0/-O1/-O2`, både `-S` och `-c`. UIRET kodning och inkommande ram
  verifieras, men själva instruktionen kräver OS/hårdvarustöd och körs inte.

### X4 (2026-11-11): implementationsplan flyttad till gemensam emitter

Kodgranskning efter rebase på main: arm64 har redan registerlokaler och
record-pass, x86 saknar inkopplingen, wasm har egna lokaler och strukturerad
sänkning. `PLAN-fas2.md` **Gemensam optimerare utan SSA** har OPT0–OPT8 med
konkreta filer, beroenden, kostnadsgränser och tester på alla tre backends.
De tidigare auditernas x86-regler är fynd/prioritering, inte beslut att
implementera dem i x86:s `lower.c`. Ingen optimering implementerad; X4 öppet.

### Registerlokaler, delprogress 2026-11-11

OPT3 i `PLAN-fas2.md` har gemensamt urval och x86:s fyra callee-save-hem,
med asm-/nonlocal-/alignmentsäkerhet. Egna sviten 137/137, kors-ABI 22/22;
alla kalla auditprober oförändrade efter lönsamhetsfilter. En liten loop
förbättras men kompilator-CPU-grinden är fortsatt öppen. Direkt ABI-bindning är nu levererad och verifierad med smala argument,
stackargument, sret och varargs. Kopiestädning återstår; full O2-paritet
är inte uppnådd. Se OPT3:s nästa statusblock för parade mätningar.

### X4 (2026-11-11): reproducerbar nivåmatris och testprober

`bench/vcc-optaudit.py` jämför 29 funktioner i
`bench/optaudit/probe.c` vid O0/O1/O2/O3, baseline x86-64 och Ryzen-native.
Körning: `PATH=/usr/lib/llvm-23/bin:$PATH python3 bench/vcc-optaudit.py --native`.
Separat worktree `audit/x86-optimizations`, vcc byggd från `52261ef`,
clang 23.1.6 och GCC 15.2.0 på Ryzen 7 9700X. Huvudworktreets
ocommittade ändringar ingår inte. Ingen kompilatorkod ändrad.

Alla 24 kombinationer kompilerar, assemblerar, länkar och ger samma
checksumma `1922a5c83358247b`. Samma clang-byggda anropare används för alla
objekt. Den testar 512 deterministiska indatauppsättningar, looplängder
0–64, signed division med positiva/negativa värden och explicita
gränsvärden inklusive LONG_MIN/ULONG_MAX. Detta är riktad differentiell
kontroll, inte ett bevis för all indata. Ingen tidtagning eller tung last.
Distrohärdning normaliseras med `-fno-stack-protector -fcf-protection=none`;
alla använder `-fno-pie`.

**Matrisen nedan anger första observerade nivå för just proben.** `saknas`
betyder att transformationen inte sker vid någon av O0–O3 i denna probe,
inte att ett helt optimeringsområde är uttömmande testat. En högre nivå
kan välja annan kod, och samma effekt kan åstadkommas av flera pass.

| Transformation / probe | vcc | Clang | GCC |
| --- | --- | --- | --- |
| Literal konstantvikning (`fold`) | O0 | O0 | O0 |
| Konstant lokalskalär ersätts med literal (`local_const`) | O1 | O1 | O1 |
| Kända arrayelement viks (`array_const`) | O1, men stores kvar | O1, array borta | O1, array borta |
| `always_inline` (`always`) | O0 | O0 | O0 |
| Liten explicit `inline` (`inline`) | O1 | O1 | O1 |
| Liten vanlig statisk funktion inline (`auto_inline`) | saknas | O1 | O1 |
| Ta bort `+0` och `*1` helt (`identity`) | saknas | O1 | O0 |
| Unsigned `/8` med shift (`div8`) | saknas | O0 | O0 |
| Unsigned `%8` med mask (`mod8`) | saknas | O0 | O0 |
| Signed `/8` utan idiv (`sdiv8`) | saknas | O1 | O0 |
| Unsigned `/10` utan div (`div10`) | saknas | O1 | O0 |
| `x*9` med en LEA (`mul9`) | saknas | O1 | O1; shift/add redan O0 |
| `p[i+3]` som en indexerad minnesoperand (`indexed`) | saknas | O0 | O1 |
| Skalära parametrar/lokaler utan stackspill (`add32`, `aggregate`) | saknas | O1 | O1 |
| Store-immediate (`stores`) | saknas | O0 | O0 |
| Ta bort överskriven store (`stores`) | saknas | O1 | O1 |
| Återanvänd icke-volatil load (`reload`) | saknas | O1 | O1 |
| Behåll båda volatila läsningarna (`volatile`, kontrollfall) | O0–O3 | O0–O3 | O0–O3 |
| Gemensamt deluttryck beräknas en gång (`cse`) | saknas | O1 | O1 |
| Eliminera andra intervalltestet (`range`) | saknas | O1 | O1 |
| Wrapper som tail jump (`tail`) | saknas | O1 | O2 |
| Täta konstanta switch-resultat som värdetabell (`switch`) | saknas | O1 | O2; kod-hopptabell O1 |
| Explicit 128-bitars heltalsvektor med packed add (`vector_add`) | saknas, skalär | O0 | O0 |
| Sum-loop automatiskt vektoriserad (`sum`, baseline) | saknas | O2 | O3 |
| Sum-loop automatiskt vektoriserad (`sum`, native) | saknas | O2 | O2 |
| Invariant `x*y+1` ut ur loopen (`licm`) | saknas | O1 | O1 |
| Restrict-copy-loop blir memcpy (`copy`) | saknas | O1 | O2 |
| Oanvänd lokal beräkning tas bort (`dead_local`) | saknas | O1 | O1 |
| Konstant if: död arm tas bort (`cond_fold`) | saknas; grenen väljs men döda instruktioner kvar | O0 | O0 |
| POPCNT med native-mål (`popcount`) | saknas, SWAR | O0 | O0 |

**Vcc:s nivåer:** `driver/ppdriver.c` skickar opt=0 för O0, opt=1 för O1
och opt=2 för O2/O3. Alla opt-kontroller i x86-vandraren testar bara
noll/skilt från noll; `x64_finish` har bara tvåadresspasset. O1/O2/O3 ger
byte-identisk assembler i detta korpus, både baseline och native.
Native ändrar inte vcc:s assembler här. Detta visar ingen effektiv
native-instruktionsselektion för dessa fall; det betyder inte att värden
alltid skall vara målmaskinen. FP-temporärer ligger redan i SSE-register;
hela TODO:s äldre formulering om FP-register gäller inte x86 rakt av.

**Aktiverade alternativ kontra verkliga transformationer:**
`build/linux/optaudit-levels/` innehåller per kombination `.s`, `.o`,
`.dis`, `.passes` och körbar fil; för GCC även `.options` och `.remarks`.
`report.json` har kommandon, versioner, CPU, revision, SHA256 för vpp och
proberna, funktionsstorlekar och råa instruktioner samt passinventarier.
GCC:s options kommer från `-Q --help=optimizers`, passlistan från
`-fdump-passes`; Clangs observerade pass och remarks från
`-Xclang -fdebug-pass-manager -Rpass=.* -Rpass-missed=.* -Rpass-analysis=.*`.
GCC:s flagglista, dess konfigurerade pass och LLVM:s körda pass är olika
mått. ON innebär inte att ett pass ändrade programmet; LLVM-listan är
inte en fullständig inventering av alla backend-transformationer.

Exempel: LLVM kör `LoopVectorizePass` även vid O1 här men sum-loopen
vektoriseras först vid O2. GCC har loopvektorisering aktiverad vid O2
men väljer skalär baseline-kod i sum-proben; native får vektorkod redan
vid O2. O3 gör alltså inte en transformation automatiskt lämplig.

Observerad skillnad O2→O3: GCC lägger bland annat till loop interchange,
unroll-and-jam, peeling, unswitching, predictive commoning och IPA
constant-propagation cloning. LLVM:s logg tillför passnamnen
`ArgumentPromotionPass`, `CallSiteSplittingPass` och
`ControlHeightReductionPass`; andra redan förekommande pass kan ändra
budget och heuristik utan att ett nytt namn tillkommer. Dessa områden
är inventerade men har inte egna isolerade beteendeprober i denna omgång.
Vcc har inte motsvarande generell analys/pipeline på x86 i auditbasen.

**Från prober till regressionstester:** körkontrollen är redan en testbas.
Harnessen kräver också två volatila inputläsningar, vikning av
literaluttrycket och frånvaro av anrop i `always_inline` vid alla nivåer.
För varje implementerad optimering tillkommer ett riktat kodkrav, t.ex.
ingen div i unsigned `/8`, packed add för explicit vektor, eller en enda
icke-volatil läsning. Kraven skall vara funktions- och målavgränsade,
inte en hel assembler-snapshot eller allmän instruktionstakgräns.
Saknade optimeringar rapporteras nu som fynd, inte som godkända tester.
Framtida minnesoptimeringar behöver negativa fall för alias, volatile,
atomics och asm; loopoptimeringar behöver noll iterationer; inlining
behöver sidoeffekter och ABI-fall. Prestanda mäts separat för både
kompilatorn och genererad kod. Denna utvecklaraudit är ingen ny CI-grind:
den kräver native x86, två referenskompilatorer och GNU binutils och
jämför även saknade egenskaper. G5:s benchmarkkrav är fortfarande öppet.

GCC använder assembly- och optimeringsdumpkontroller
([dg-final](https://gcc.gnu.org/onlinedocs/gccint/Final-Actions.html)); LLVM
har isolerade pass/IR- och backendtester med FileCheck
([TestingGuide](https://llvm.org/docs/TestingGuide.html)) och separat
helprogramssvit för korrekthet, kör- och kompileringstid
([TestSuiteGuide](https://llvm.org/docs/TestSuiteGuide.html)). Deras tester
är bra förebilder för struktur men ingen garanti för optimalt resultat
på godtycklig kod. För vcc är små C-tester och maskinpost/assemblerkrav
mer direkt återanvändbara än LLVM:s IR-tester.
Vår befintliga `vcc-gcc-dg.py` avvisar kontrakt med `dg-final` (testat i
`test-gcc-dg.py`); den kör alltså inte GCC:s assemblykontroller i dag.
Återanvändning av sådana upstreamfall kräver en medveten utökning eller
portning av kodkravet, inte bara att filerna kompilerar och kör rätt.

### X4 (2026-11-11): audit av billiga optimeringar utan SSA

Audit i separat worktree `audit/x86-optimizations`, bas `52261ef`. Här
betyder billig låg kompilatorkostnad: lokal sänkning och högst ett linjärt
pass över maskinposterna, inga fixpunktsiterationer eller global aliasanalys.
Ingen optimering implementerad; X4 är fortfarande inte färdigt.

På Ryzen 7 9700X jämfördes 13 små funktioner med vcc `-O2`, clang 23.1.6
och GCC 15.2.0 `-O2 -march=x86-64`; referenserna också med `-march=native`.
Detta är assemblerinspektion, inte uppmätt körtid eller speedup. Basen är
committad kod, inte huvudworktreets pågående ändringar. Proberna och de fem
assemblerfilerna finns lokalt i `build/optaudit/` i audit-worktreet.

| Ordning | Lucka och belägg | Billig implementation utan ny IR/SSA |
| --- | --- | --- |
| 1 | `unsigned long x / 8` och `x % 8` använder `divq`; båda referenserna använder `shr` respektive `and` | Specialfall för konstant tvåpotens i `x64_bin`, O(1) per operation. Signed division behöver bias för avrundning mot noll; börja med unsigned. Behåll fel/UB-beteendet för noll separat. |
| 2 | `add32` innehåller `movl %r10d,%r10d`; `indexed` signutökar ett redan signutökat resultat; returer hoppar till nästa etikett | Ett framåtpass med en liten tabell över registrens kända bredd/utökning samt borttagning av hopp till omedelbart följande etikett. Nollställ fakta vid etiketter, anrop och asm. En 32-bitars självmove kan nollställa övre halvan och får bara tas bort när den redan är känd noll. |
| 3 | `x*9` använder `imulq`; referenserna `leaq (rdi,rdi,8)` | O(1)-regler för tvåpotenser och 3/5/9; lägg till index/scale i LEA-sänkningen. Behåll flaggornas semantik eller bevisa att de inte används. Undvik långa shift/add-kedjor för godtyckliga konstanter. |
| 4 | `p[i+3]` använder separat add/mul/add; referenserna `movl 12(rdi,rsi,4),eax` | Matcha adressens AST vid minnesoperationen; bas + index*1/2/4/8 + displacement. Kräver utökning av `lval`/poster/skrivare, men ingen analys över hela funktionen. Respektera bredd, signed index och evaluationsordning. |
| 5 | `*p=2` materialiserar 2 i ett register | Store-immediate direkt vid sänkning. x86:s 64-bitars store-immediate tar endast signutökad imm32. Att dessutom ta bort en tidigare store är en separat minnesanalysfråga. |
| 6 | Native referenser använder `popcntq`; vcc använder SWAR | Välj instruktion med en redan beräknad målfeature-bit. Kräver korrekt `-march`/`-m*`-hantering, inte kontroll av byggvärden i backend. SWAR är giltig baseline och är inte i sig en optimeringsbugg. |
| 7 | Explicit `vector_size(16)` unsigned add går lane för lane via stacken; baseline-referenser använder `paddd` | Direkt SSE2-sänkning för en liten uppsättning explicit angivna vektoroperationer, O(1) per operation. Ingen automatisk vektorisering eller loopanalys behövs. |

Nästa nivå utan SSA, men större arbete:

- **Konstant division utöver tvåpotenser.** `/10` använder `divq`, mot
  referensernas multiply-high + shift. Beräkna magic multiplier med en
  verifierad algoritm, högst O(bitbredd) per konstant, gärna cache per TU.
  Testa 32/64 bitar, signed/unsigned, extremvärden och remainder separat.
- **Lokalt registercache för frame slots.** Parametrar och lokaler ligger
  i minnet; alla inkommande parametrar får platser i `wk_function`.
  Ett begränsat slot→register-cache inom ett basic block kan minska
  trafik utan SSA. Invalidera för aliasande stores, adressflykt, asm,
  anrop, joins och volatil/atomisk åtkomst; fysisk registerpress måste
  hanteras. Detta är större än en peephole, men sannolikt bredare nytta.
- **Switch.** Åtta konstanta returfall blir en jämförelsekedja; båda
  referenserna använder värdetabell. Värdetabell för täta konstantfall,
  hopp-tabell för täta kodfall, balanserat träd för glesa fall. Begränsa
  tabellstorlek; sortering O(k log k), inte kvadratisk sökning.
- **Tail calls.** En enkel wrapper gör vanligt anrop och epilog, mot ett
  `jmp` hos referenserna. Begränsa först till ABI-kompatibla direkta
  anrop utan stackargument, cleanup, dynamisk stack eller adress till
  egen ram som kan användas av mottagaren.
- **Lokal CSE/load forwarding/DSE.** `reload` läser samma pekare två
  gånger; `stores` behåller båda skrivningarna. Börja bara med exakta,
  icke adressflydda frame slots. Nuvarande `rec` saknar uttrycklig
  volatile/atomic-markering; radering av godtyckliga minnesposter är
  därför inte en säker första regel. Lägg effekter och implicita
  register/flags i op-tabellen innan generella record-pass.

Befintligt som inte skall räknas som saknat: konstantvikning,
villkorsvikning med bevarade sidoeffekter, viss lokal konstantpropagering,
små `inline`/`always_inline`, immediater och direkta jämförelsegrenar.
`x64_finish` kör i dag bara `x64_twoaddr` före utskrift; registret väljs
fysiskt vid AST-sänkning. Inget av förslagen kräver att byta det upplägget.

Prioritet: 1–5 först, sedan lokalt registercache; målfeatures och explicit
SIMD som separata steg. LICM, automatisk vektorisering, global GVN och
aggressiv inlining hör inte till denna billiga första omgång. SSA är inte
ett teoretiskt krav för dem heller, men de behöver mer analys och budget.
Mät varje implementerat steg med instruktioner för kompilatorn och
körtid/kodstorlek för resultatet; inga prestandasiffror antas av denna audit.

Referenser för terminologi och jämförelse:
[LLVM passes](https://llvm.org/docs/Passes.html),
[GCC optimeringsflaggor](https://gcc.gnu.org/onlinedocs/gcc/Optimize-Options.html).

### X3 (2026-11-11): utökade Linux-körningar

Den kombinerade arbetskopian klarar 1807/1815 vendrade körtest på x86
(134 filtrerade); kvar är åtta GCC-optimeringsfall. Egna tester går
100/100. ARM/QEMU går 1822/1822, egna tester 105/105. Nonlocal jumps,
FP-registerspill, breda vektorlaner, ramdjup och 128-bitars overflowresultat
för högst 64-bitars operander har isolerade unity/nonunity-byggen och
riktade tester på båda målen. GCC:s ABI-generator har ett återstående
x86-fel av 777 clang-giltiga fall; LLVM:s generator går 1000/1000.
Detta flyttar X3 men är inte ett godkännande av återstående steg.

### X3 (2026-11-11): x87-operander i inline asm

- `t`/`u`-operander läggs på x87-stacken efter att uttrycken har evaluerats;
  utdata lagras med sin rätta bredd och kvarvarande indata poppas. `st(1)`
  i clobberlistan beskriver den pop som själva asm-mallen gör. Blandade
  x87/GPR-operander och andra x87-constraints stöds ännu inte.
- Matchande GPR-operander får olika bredder (t.ex. 64-bitars indata och
  32-bitars utdata i samma register), som clang.
- GCC `990413-2`, `20030222-1` och `stkalign` passerar x86. De två senare
  passerar även arm64/QEMU. `asm_x86_x87` kör sqrt för float/double/long
  double, fpatan, fsincos, read/write och trunkering, 32 gånger för att
  fånga läckta x87-stackposter. Separat kompilering av ändrade C-filer grön.

### X3 (2026-11-11): överjusterad stack

- Lokala objekt med alignment över 16 får en dynamiskt justerad rambas i
  `r12`; `rbp` behåller inkommande argument och returadressen. VLA:er får
  separat utfyllnad och en justerad objektpekare. Utgående stackargument
  justeras efter argumentens största alignment och stackpekaren återställs
  efter anropet.
- `stack_overalign`: fasta objekt (64/128/256), VLA (256), rekursion,
  stackargument, variadiska argument och 64-justerad struktur som värde
  passerar på x86-64 och arm64/QEMU. Korsanrop clang/vcc i båda riktningar
  passerar på båda målen (`tests/abi/stack_overalign`). cproc:s 12
  alignmentfall passerar kompilering på båda målen. `vla_zero` passerar också:
  nollningsslingans byteräknare rundas upp till hela 8-byteord.
- Inline asm som explicit använder `r12` i en överjusterad funktion ger
  tills vidare en diagnos: registret används som rambas.

### X3 (2026-11-11): GNU-villkor och debug-assembler
GNU `a ?: b` omvandlar det sparade värdet till bool för grenen, också
när a är float/double/long double. DWARF använder portabla
`.long`/`.short`/`.quad` i stället för arm64-specifika direktiv.
Regressionsfallen kör på x86; GCC `20121107-1.c` och `pr41679.c`
assembleras, och `llvm-dwarfdump --verify` godtar debug-objektet.

### X6 (2026-11-11): tomma compiler barriers
Tom inline asm utan operander, med `memory`/`cc`-clobber, bevaras som
en barrier-post utan maskininstruktion. Andra asm-former återstår.
GCC IEEE ger nu 49 godkända, inga fel, 30 filtrerade på x86 Linux,
samma som arm64/QEMU. Regression: `asm_barrier.c`.

### X3 (2026-11-11): flyttalsinbyggda
Vandraren sänker nu ordnade/oordnade jämförelseinbyggda för float,
double och x87 long double, och konstantfoldade NaN/infinity-värden.
Runtime-klassning (`isnan`, `isinf`, `isfinite`, `isnormal`) använder
bitmönstren utan flyttalsundantag. GCC IEEE: 48 godkända, ett fel
(tom inline asm med memory-clobber), 30 filtrerade. Regressionerna
`fp_compare_builtin.c` och `fp_classify_builtin.c` passerar på x86;
fil-för-fil-bygget passerar också.

### X3 (2026-11-11): void-lvärden
GCC:s `compile/20000701-1.c` kraschade när ett void-lvärde gav en
minnesläsning med storlek noll. Vandraren utvärderar nu adressen och
frigör dess register utan att läsa minnet, även för `volatile void`.
`tests/vcc/void_lvalue.c` kontrollerar också sid-effekter i adresser och
villkorsuttryck. x86:s egna tester: 47/56 godkända, mot 46/56 före
ändringen; de nio tidigare funktionsluckorna återstår.

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
  justerad rsp); bit-fält över en 8-bytegräns (delat vid gränsen); float-
  spill som för heltal; `__float128` (via adress, ett xmm i ABI:t, libgcc:s
  `__addtf3` ..., `__*tf2`, `__extend*`/`__trunc*` även x87, `__float*tf`/`__fix*tf*`);
  inbyggda i vandraren för alla CPU:er: clrsb, bitreverse, isnan/isinf/isfinite/
  isnormal/isinf_sign/fpclassify på bitmönstret (alla flyttalstyper), isgreater ...
  isunordered, inf/nan/huge_val, atomics nand/max/min, test_and_set, clear;
  inlining i vandraren (`always_inline` och små `inline`, argument parkerade
  före bindning, etiketter och case per kopia, förskanning per kopia),
  konstanta lokaler och parametrar, `__builtin_constant_p` efter inlining;
  `-mgeneral-regs-only`/`-mno-sse`/`-mno-sse2`/`-mno-mmx` (makron som clang, inga
  vektorregister, flyttal ett fel; `vcc-x86.py --fpcheck`); UTF-8-namn citerade;
  inline asm för `-S` (X6:s del i emittern: AT&T, begränsningar, modifierare, x87
  `t`/`u`, `asm goto`); `__builtin_setjmp`; död kod efter hopp och i konstanta
  `if` som clang -O2; vektorer av `__int128`/`long double`.
- `bench/vcc-x86.py`: 1768/1776, lika i `--pie` och `--pic` (vcc-sidan
  `-fPIE`/`-fPIC`, länkad `-pie`). Kvar: körfelet `20030125-1` (clangs `floor` -> `floorf`) och 7
  länkfel där clang -O2 tar bort ett `link_error`-anrop (X4:s pass). `vcc-abi.py --target x86_64`: 12/12, med tre nya tester
  (`int_records`, `fp_records`, `complex_args`).
- Kvar i X3: bit-fält bredare än 64 bitar, 128-bitars overflow-inbyggda med 128-bitars operander, `need_fold` (ett
  error()-anrop kvar efter inlining), `VCC_WALKLOG`.

#### Historisk status i den lokala grenen före integreringen

>   scopes via ett träd från förskanningen, `for`-satsens första led).
> - `bench/vcc-x86.py`: 1659/1770, lika i `--pie` och `--pic` (vcc-sidan
>   `-fPIE`/`-fPIC`, länkad `-pie`). Körfel bara där clangs optimerare krävs
>   (`20030125-1`, `bcp-1`, `builtin-constant`) och 11 länkfel av samma slag eller
>   `__builtin_setjmp`. `vcc-abi.py --target x86_64`: 10/12, med tre nya tester
>   (`int_records`, `fp_records`, `complex_args`) som klarar alla fyra
>   kombinationerna; resten stannar vid alignment > 16 och bit-fält över 8 byte.
> - Kvar i X3: explicit `r12` i inline asm med överjusterad rambas, `__float128`-aritmetik (libgcc `__addtf3` ...), inlining av
>   `__always_inline` med `need_fold` och `constant_p` efter inlining,
>   `-mgeneral-regs-only`/`-mno-sse`, `VCC_WALKLOG`.

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

### 2026-11-11: funktionsmål och genererad x86-featuremodell

- CPU- och featureval (`-march`, `-m`/`-mno-`, `target("...")`) delar nu en
  statisk LLVM/Clang-23-genererad modell med beroenden och makron. `native`
  läser CPUID/XGETBV på en x86-värd; ingen LLVM/Clang behövs vid körning.
  Explicita negativa flaggor och ordningen mellan featureflaggor bevaras.
- Funktionsattribut sparas, deklarationscache nycklas med featureläget och
  inlining kräver kompatibelt mål. Inkompatibel `always_inline` ger fel vid
  kodgenerering. SIMD- och variadiska registerregler använder funktionsmålet.
- CPU-/featureacceptans betyder inte att alla instruktioner eller ABI-varianter
  är implementerade. YMM/ZMM-anrop saknas: vektorer större än 16 byte som
  argument/retur med AVX ger nu ett uttryckligt fel i stället för fel ABI,
  även vid direktanrop till en extern funktion med AVX-attribut.
- Validering: `bench/vcc-x86features.py` jämför fullständiga `-dM`-resultat
  med clang (CPU, featureordning och native; syntetiska CPUID/OS-tester ingår).
  `bench/vcc-x86target.py` jämför attribut/inlining med clang och kontrollerar
  ABI-felen. Båda körs i den vanliga premerge-grinden även på ARM-värd.
- ABI-spärren omfattar även struct/union med breda vektorer, inklusive
  nästlade fält och arrayomslag. Clangs sammanslagning av klasser styr spärren;
  stora heltalsrecord och record som fortfarande passerar i minnet tillåts.
  207 jämförelser mot Clangs LLVM-signaturer (SSE2/AVX/AVX512, globala flaggor,
  funktionsattribut och externa mål) samt 22 x86-ABI-fall passerar.

### 2026-11-11: granskning av nästlade mål och variadisk AVX-ABI
Inlining kontrollerar nu den aktiva inlinade anroparens featureläge, inte bara
funktionens yttersta mål. Åtta orakelfall och åtta verifierade cachevägar
(deklarationssnapshot respektive replay med DRCHECK) passerar.
Onamngivna variadiska breda vektorer/record går via stacken och va_arg tillåts;
namngivna YMM/ZMM-parametrar och returvärden är fortfarande spärrade.
X86-vektorers naturliga alignment är 32/64 byte även utan AVX, medan ARM64
behåller 16-bytegränsen. 42 variadiska kompilerings-/spärrfall, 18 korsanrop
med clang och 12 layoutjämförelser passerar i unity och filvis bygge.

### OPT3-integration med X5 (2026-11-11)

Registerlokaler använder samma csslot/CFI som asm. Privata asm-snapshots
bevarar aktuella hem på alla kanter; riktiga unwind-tester återvinner
anroparens callee-save-värden genom vanliga och justerade ramar (O1/O2).
Se OPT3:s PR-status i PLAN-fas2.md; full O2 och prestandagrind kvarstår.

### 2026-11-11: integration av funktionsmål, packad bool-ABI och OPT3

PR39:s funktionsmål behålls vid sammanslagning med `9d022c3` (PR38/PR40).
ABI-klassificeringen behåller både namngivna/onamngivna argument och packade
boolvektorers lane-transport; indirekta variadiska anrop använder vandrarens
information om fasta parametrar. Registerlokaler får fortsatt egna hem,
medan inlining behåller den aktiva funktionens featureläge.
SSE4:s blend-/strängposter beskriver nu sina implicita register och flaggor
för den gemensamma optimeraren. Regressionsprov kontrollerar alla fyra
strängopvarianter samt fasta boolvektorparametrar via funktionspekare.

Unity och filvis bygge: egna Linuxprov 176/176 x86 (fem referensskip),
181/181 ARM; ABI 32/32 på båda mål; diagnostik 220/220; sema 887/887 i
process- och batchläge. OPT-regler O0–O3, registerhem/setjmp, unwind/CFI,
ASan/UBSan-posteffekter och interrupt-proven passerar. Detta är en
korrekthetsintegration; ingen ny prestandavinst eller stängd releasegrind
påstås.

Utökade integrationsgrindar: 587/587 featuremakron och 124/124 SSE4-
acceptansfall i båda bygglägen; kernel-PP 881/881 i process och batch.
Wasm32/64 kör det asm-omdöpta matematikprovet 1/1 i båda bygglägen.
Den första Wasm-starten saknade Emscripten-konfiguration och nådde inte vcc;
omkörning med explicit SDK-konfiguration är grön. TSan klarar 44 kompileringar
på 11 käll-/målkombinationer med 1 respektive 6 emittertrådar, identisk
assembler och inga rapporter. Exakta kommandon och binärhashar sparas i
`work/results/pr39-merge-validation/`.
### OPT4 kopior och kanoniska unsigned-resultat (2026-11-11)

Separat copies.c använder delad native-kärna och kräver lokal
överskrivning innan en kopia kan försvinna; tvåadressvandringen tar även
hand om borttagning/debugrader. extensions.c bevisar från föregående
post när högbitar redan är noll. Inga nya fulla pass/allokeringar/SSA.
Riktade O0–O3-körningar, debug/unwind och sanitiserade kontrakt gröna;
prestandaprofil och dess begränsningar finns vid OPT4 i PLAN-fas2.md.

### OPT4 blad utan ram (2026-11-11)

`arch/x64/emit/frame.c` avgör nu om en verkligt ramfri bladfunktion kan
utelämna rbp-prolog/epilog. Beslutet följer den befintliga tvåadressvandringen:
inga nya fulla pass, allokeringar eller AST-vandringar. Noll frame-bytes och
ingen justering, canary eller anropsmarkering krävs först; effektbeskrivningen
av varje kvarvarande post måste sedan sakna rbp/rsp-beroenden och anrop.
Okänd effekt och rå asm stänger vägen. Stackargument, frame/return-address,
setjmp, alloca, callee-saves och justerade ramar behåller nuvarande prolog.
CFI använder den vanliga initiala CFA:n rsp+8; debugrader, endbr, patchbara
nop-inträden och retur-thunk lämnas kvar.

Drivern tolkar `-fomit-frame-pointer`/`-fno-omit-frame-pointer` i ordning.
Standard: tillåtet vid O1–O3, O0 behåller ramen; explicit omit tillåter också
O0 (verifierat mot clang 23). VCC_NOOPT behåller ramen. Policyn ligger före
`cc_opts.outfd`, så deklarationscachens befintliga optionsignatur tar med den.
Andra måls ramval ändras inte. Funktionsattributet optnone är sedan tidigare
ignorerat i parsern; denna leverans inför ingen ny attributsemantik.

Validering: `bench/vcc-x86-leaf-frame.py` klarar nivåer/flaggreordning,
blandade headercacher med och utan replay, negativa ramkrav, kod och körning,
patch-inträde, CFI och dwarfdump. En signal avbryter en verkligt anropsfri,
ramfri bladfunktion; libgcc återfinner både blad och anropare genom dess FDE.
CI kör den värdoberoende delen med `--no-run`. Egna x86-sviten: 215/215 med
sex redovisade referensundantag; ABI: 32/32, fyra clang/vcc-kombinationer.
Befintliga libgcc-prov för registerhem i vanliga/justerade ramar är gröna vid
O1/O2. Unity-bygge samt fristående syntaxkompilering av ändrade enheter gröna.

Avgränsad CPU-kostnad: kalla separata processer, Callgrind Ir, samma
`VCC_OPT='-O3 -march=x86-64-v3'`, mål x86-64 O2. Bas 781ca697:s kompilatorkällor
är hash-verifierat identiska med 54580f7 (enda ccs-skillnaden är en testfil).
Basbinär SHA256 `6f42919ea1d4eef11100e017f2857b72054e52161620b443a1ada705bab89cf7`,
kandidat `a01b54686639a58ffe897137851c8abc2fd6793be7533d1ccaa32923cca79672`.
`work/opt-profile/leaf-cost/report.json` innehåller kommandon, källhashar och diff;
`work/opt-profile/measure-leaf.py` återskapar de två 501-funktions-TU:erna.

| Korpus | Ir bas → ny | Skillnad | .text bas → ny |
|---|---:|---:|---:|
| Positiv: `int fN(void){return 151+N;}` | 10 587 676 → 10 569 032 | −0,176 % | 16 022 → 8 014 byte |
| Icke tillämplig: `unsigned fN(unsigned x){return x*37u+(x>>3);}` | 16 471 567 → 16 383 053 | −0,537 % | 24 039 → 24 039 byte |

Positiv källhash `eee9910c8737d006d756893be7d52f6bb2e15c36bb39dbf929247aa15d3c362d`;
kontroll `a939023c599b599a652f3a88ff6fb10aa6cbd2a5d40a1741cf02812063c67b9e`.
Kontrollens assembler är byte-identisk. Bytevinsten inkluderar funktionsjustering,
inte bara borttagna instruktioner. Dessa två små syntetiska TU:er mäter varken
programkörtid, helkorpus eller någon allmän kompilatorförbättring; G5/G7 är öppna.
Nästa separata kvalitetslucka är kallt använda parametrars vanliga stackhem:
den nuvarande direkta ABI-bindningen gäller bara lönsamma, utvalda registerhem.

PR50 integration 2026-11-11: uppdaterad mot 9b18f642 (main genom 4bb6d52
plus PR49). Interrupt-prolog/epilog och nya x86-funktionsmål bevaras;
ramfri policy gäller uttryckligen bara vanliga funktioner. Unity -j1,
bladens flagg-/cache-/hårdningsprov inklusive interrupt-undantaget,
riktig signal-unwind genom ett ramfritt blad, befintlig vanlig/64-alignad
unwind O1/O2 samt debugrader O0–O3 passerar. CI för integrationshuvudet återstår.
