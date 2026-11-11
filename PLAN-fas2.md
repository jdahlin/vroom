# Fas 2: egen verktygskedja, kallt kärnbygge < 2 s

**Mål:** `make ARCH=arm64 CC=vcc AS=vcc LD=vld …` bygger `Image` från tomt O= på under 2 s
väggtid (M1 Pro, 10 kärnor), med samma config som i fas 1. Kärnan bootar Debian 13 till
`is-system-running = running`.

**Regler:**
- Ingen del beror på clang/LLVM eller binutils.
- make/Kbuild behålls. Stoakes kbuild-speedup v5 är applicerad.
- Varje del ska ha state-of-the-art-prestanda:
  - SIMD
  - parallellism
  - minimal allokering
  - mmap i stället för read

## Utgångsläge (mätt 2026-11-07)

| | |
|---|---|
| Fas 1-slutläge | 23,5 s (clang -O0 via vroomd + PCH), ~29–32 s när maskinen används till annat |
| Stoakes v5 | ingen mätbar skillnad: inga moduler, ingen objtool på arm64, ingen rust |
| TU:er | ~840 C + ~70 `.S` |
| Headers per TU | ~880 filer, 4–9 MB källtext |
| Läst totalt om varje TU preprocessas | 3,4–8 GB |
| Unika filer / bytes | 3 114 / 44 MB |
| Före första CC | ~6–7 s (Kconfig `cc-option`-tester, syncconfig, asm-offsets) |

Budgeten för 2 s med 10 kärnor är ~20 kärnsekunder totalt, alltså **≤ 15 ms kärntid
per TU** inklusive headers. Det kräver att headerarbetet delas: de 44 MB unik text
får bearbetas ungefär en gång, inte 840 gånger.

## Arkitektur

```
make ──► vcc (shim, ~100 µs) ──unix-socket+SCM_RIGHTS──► vccd (server, en process, trådpool)
             │                                              │
             └── as/ld/objcopy/nm-shims likadant            ├── header-cache: tokens + makron + deklarationer
                                                            ├── TU-jobb: lex → pp → parse → sema → emit (arm64)
                                                            ├── inbyggd assembler (.S och inline asm)
                                                            └── inbyggd länkare: objekt i minnet → vmlinux → Image
```

### 1. Shims (`vcc`, `vas`, `vld`, `vobjcopy`, `vnm`, `var`)
- Statiska binärer på ~100 rader C, utan libc-init-overhead (`-nostartfiles` där det går).
  De skickar argv, cwd, env-delta och fd 0–2 via SCM_RIGHTS och väntar på status.
- `cc-option`/`as-option`/`ld-option`-tester besvaras av servern från en tabell över
  vad den stöder. Ingen kompilering behövs, så de ~6 s före första CC försvinner.
- `-E`, `-S`, `-M*`, `-Wp,-MMD` stöds, eftersom Kbuild använder dem.

### 2. Lexer (SIMD)
- Källfiler mmap:as en gång per bygge (read-only, `MAP_PRIVATE`) och mappningen delas
  av alla trådar.
- **Klassificering med NEON:** 16–64 byte per iteration klassas i ident, siffra,
  blanksteg, `"`, `'`, `/`, `\`, `#` och newline via tabelluppslag (`vqtbl1q_u8`) och
  bitmasker. Tokengränser hittas med `ctz` på masker. Samma teknik som simdjson.
- Fortsättningsrader (`\`+newline) och trigrafer hanteras på en långsam väg, bara där
  masken visar `\`.
- **Tokens:** 8–16 byte vardera, med kind, flags, offset och längd. De skrivs till en
  arena per fil. Identifierare internas direkt med en hash (CRC32-instruktionen) och
  en lockfri öppen-adresserad tabell som delas mellan trådar.
- **Tokenisering en gång:** varje unik fil tokeniseras en gång per bygge och resultatet
  är oföränderligt efteråt. 44 MB ≈ 10 M tokens, och med ~1 GB/s per kärna tar det
  ~5 ms på 10 kärnor.

### 3. Preprocessor
Underlag och mätningar finns i `RESEARCH-preprocessor.md` (2026-11-07). Klang lägger
61 % av en -O2-kompilering på `-E`. Bygget läser 5,6 GB källtext, där varje fil
förprocessas 121 gånger.
- Arbetar på tokenströmmar, inte text.
- **Expansion enligt klangs modell, inte Prosser-hide-sets:** en "avstängd"-bit per makro
  och en paint-bit per token. Det ger exakt klangparitet utan allokering. Kontexterna
  pekar in i oföränderliga tokenarrayer, och bara funktionsmakron skriver till en
  tråd-arena. Snabbväg för objektmakron med 0 eller 1 token. `#define` kompileras till en
  opcode-mall, och argument förexpanderas lat, en gång per argument.
- **Per fil, en gång och delat:** direktivskelett med `#if`/`#else`/`#endif`-länkar, så
  att ett falskt block blir ett hopp. Include-guard-makrot lagras i tokencachen.
- **Include-uppslag utan syscalls:** katalogcache (`getattrlistbulk`) plus
  (sökväg-id, namn) → fil. I/O-trådarna förläser mål så snart de är kända.
- **Makrotabell per TU indexerad med identifierar-id:** tvånivå-array med 16 KB-sidor
  och copy-on-write mot delade ögonblicksbilder. Uppslag kostar två laddningar och ingen
  hashning.
- **Platser lata och glesa:** en post per expansion, och radnummer räknas bara vid behov.
- Pull-modell mot parsern.
- `__has_include`, `__has_attribute`, `__has_builtin`, `_Pragma`, `__COUNTER__`,
  `push_macro`/`pop_macro`, `#include_next`.

### 4. Header-cache (det viktigaste för 2 s)
- Nästan alla TU:er inleds med samma `-include`-filer och samma stora headers
  (`linux/kernel.h`, `mm.h`, `sched.h`, …) i samma makrokontext. Utöver `KBUILD_*`
  skiljer sig makrokontexten bara i små detaljer.
- **Prefix-delning, dynamiskt och utan PCH-fil:** TU:er grupperas på identisk
  flaggsignatur (`-D`/`-I`/`-include` utom `KBUILD_*`). Första TU:n i en grupp bygger
  tillståndet inkrementellt. Varje `#include` på toppnivå utan föregående
  kodberoende bildar en nod i ett **prefixträd**: tillståndet efter `#include <a>`,
  sedan `<b>`, … Följande TU:er går ner i trädet så långt deras include-sekvens matchar
  och fortsätter därifrån.
- Ett tillstånd består av makrotabell, deklarationer (symboltabell, typer) och
  emitterad kod för inline-funktioner. Det är oföränderligt och delas mellan trådar.
- **Läsmängd per region** (efter Bothner 2003 och GCC:s `cpp_valid_state`, men exakt).
  Varje #include-region på toppnivå registrerar:
  - lästa makron med värde, inklusive "odefinierat"
  - define/undef-deltat
  - `__has_include`-svaren
  - `__COUNTER__`-deltat
  - pragmatillståndet

  Regioner som använder `__FILE__`/`__LINE__`/`__INCLUDE_LEVEL__` tar dem som
  indata eller cachas inte. Ingen känd kompilator delar sådant tillstånd mellan trådar
  i en process, så det här är ny design: publicera en gång, aldrig frigöra, inga refcounts.
- Om makron som `KBUILD_MODNAME` används under prefixet registreras det som ett
  beroende på noden. Bara de noderna blir då specifika per TU, och korrektheten
  bevaras utan `-fno-validate-pch`-trick.

### 5. Parser + sema (resolver)
- **Parser:** handskriven rekursiv descent för C11 + GNU C, i den omfattning kärnan
  använder. Det omfattar:
  - statement expressions, `typeof`/`__auto_type`, attribut,
  - `asm`/`asm goto`, designated initializers, nested functions (används inte; avvisas),
  - `__builtin_*`, `_Generic`, `__int128` och vektortyper i liten omfattning.
- **AST:** platt och indexbaserat, med noder i arenor och 32-bitars index i stället
  för pekare. Typer internas, så att två lika typer är samma index.
- **Sema i samma pass som parsningen:** konstantuttryck utvärderas direkt, eftersom
  `BUILD_BUG_ON`, `__builtin_constant_p` och `sizeof` i `#if`-nära kod används hela tiden.
- **Inlining och konstantvikning för `__always_inline`** och för konstanta argument,
  vilket kärnan kräver (fas 1 visade `"i"`-operander). Det är ingen "optimering" utan
  semantik som kärnan förutsätter. -O0-patchen från fas 1 blir därmed onödig.
- Död kod bakom konstanta villkor emitteras inte. Det ersätter `vroom-ld`-stubbarna.
- Funktioner kompileras lat: `static inline` i headers emitteras bara om de används i TU:n.

### 6. Emitter (arm64)
- Direkt från AST till maskinkod, utan IR, i stil med tcc men med en bättre
  stackallokering. Alla lokala variabler ligger i stackslotar, med en enkel
  registercache för uttrycksträd.
- Relokeringar och sektioner (`.text.*`, `.init.text`, `__jump_table`,
  `.altinstructions`, `__ex_table`, `__bug_table`) skrivs direkt till minnesobjekt.
- **Debuginfo:** ingen. Kärnconfigen har DEBUG_INFO avslaget.

### 7. Assembler (`.S` och inline asm)
- GNU as-syntax för arm64, så långt kärnan använder den:
  - `.macro`/`.irp`/`.rept`/`.if`, `.pushsection`/`.popsection`, `.altmacro` (sällan)
  - lokala numeriska labels (`1b`/`1f`), uttryck med symboler
  - alla A64-instruktioner som förekommer, inklusive system-register (`msr`/`mrs` med
    `sysreg-defs.h`)
- `.S` går genom samma preprocessor och sedan direkt till assemblern i minnet.
- Inline asm: operander substitueras (`%0`, `%[name]`, `%c`, `%l`, `%w`, `%x`), och sedan
  går texten genom samma assembler.

### 8. Länkare + objcopy
- Kbuild kör `ar` för `built-in.a`/`vmlinux.a` och sedan `ld` med `vmlinux.lds`.
  Inom bygget skickar shimsen bara referenser till objekt som finns i serverns minne;
  ELF-filer skrivs till disk asynkront och bara när någon läser dem (nm, modpost).
- **Linker-skript:** en tolk för den delmängd som `vmlinux.lds` använder:
  - `SECTIONS`, `KEEP`, `SORT`, `PROVIDE`, `ASSERT`, `ALIGN`
  - adressuttryck, `AT()` och `/DISCARD/`
- **Parallell layout per output-sektion.** Relokeringar appliceras parallellt per
  input-sektion, och `Image` skrivs direkt (objcopy -O binary).
- modpost behöver ELF på disk för `vmlinux.o`. Den skrivs med en enda `pwritev`
  från minnesbuffertar.

### 9. Minne och parallellism, genomgående
- **Trådmodell: CPU-trådar och I/O-trådar är skilda.**
  - **CPU-pool:** en tråd per kärna, fastlåst via QoS-klass på macOS (P-kärnor för
    kritiska vägen: header-cache och länkning, E-kärnor för fristående TU:er), med
    work-stealing (Chase–Lev-deque per tråd). Ingen CPU-tråd blockerar någonsin på I/O.
  - **I/O-trådar, 2–4 st:**
    - Förläsning: mmap plus `madvise(MADV_WILLNEED)` och pre-touch av sidor för
      headers som snart behövs, styrt av prefixträdet.
    - Asynkron skrivning av `.o`, `.cmd`/`.d` och `vmlinux`: `pwritev` från färdiga buffertar.
    - På Linux senare io_uring: en ring per I/O-tråd, med registrerade buffertar.
  - **Shim-socket:** en accept- och dispatch-tråd med kqueue (epoll på Linux) som lägger
    jobb i CPU-poolen och svarar när jobbet är klart.
  - make `-jN` levererar jobb, och servern prioriterar själv.
- **Arenor:** per tråd och per TU, frigörs i ett svep. Inga `malloc`/`free` i heta loopar.
  Stora buffertar tas med `mmap` och `MAP_ANON`, och sidor återanvänds mellan TU:er
  (`madvise(MADV_FREE)`).
- **Delad, oföränderlig data:** filmappningar, tokenströmmar, internerade strängar och
  header-cache-noder. Ingen låsning efter publicering.
- **Profilering från dag 1:** Instruments/`perf`-motsvarighet med dtrace och
  `os_signpost`, plus interna räknare (ns per fas och TU). Varje steg nedan mäts innan
  nästa påbörjas.

## Steg

| # | Leverans | Kriterium |
|---|----------|-----------|
| 2.1 | `vcc`-shim + `vccd`-skelett som vidarebefordrar till clang, och svarar på `cc-option`-tester från tabell | Kconfig-fasen ~6 s → < 0,3 s, bygget fortfarande grönt |
| | **2.1a klar** (`patches/0002-kconfig-parallel-shell.patch`): spekulativ parallell `$(shell)` i Kconfig, byte-identisk config. conf 4,1 s → 1,0 s; första CC 7,2 s → 4,3 s. Resten är processkostnad (97 `cc-probe` à ~35 ms, 7 `as-instr` à ~156 ms via riktig clang `-cc1as`) och försvinner när vcc/vas svarar själva | |
| | **2.1b klar** (`ccs/vcc/`): `vcc`/`vld`/`vas`-shim (en binär, ingen stdio/malloc) + `vccd` (kqueue-accept, trådpool, mmap-buffertar per tråd). Prober besvaras från kapabilitetstabellen `caps.c` på ~0,05 ms (hela anropet 3,2 ms ≈ processgolvet 2,3 ms), resten vidarebefordras till vroomd/vroom-ld. Tabellen är minimal: inga sanitizers, stack-protector, PAC/BTI, SCS, auto-var-init; varningsgrupper ⊆ clang (`gen-warnings.sh`) så länge clang är backend. Första CC 4,3 s → 3,4 s, kallt bygge 22,2 s, bootar till `running` | |
| | **2.1c profil före första CC** (dash som `SHELL`/`CONFIG_SHELL`, `patches/0003`): ~3,5 s på belastad maskin. Kritiska vägen är seriell: make-parse + SYNC ~0,5 s → fixdep ~0,3 s → conf-objekt + bison + ld64 ~0,4 s → **conf ~0,7–1,3 s** (≈240 skalkommandon, ~700 processer à 2–3 ms; nära processgolvet) → dtc/kallsyms/sorttable ~0,6 s (Kbuild låter `archprepare` vänta på hela `scripts`) → bounds.s. Hostverktyg med -O0 gav ingen vinst (conf blev långsammare). **Slutsats:** med make + Kconfig + hostverktyg byggda av clang/ld64 ligger golvet runt 1,5–2 s före första CC, så < 2 s totalt kräver (a) att vcc även kan bygga hostverktygen (Apple Silicon är AArch64: Mach-O-utdata + libSystem-ABI i samma emitter), (b) att `archprepare` bara väntar på `scripts_basic`, (c) färre processer per probe (`cc-probe` utan mkdir/rm) | |
| | **2.1d klar** (b) och (c) från 2.1c, plus prober som låg på kritiska vägen. Kärnpatchar `patches/0004`–`0007` (gren `vroom-pf` i `work/linux-pf`, ännu inte i `work/linux`): (1) `archprepare` väntar bara på `scripts_basic`; nedstigningen i trädet och `modules_prepare` väntar på `scripts` (GCC-plugins/recordmcount/Rust: som förut); arm64:s kapi-headers (`cpucap-defs.h` m.fl.) genereras som förutsättning till `archprepare` i stället för i dess recept, parallellt med asm-generic (0007). (2) `filechk` hoppar över `mkdir -p` och `rm -f` när de inte behövs (4 → 2 processer per genererad header); config-instansen expanderar `CC_VERSION_TEXT`/`RUSTC_VERSION_TEXT`/`PAHOLE_VERSION` en gång i stället för vid varje kommando (~20 × `vcc --version \| head` + pahole + rustc). (3) Kconfig skapar en scratch-katalog (`$KCONFIG_TMPDIR`, tas bort vid exit) och `cc-probe` gör `-Wno-`-omskrivningen med parameterexpansion: 7 → 2 processer per `cc-option`, ingen nästlad `$(shell)`. syncconfig: 241 → 143 kommandon, 9–11 → 4–6 rundor. vccd/caps.c: `as-instr` och `echo … \| $(CC) -x c -` slås upp i en tabell med clangs svar (okänd källa kompileras på riktigt med samma stdin); länkprov (`cc-can-link.sh`) → nej; `--version`, `-print-file-name=`, `vld --version` ur caps.c; vld vägrar `--pack-dyn-relocs=`/`-z pack-relative-relocs` direkt (relr-skriptet 187 → 32 ms). `.config` byte-identisk och alla 1 387 `.cmd`-filer identiska mot A. **A/B** (alternerande, samma privata vroomd/vccd, `VCC_PP=0` p.g.a. -E-regressionen i main): första CC från make-start 3,5/3,5/3,4 s → 2,3/2,3/2,3 s (0004–0006, last 4 → 50) och 3,5/3,5/3,9 → **2,2/2,2/2,3 s** (med 0007, last 19 → 58); kallt bygge 22,4/23,5/23,9 → 21,2/22,5/23,4 s resp. 24,4/23,8/24,5 → 22,7/24,4/23,5 s (kompileringsfasen dominerar och är brusig under last); bootar till `running`, 0 fel. conf själv ~0,4 s tyst (yyparse inkl. `$(shell)` 0,23 s, `menu_finalize` 0,17 s ren CPU i `expr_transform`). Shimmen ligger på golvet: `vcc --version` 3,4–4,1 ms = ett tomt `int main` (3,5–3,9 ms) mot `/usr/bin/true` 2,6 ms (systembinärer startar snabbare); vccd-rundresan ~0,1 ms. **Kvar före första CC (~2,3 s):** fyra parsningar av toppnivåns Makefile (yttre+inre ~0,2 s, config-instansen ~0,2 s, omstarten efter `auto.conf`) med ~50 processer var, varav try-run-prober à 4 processer; fixdep 0,2 s och conf-objekt 0,3 s (vroom-cc + ld64); `menu_finalize`; `cc-version.sh` (`-E` av stdin) | |
| 2.2 | SIMD-lexer + tokencache | lexar alla 3 114 kärnfiler; > 1 GB/s/kärna; tokens identiska med clang `-dump-tokens` |
| 2.3 | Preprocessor | `vcc -E` ger samma tokenström som `clang -E` för alla 840 TU:er (normaliserad jämförelse) |
| 2.4 | Header-cache/prefixträd | `-E` för alla TU:er < 0,5 s väggtid totalt |
| 2.5 | Parser + sema | `-fsyntax-only` för alla TU:er utan fel; konstantuttryck = clang |
| 2.6 | Emitter arm64 | kompilerar alla TU:er; per-fil-diff av symboler och sektioner mot clang -O0; kärnan bootar med vcc-objekt + lld |
| 2.7 | Assembler | alla `.S` och all inline asm; byte-identiskt mot clang `-cc1as` för `.S` |
| | **`.S` klar**: 30/30 byte-identiska, även via `vpp` (se Status 2.7); inline asm kvar | |
| 2.8 | Länkare + objcopy | `vmlinux` och `Image` utan lld; bootar |
| | **2.8 klar** (vdso-länken i 2.12): `vlink` byte-identisk med lld (30 ms mot 340 ms), `vobjcopy`/`vnm` identiska med llvm-objcopy/llvm-nm, Image bootar | |
| 2.9 | Allt i minnet, profilering, finjustering | kallt bygge < 2 s, bootar Debian till `running` |
| | **vccd kompilerar själv** (se Status 2.9): alla 877 `-c` av kärnans `.c`/`.S` i vccd, 0 till clang; objekt direkt från emittern utan asm-text; kompileringsfasen 16,7 → 11,7 s | |

### Status 2.2 (2026-11-07)
- `ccs/vcc/lex.c`, `intern.c`: strukturell lexer i simdjson-stil. 9 masker per 64-byteblock
  (vld4q + tbl4), alla tokenstarter i ett 64-bitarsfönster räknas fram samtidigt, och
  token-kind väljs grenfritt. Literaler, kommentarer, splices och UCN går via computed goto.
  Interning görs efter tokenloopen: en trådlokal cache med 2048 poster och en låsfri global tabell.
- Korrekthet (`ccs/vcc/lexdiff.py` mot `clang -cc1 -dump-raw-tokens`, gnu11), identiskt på:
  - 3 225/3 225 byggfiler (6,06 M tokens)
  - 5 489/5 489 slumpade trädfiler + fuzz (11,2 M tokens)
- Prestanda på en P-kärna (`lextest core`, 45 MB):
  - **0,99 GB/s** utan interning (45 ms)
  - **0,64 GB/s** med interning (70 ms)
  - Målet > 1 GB/s med interning är **inte nått**. Interning kostar ~10 ns per
    identifierare (2,5 M st). Prefetch och batchade missar i den globala tabellen
    blev långsammare.
- `lextest bench -j 8` (mmap + lexning genom tokencachen): 30 ms väggtid för alla
  3 225 filer med varm sidcache.
- PMU-mätning (`ccs/vcc/pmc.c`, `sudo ccs/vcc/lexpmc.sh`, samma räknare som Instruments
  CPU Counters). Interning lägger till per pass:
  - +91 M cykler
  - +160 M instruktioner (IPC 2,2)
  - +1,73 M L1D-läsmissar
  - +655 k felpredikteringar

  Själva lokala cachen kostar ~8 ms. Resten är ~520 k uppslag i den globala tabellen
  à ~40 ns. De går ut i SLC/DRAM, eftersom 45 MB källtext per pass tränger ut allt
  utom L1.
- Prövat och förkastat (A/B, omväxlande körningar):
  - större lokal cache, både 1-nivå och 2-nivå (L2-storlek)
  - 2-vägs associativ cache (felpredikteringar)
  - 48-byte NEON-nycklar
  - nycklar inline i den globala tabellens slots
  - tvåfasig batch med prefetch, med och utan "pending"-markörer
  - mjukvarupipelinad prefetch D=8

  Alla blev 3–15 % långsammare. Behållet: globala entries med de första 16 byten
  inline, 2^20 slots, `always_inline`.
- `lextest idcheck`: varje identifierares id stavar identifieraren (2,8 M, 0 fel).
- Tokencachens verkliga väg (`lextest bench -j 8 [-r R]`: open + read + lex + kall
  interning) kostar ~250 ms CPU per pass mot 70 ms för ren lexning. Över hälften är
  kärntid:
  - `open()` kostar 12–20 µs/fil på 1 tråd och ~43 µs på 8 trådar, dvs. ~145 ms CPU
    per pass. Som jämförelse tar `stat` 1,7 µs.
  - read, fstat och close är försumbara.
  - Sidfel kostar 1,1 µs/sida på 1 tråd och 2,7 µs på 8.

  Ändrat: källan läses med `read()` in i arenan i stället för mmap. Det ger 20 % färre
  sidfel och tar bort den dubbla mappningen för filer utan slack. Prövat: `mlock` för
  att förfaulta (sämre) och `madvise` (ingen effekt).
- `open()`-kostnaden är verklig: samma siffror från en vanlig terminal. Den är
  oberoende av flaggor (O_EVTONLY, O_NONBLOCK, O_NOFOLLOW, openat). Den beror inte på
  `com.apple.provenance` (SDK-headers utan xattr är lika dyra), inte på FileVault /
  per-fil-nycklar (den förseglade systemvolymen är lika dyr), och inte på sandlådan.
  Genomströmningen planar ut vid ~3 trådar, ~6 µs vägg per open, alltså serialiserat i
  kärnan. Golvet för 3 225 filer är ~20–30 ms vägg.
- Orsak, enligt XNU-källan (open1 → vn_open_auth, fp_close_and_unlock). Open + close
  gör långt mer än stat:
  - MAC-hookar: `mac_vnode_check_open`, `mac_vnode_notify_open` och
    `mac_file_notify_close`, över alla policy-kextar
  - APFS `VNOP_OPEN`, `VNOP_CLOSE` och `VNOP_INACTIVE` (stängd källkod)
  - det globala `vm_object_cached_lock` två gånger per cykel, eftersom usecount går
    0→1→0
  - `proc_fdlock` 3–4 gånger och atomiska räknare (`nfiles`, kreditiv-refcount)

  N processer planar ut på samma ~19 ms som N trådar, så flaskhalsen är systemglobal
  och inte per process. Liknande fynd: Szorc, globala lås i APFS (2018); Arko, APFS
  planar ut vid ~4 kärnor (2025). Enda botemedlet är färre open().
- Read-ahead: `vlex_prefetch()` köar filer till 3 I/O-trådar (`VLEX_IO_THREADS`) som
  gör open + read. `vlex_get()` lexar bara, eller tar själv över en fil som ännu inte
  lästs. Lexertrådarnas CPU sjunker från ~250 till ~120–160 ms per pass. Väggtiden
  ligger på ~35 ms, nära I/O-golvet. Verifierat:
  - `idcheck` med och utan prefetch
  - lexdiff 3 225 + 5 489 filer
  - fil som saknas

Varje steg verifieras differentiellt mot clang/lld, som bara används som referens och
aldrig i bygget. Ett TU-test (`bench/vcc-diff.py`) körs över alla kärnfiler.

### Status 2.3 (2026-11-07): klar
- `ccs/vcc/pp.c` (+ `pp.h`): pull-modell (`pp_next`), kontextstack utan kopiering (filer och objektliknande makrokroppar pekar in i tokencachens arrayer), funktionsliknande expansioner i en per-TU-arena som nollställs när stacken är nere på filnivå. Expansionen är en port av clangs TokenLexer-logik: disable-bit per makro (bit 0 i makrotabellens post) + paint-bit, pre-expansion bara om argumentet nämner ett makro, pop av färdiga kontexter före `(`, GNU `, ## __VA_ARGS__`, whitespace-propagering (syns i `#x`), `__LINE__` = expansionens slut för ##-operander.
- Per fil (delat, byggs en gång, publiceras atomiskt i `vlex_file.pp`): direktivskelett med `#if`→`#elif`/`#else`/`#endif`-länkar (en falsk grupp = ett hopp, inga tokens rörs) och include-guard (`#ifndef X` / `#if !defined(X)`) → filen hoppas över utan att öppnas igen.
- Makrotabell indexerad med identifierar-id (tvånivå, 2048-postssidor), ingen hashning. Predefines + `__has_*`-svar tas från clang en gång (`gen-ppdata.sh` → `ppdata*.inc`), som caps.c; vcc kör aldrig clang.
- **Acceptans:** `bench/vcc-diff.py cmp --kernel`: **840/840 kärn-TU:er token-identiska med `clang -E -P`** (art + stavning, lexat med vcc:s lexer). Referenser: `bench/vcc-diff.py ref` → `work/ppref/`.
- Fart (`vpp`, en TU per process): kernel/fork.c 78 ms kallt / 29 ms med varm tokencache; clang -E 310 ms. async.c 49/15 ms mot 170 ms.
- Host-TU:er (`scripts/`): Darwin-predefines, clangs resource-include + SDK `usr/include`, framework-kataloger (`<A/b.h>` → `A.framework/Headers/b.h`), `__has_*`-tabell med två kolumner (linux, darwin) genererad över alla SDK-/kärnidentifierare.
- `__VA_OPT__` (inkl. placemarker vid `##`), `#line` (påverkar `__LINE__`/`__FILE__`), assembler-with-cpp (`.S`, `.lds.S`, `.dts`: okända direktiv och `# N "fil"` passerar, `#` utan parameter, ogiltig paste lämnar båda tokens, `__ASSEMBLER__`), `-undef`.
- Katalogcache (`dircache.c`): ENOENT-open() kostar ~3 µs och serialiseras systemvitt; 94 % av missarna ligger i kataloger som inte finns, så cachen minns bara om en katalog finns (en stat per katalog, låsfri, delad mellan trådar). Listning med getattrlistbulk provades och var långsammare.
- `ppdriver.c`: clang-argv, predefines, `-E -P`-utskrift och `-MMD`/`-MD`-deps (clangs ordning, `./` strippas) bakom `ppd_run()`; används av `vpp` och av `vccd`, som nu kör `cc -E`-jobb själv (`VCC_PP=0` → clang). Fatala fel longjmp:ar, `pp_close` frigör allt.
- **Acceptans:** `bench/vcc-diff.py cmp -j 6`: **911/912 TU:er token-identiska** (840 C + 39 .S + 32 lds/dts i kärnan, + host). Undantaget, `scripts/mod/modpost.c`, behöver en genererad `module-offsets.h` som clang inte heller hittar. Via `vcc`-shimmen → `vccd`: vmlinux.lds, vdso.lds och empty_root.dtb identiska med clang inkl. dep-filer, 0,5–3,4 ms per jobb.
- Kvar → 2.4 (header-cache/prefix-snapshot) för att komma under 0,5 s totalt.
### Status 2.4 (2026-11-07): header-cache för `-E`, målet inte nått (0,77 s mot 0,5 s)
- Mätrigg: `vpp --batch JOBS -j N -C DIR [--cmp]` kör alla TU:er i en process med en tokencache, som vccd gör (`bench/vcc-diff.py batch [-j N] [--cmp]`). Utgångsläge utan cache: 912 TU:er på **4,7 s vägg / 28 s CPU** (`-j 6`).
- Delade makroobjekt: `#define` i headers cachas per direktivplats, main-filens och `<built-in>`-definitioner görs kanoniska på innehåll, och builtins är processglobala. Samma makro blir då samma pekare i alla TU:er, så makrovärden jämförs med en pekarjämförelse.
- Regioncache (bara `pp_text`, alltså `-E`): varje `#include` är en region, även nästlade (upp till 64 nivåer). Läsmängden är exakt:
  - makrovärde vid inträdet, eller bara "definierat" när enbart det lästes;
  - include-guards (kontrolleras först);
  - medlemskap i `#pragma once`;
  - identifierare som klistrats ihop med `__COUNTER__`;
  - `__INCLUDE_LEVEL__`;
  - hur många sökvägskataloger uppslagen berodde på (prefix-signatur).

  En post sparar define/undef-deltat, once-tillägg och filposter (för deps och positioner), `__COUNTER__`-delta och -positioner (värdena skrivs om vid uppspelning, även inne i strängar från `#`), samt texten som ett rep: egen text plus referenser till barnens poster. En uppspelning inne i en region som spelas in läggs in i den yttre läsmängden. Ej cachebart: diagnostik, `#line`, push/pop_macro, `__DATE__`/`__TIME__`/`__TIMESTAMP__`/`__BASE_FILE__`. Upp till 64 varianter per nyckel (fil, sökväg, katalog, sys-flagga, flaggsignatur). Allt publiceras låsfritt (CAS) i delat minne och frigörs aldrig.
- Småsaker från profilen: `-E`-bufferten och inspelningsbuffertarna återanvänds per tråd (ingen munmap och inga sidfel per TU), och `__COUNTER__`-siffror skrivs utan snprintf.
- **Fart (`-j 6`, delad maskin med load 5–7): 0,77 s vägg / 4,6 s CPU** (6× mindre CPU); `-j 10` ger 0,67 s; en tråd 3,5 s (utan cache ~22 s). 2,8 GB av 2,9 GB utdata spelas upp ur cachen (42k träffar, 12,5k inspelade missar).
- **Verifiering:**
  - `vcc-diff.py batch -j 6 --cmp`: 911/912.
  - `-E`-texten och dep-filerna är byte-identiska med `VPP_NOHC=1` för alla 911 TU:er.
  - `vcc-diff.py cmp -j 6`: 911/912.
  - `vcc-sema.py run`: 878/879.
- Varför 0,5 s inte nås: två tredjedelar av CPU-tiden går åt till missar. 9,4k av dem beror på include-guards, alltså att samma header inkluderas där ett barn redan är (eller ännu inte är) inkluderat. Varje miss bearbetar bara headerns egen text, eftersom barnen träffar, men det räcker för att dominera.
  - Provat: "guard-tolerant" träff, som hoppar över ett barn som TU:n redan har inkluderat. Det gav bara 376 träffar och togs bort.
  - Den dominerande riktningen är tvärtom: posten spelades in när barnet redan fanns, och den här TU:n saknar det. Den kräver att läsningarna ordnas relativt barnets plats (läsposition per indata) eller ett prefixträd per include-sekvens.
  - Snabbare bearbetning per token (pp-fartarbetet) minskar missarnas kostnad direkt.
- Miljövariabler: `VPP_NOHC=1` stänger av cachen, `VPP_HC_DEBUG=1` skriver orsak per miss, `VPP_HC_MAX_MB` sätter minnestaket (standard 8 GB).
- **Tokenläge (`pp_next`, alltså parsern):** samma regioner och läsmängder, men nyckeln skiljer på text och tokens.
  - Inspelningen tar de tokens som `pp_next` lämnar ut. Spellningar i per-TU-minne kopieras, platser lagras relativt regionens bas, och `__COUNTER__`-siffror sparas som (token, byteposition).
  - En träff lägger en `C_REPLAY`-kontext som lämnar ut tokens oexpanderade (målade). Barnens poster skarvas in, platser flyttas till TU:ns bas och räknarsiffror skrivs om (identifierare internas på nytt). `pp_next` har en snabbväg för replay-tokens.
  - **Verifiering:**
    - `--vcc-tokhash` hashar art, SOL/SPACE, plats, spellning, id och fillistan per TU: identisk med `VPP_NOHC=1` för alla 911 TU:er.
    - `vcc-sema.py run` 878/879, `layout` 878/879 (18,8 M kontroller).
    - `-E` är fortfarande byte-identisk, och deps likaså.
  - **Fart (`-j 6`, load 8–11):**
    - tokenströmmen för alla TU:er: 1,74 s / 10,4 s CPU mot 3,1 s / 18,5 s utan cache;
    - `-fsyntax-only` för 879 C-TU:er: 3,77 s / 22,6 s CPU mot 4,80 s / 28,7 s.

    541 M tokens spelas upp, med ~7–8 ns per token som begränsas av minnesbandbredden (24-bytes `ptok`). Parsern är nu den större delen, så deklarationsuppspelning (RESEARCH-preprocessor §6.4) är nästa stora vinst.
- Kvar:
  - deklarationer per region för cc-vägen. Första formen är klar (`ccs/vcc/ccsnap.c`, RESEARCH-preprocessor §6.7): parserns tillstånd efter TU:ns inledande top-level-headers sparas i ett trie per cache-post, och TU:er som börjar likadant återställer det i stället för att parsa.
    - 834 av 879 TU:er återställer, och 98 M tokens (16 %) hoppas över.
    - `-fsyntax-only` 2,96–3,25 s mot 3,33–3,81 s, och `-S` 3,13 s mot 3,51 s.
    - `-S`-utdata och layoutkontroller är identiska med och utan cachen (`VCC_NODC=1`) för alla TU:er.
    - Minnet är 256 MB (`VCC_DC_MAX_MB`).
    - Kvar är omlokaliserbar uppspelning per region på alla djup, som täcker resterande 84 %;
  - ~~läsordning per region mot guard-missarna~~: provat och lagt på hyllan (RESEARCH-preprocessor §6.6). Med splitsning blev det 6–8 % färre missar, men 4× långsammare `-E` och 101 TU:er med fel utdata;
  - ~~minnet: 2,8 GB lagrat vid `-j 6`~~ (2026-11-08): taket är fortfarande `VPP_HC_MAX_MB`, men en post lagrar nu mycket mindre:
    - Profil först (`VPP_HC_DEBUG=1` skriver bytes per slag, `VPP_HC_PUB=1` en rad per post): i `-E`-läget var 1,33 GB postdata, varav makrodelta 628 MB, läsningar 308 MB, egen text 233 MB och filposter 141 MB. Det gamla "lagrat"-talet räknade med andra trådars allokeringar (`shared_used`); nu räknas exakt (`hsalloc`).
    - Läsningar och delta upprepar barnens körningar i varje omslutande header. De delas nu i innehållsdefinierade bitar (snitt efter id, ~32 element, högst 256) som lagras en gång i en låsfri tabell; filnamn, egen `-E`-text och tokenarrayer lagras också en gång (`hc_blob`).
    - Lagrade tokens är 12 byte (`htok`) i stället för 24 (`ptok`): identifierare som id, andra spellningar som 32-bitars offset till en delad blob, plats relativt posten, art/flaggor/16-bitars längd. Skarvade identifierare och långa spellningar går till en full `ptok` (HF_X).
    - **Mätt (`-j 6`, load 10–13), main mot nu:** `vcc-diff.py batch --cmp --kernel` 1,33 GB → 0,33 GB lagrat, max RSS 2,07 → 1,04 GB, 3,2 s vägg och ~19 s CPU båda. `-fsyntax-only`-batch (840 TU:er) 3,6 GB (racy tal; ~1,1 GB tokens) → 0,44 GB lagrat, max RSS 3,21 → 1,36 GB, vägg 1,39 → 1,48 s, CPU 8,3 → 8,9 s. Kostnaden ligger i publiceringen (hash och jämförelse av bitar); ren uppspelning (`--warm 1`) gick 2,98 → 2,54 s vägg vid lika många cykler.
    - Poster som träd (2026-11-08): en post lagrar bara sina egna läsningar, sina egna sista skrivningar (`HT_O`) och en lista med barnposter. Varje barn har de id:n föräldern skrev före barnet; barnets läsningar av dem är inte indata. Deltat tillämpas barn för barn och sedan det egna. Vid uppslag memoiseras ett barns utslag per `hc_try`, och räknarnamnen (`__COUNTER__`) kontrolleras sist. En variant över 64 per nyckel publiceras men listas inte, så att föräldern kan ha den som barn. Vid minnestaket blir de omslutande nivåerna ej cachebara. Lagrat `-E` 330 → 292 MB, tokens 430 → 361 MB, max RSS 735 → 691 MB respektive 1,35 → 1,27 GB; instruktioner 77,3 → 76,3 G, cykler lika inom bruset (load 6–9). Publiceringens bitar försvann nästan (`hc_vec` 193 → 14 samplingar), men nu kostar barnens validering och genomgången av barnets läsningar och skrivningar när det läggs till en förälder ungefär lika mycket.
    - Verifiering: `--vcc-tokhash` identisk med `VPP_NOHC=1` för alla C-TU:er, `vcc-diff.py cmp -j 6` 911/912, batch `--cmp` 873/873, `vcc-sema.py run` och `layout` 878/879, `fuzz-regress` grönt. TSan-körningen avbröts (för långsam på den delade maskinen) och är inte gjord.
  - vccd tar inte om filer och litar redan på att de är oföränderliga, och cachen ärver det.
### Status 2.5 (2026-11-07): parser + sema, första version klar
- 2.4 (header-cache) är medvetet uppskjuten: först hela kedjan, sedan fart.
- `ccs/vcc/cc.c` (+ `cc.h`): rekursiv descent för GNU C11 med sema i samma pass. Typer internas (qty = index<<3 | kvalificerare), poster/symboler/noder är 32-bitars index i per-TU-arrayer som reserveras med mmap. Varje uttryck får sin typ, implicita konverteringar blir N_CAST-noder, och konstantuttryck utvärderas vid behov med clangs skillnad mellan ICE och vikbart (nollpekarkonstanter, alltså `__is_constexpr`, beror på den).
- Postlayout är portad från clangs ItaniumRecordLayoutBuilder (AArch64: bitfältstypens alignment, nollbreda bitfält, packed/aligned per fält och post, `#pragma pack`). Enum-typer enligt clang: alla uppräknare är `int` om alla värden ryms, annars enumtypen.
- GNU/clang-utökningar som kärnan använder: statement expressions (även med label sist), `typeof`/`__typeof_unqual__`/`__auto_type`, `_Generic`, designerade initierare med intervall och brace elision, sammansatta litteraler, `asm goto` med operander, `__label__`, `&&label`/`goto *`, case-intervall, `?:` utan mitten, `-fms-anonymous-structs`, `-fshort-wchar`, `counted_by`, ~120 builtins (vikbara där clang viker). Värd-TU:er: nullability-kvalificerare, block-pekare i SDK-prototyper, implicit int i systemheaders (pp märker nu systemheaders).
- **Acceptans:** `bench/vcc-sema.py run`: **878/879 TU:er utan fel** (840 kärna + 38 värd; modpost.c saknar genererad header även för clang). `bench/vcc-sema.py layout`: vcc skriver `_Static_assert` för sizeof/_Alignof/offsetof för alla poster/enums/typedefs på filnivå, sizeof och typ (`__builtin_types_compatible_p`) för alla globala objekt och funktioner samt värde och typ för alla uppräknare; clang kontrollerar dem mot sin egen -E-utdata: **18,8 M kontroller, 878/879 TU:er gröna**. (clangs `-fdump-record-layouts-complete` gick inte att använda: den dumpar innan `} __packed;` har applicerats.)
- Fart: kernel/fork.c `-fsyntax-only` 84 ms inkl. pp (pp ensam ~75 ms), clang 464 ms.
- Kvar till 2.6: typerna inuti funktionskroppar är inte verifierade mot clang (kommer med objektjämförelsen), statiska initierare kontrolleras inte för konstanthet, VLA och deklarationer efter label är approximerade, `__builtin_constant_p` efter inlining (emitterns sak), inget K&R.
### Status 2.6 (2026-11-08): emitter, alla 840 kärn-TU:er stämmer mot clang, kärnan bootar Debian
- `ccs/vcc/emit.c` (+ `ccint.h`, cc.c:s interna strukturer utbrutna): arm64-kod direkt från AST:n, utan IR. Heltalsfolding medan koden genereras (varje uttryck blir konstant, symboladress + offset eller register), nåbarhetsflagga så att ingen kod genereras bakom konstanta villkor (`IS_ENABLED`, `compiletime_assert`, konstant `switch`, efter `return`/noreturn), `__always_inline` expanderas med konstanta argument bundna till parametrarna (asm `"i"`, `alternative_has_cap_likely`, `arch_static_branch` ser konstanter), lat emission (statiska funktioner/objekt bara om emitterad kod refererar dem), lokala variabler i stackslotar, temporärer i x9–x15/x19–x28. AAPCS64 (poster ≤ 16 byte i register, x8 för större, varargs med 64-byte sparyta), inline asm med constraints r/i/n/I–N/Z/m/Q/o/p, modifierare w/x/c/a/l/H/n, `asm goto` med utdata, bitfält, 128-bitars heltal, kärnans datainitierare (intervall, överskrivning, flexibla arrayer), alias/weak.
- Instruktionsposter (16 byte) genom ett smalt byggargränssnitt; skrivaren gör assemblytext som clang `-c` assemblerar i dag, en kodare (a64.c, annan session) kan ta över utan att codegen ändras. Funktioner är oberoende: arbetartrådar tar dem från en kö med egna post-, text- och bindningsbuffertar (mmap, 32-bitars index); bitarna sys ihop i fast ordning, så utdata är byte-identisk oavsett trådantal.
- Det kärnan kräver utöver -O0: `BUILD_BUG_ON` i vanliga `static inline`-/`static`-funktioner som bara viks när funktionen inlinas där argumenten är konstanta (`io_kiocb_cmd_sz_check`, `patch_map` → `fix_to_virt`). Lösning: inline-funktioner som anropar en `error()`-funktion inlinas; en statisk funktion där ett `error()`-anrop överlevde markeras och TU:n genereras om med den inlinad (sällsynt). Kvarvarande `error()`-anrop är sådana clang tar bort med värdeintervall (`min()`-signedness i `while (len > 0)` i `copy_string_kernel`) och tas bort, som kärnans egen -O0-variant (`((void)(condition))`); en trap där slog till vid boot. `__builtin_constant_p` följer clangs regler: viks i front end → 1; sidoeffekter eller pekare → 0; annars operanden efter inlining.
- **Acceptans:** `bench/vcc-obj.py` (ny) bygger clang-referenser med varje TU:s flaggor och jämför per objekt: definierade globala symboler (bindning, typ, sektion, storlek för objekt), att inga måste-vara-döda funktioner refereras (`__compiletime_assert_*` m.fl.), och relokeringarna inuti globala dataobjekt. Skillnader som följer av att clang -O2 inlinar mer (andra odefinierade symboler, kod- och asm-tabellsektioner som `.altinstructions`/`__bug_table`, `.text.unlikely.`) är noteringar. **840/840 kärn-TU:er gröna**. Exekveringstester i qemu (`ccs/vcc/tests/emit/run.sh`, bare-metal virt, UART-utdata mot clang -O2): t1–t6 gröna.
- **Död kod som -O2 tar bort (2026-11-08):** för att länka och boota behövs mer av det clang -O2 gör: små `static inline`-funktioner (≤ 40 AST-noder) inlinas (`scs_is_enabled()`); lokala variabler som bara tilldelas heltalskonstanter följs längs framåtkanter (värdet följer med hoppet till etiketten, sammanflöden behåller det alla vägar är överens om, loophuvuden och okända mål glömmer; t.ex. `alternative_has_cap_unlikely` och `enable_scs` i `map_kernel`); `__builtin_strcmp` av två strängliteraler viks (`vruntime_cmp`); kastade uttryck utan sidoeffekter genereras inte (`(void)(RCU)`); oanvända skalära argument till inlinade funktioner genereras inte. Användaretiketter är bara tvingat nåbara om de är mål för bakåthopp eller `&&`. Ett internt fel fångar hopp bakåt till en etikett som släppts eller placerats med kända värden. `bench/vcc-obj.py` listar symboler vcc lämnar odefinierade som inte finns i vmlinux.
- **Boot (2026-11-08):** `bench/vcc-kbuild.py DST` (ny) kopierar build-o0, ersätter alla 838 kärn-TU:ers objekt med vcc-objekt (`vpp -S`, clang assemblerar), länkar med `kmake ... LD=ccs/vroom-ld Image`. vDSO byggs av clang (kräver PIC, saknas). vroom-ld stubbar 28 symboler som bara död kod refererar (clang -O0: 94, vcc:s är en delmängd). **Debian 13 bootar till `running` utan fel** (`bench/boot-test.py`, `results/boot-20261108-001340.log`). Fel som hittades på vägen (bisektion per objekt mot clang-objekt, ~17 s per steg): `for`-initierarens cleanup-djup i förpasset (`scoped_guard` låste upp två gånger → preempt-räknaren fel), en oanvänd `va_list` (32-byte post) kopierad från adress 0.
- **Kodkvalitet, första steg (2026-11-08):** mätt mot clangs -O2-pipeline (kärnans flaggor) på 80 slumpvisa kärn-TU:er, statiskt (instruktioner i objekten): vcc 2,86× clang -O2, clang -O0 2,78×; med clangs -O2-bakände ensam 1,46×, + mem2reg/SROA 1,15×, + InstCombine/SimplifyCFG/EarlyCSE 1,10×, + inliner 0,88×, + GVN/LICM/DSE 0,86× (full -O2 1,00×: unrolling och mer inlining kostar storlek). Klassning av vcc:s sp/fp-laddningar och -lagringar: vanliga lokala variabler 31 % av instruktionerna, omladdning direkt efter lagring 6,6 %, kopior slot→slot 6,2 % (inlinade anrop band parametrar och resultat till slotar), `b` till nästa etikett 6,5 %, spill runt anrop 0,2 %. Två åtgärder: (1) `b` till en etikett direkt efter tas bort i `finish_function`; (2) en inlinad funktions parametrar som kroppen aldrig skriver (adresstagning räknas som skrivning) behåller argumentets register (`B_REG`, läsning = `mov`, smala typer utvidgas en gång som en laddning skulle), och resultatet kommer i ett register (`fnctx.retreg`), så länge minst 8 temporärer är lediga. Resultat på samma 80 TU:er: instruktioner 370 852 → 339 546 (−8,4 %, 2,62× -O2), sp/fp-operationer 174 156 → 135 005 (−22 %), `b` till nästa 24 060 → 6, omladdningar 24 313 → 11 102, kopior 22 845 → 8 533; spill runt anrop 608 → 5 360 (parametrar i x9–x15 över anrop i kroppen: nästa steg är callee-saved register för dem). Kompileringstid oförändrad (fair.c 0,10 s). Verifierat: körtester t1–t6, `bench/vcc-obj.py` 840/840 (samma 31 odefinierade symboler som före), `bench/vcc-fpcheck.py` 840/840, alla 840 kärn-TU:er från vcc bootar Debian 13 till `running` (`results/boot-20261108-231928.log`). Nästa: lokala variabler i register (31 % av instruktionerna). **Rättelse:** bootloggen ovan (`boot-20261108-231928.log`) var en clang-kärna: kbuild byggde om det nykopierade trädet med clang (854 CC-rader), se nästa punkt; ändringarna är bootverifierade där.
- **Lokala variabler i register (2026-11-08):** förpasset räknar varje lokal variabels användningar viktade med loopdjup (8× per nivå, `luse`) och markerar dem som behöver minne (`lnoreg`: adresstagning, asm-operander som utdata eller m/g/X, va_start, `{}`-initierare; skilt från vanlig skrivning). Den emitterade funktionens (inte inlinade kroppars) mest använda skalära lokala variabler och parametrar får callee-saved register x28 nedåt för hela funktionen (`B_LREG`, lvärde `L_VREG`: läsning `mov`, skrivning `mov`/utvidgning som en laddning från slot skulle ge; adress eller minnesåtkomst är internt fel). Registren tas ur temporärpoolen. Tar temporärerna slut genereras funktionen om utan registervariabler och med fler lediga temporärer i inlinade anrop (`INLINE_FREE_FAST` 5 → `INLINE_FREE_SAFE` 8). Ny `emit/opt.c`: livenessanalys över funktionens poster (block vid etiketter och efter hopp; inline asm, `I_WORD`, brk läser alla register; funktioner med `br` lämnas), framåt kopiepropagering (`mov t, v` → läsningar av t läser v), bakåt (`op t; mov v, t` → `op v`), döda `mov` bort. Mätt (80 TU:er, statiskt): `VCC_LREGS`=0/4/6/8/10 ger 311 794/292 299/291 677/291 417/… instruktioner; utan opt.c ökade antalet (varje läsning en kopia, `mov` 39 k → 76 k). Med 6: instruktioner 339 546 → 291 677 (−14 %; sedan sessionens start 370 852, −21 %; 2,25× clang -O2, färre än clang -O0:s 361 076), sp/fp-operationer 135 005 → 100 715 (−25 %; från start −42 %). Kvar: lagringar 11 %, laddningar 8,7 % (lokala i inlinade kroppar och icke valda), spill runt anrop 3,5 % (upp från 1,6 %: färre callee-saved temporärer), sparade callee-saved 5,9 %. Kompileringstid oförändrad (840 TU:er `--batch -S` 7,6–7,7 s mot 8,0–8,1 s). Verifierat: körtester t1–t6 (även `VCC_LREGS=10`), `bench/vcc-obj.py` 840/840 (samma odefinierade symboler), `bench/vcc-fpcheck.py` 840/840, även med ASan/UBSan och TSan (TSan: två rapporter i `pp/include.c:428` och `arena.h:84`, inte i emit/), 838/838 kärn-TU:er från vcc bootar Debian 13 till `running` (`results/boot-20261108-235047.log`, 28 stubbade symboler). `bench/vcc-kbuild.py` bygger inte längre vDSO:n med vcc (kräver PIC; ett återanvänt träd fick annars vcc:s `memcpy`-anrop i vDSO:n) och avbryter om kbuild byggt om något vcc-objekt med clang.
- **Prestanda (2026-11-08, laddad maskin, load ~4):** emittern på alla 840 TU:er: 459 ms CPU (0,55 ms/TU, varav utskrift 122 ms), 4,48 M instruktioner = 17,9 MB maskinkod, 39 MB/s, 103 ns/instruktion, en tråd per TU (`bench/vcc-emit-stats.py`). `kernel/sched/core.c` ensam: 2,9 ms med 1 tråd, 1,26 ms med 2, 1,04 ms med 4. Roofline (`bench/emit-roofline.c`, 29 755 instruktioner): maskinkod 0,10 ns/instruktion (~40 GB/s), text 0,74 ns/instruktion; vi ligger två storleksordningar över, det mesta i generering och utskrift, inte i minnesbandbredd.
- **Optimeringsnivåer (2026-11-09):** `cc_opts.opt` får `-O`-nivån (0: ingen/`-O0`, 1: `-O1`/`-Og`, 2: `-O2`/`-O3`/`-Os`/`-Oz`/`-O`). Från `-O1`: registervariabler, inlinade parametrar och resultat i register, borttagna hopp till nästa etikett och `opt.c`; `-O0` är den enkla kodgeneratorn. Målet är att resten av clangs `-O2`-pass hamnar på samma nivåer. `tests/emit/run.sh` väljer nivå med `VCC_O` (standard `-O2`). Verifierat: t1–t6 vid `-O0`/`-O1`/`-O2`, `-O2`-utdata identiskt med 74b2d82 (80/80 TU:er), `bench/vcc-obj.py` 840/840, 838/838 kärn-TU:er från vcc bootar (`results/boot-20261109-000308.log`).
- **Villkor på en cset (2026-11-09, från `-O1`):** är senaste posten `cset r, villkor` för värdet (`last_cset`) används flaggorna i stället för värdet: test i hopp blir `b.villkor` (`test_zero`), omvandling till bool och utvidgande heltalscast blir ingenting, `!` och `== 0` vänder villkoret, `!= 0` behåller det. Täcker `likely`/`unlikely` (`__builtin_expect(!!(x), …)` gav `cmp; cset; eor; cmp; cset; eor; sxtw; cbz`, nu `cmp; b.cc`). Mätt (80 TU:er): instruktioner 291 677 → 268 502 (−7,9 %; 2,07× clang -O2), `cset` 11,6 k → 3,5 k, `eor` 7,5 k → 0,8 k; emit-fasen 76 → 74 ms. Jämförelse med clang per klass och mönster: `ophist.py`/`ngram.py` (scratch). Nästa i ordning: argument direkt i x0–x7 (11,7 k `str` + 12,9 k `ldr`), spill bara av levande temporärer, lagring→laddning-vidarebefordran på slotar utan adress, redundanta utvidgningar. Verifierat: t1–t7 (ny `t7_cond.c`; fångar en mutation av `== 0`-vändningen) vid `-O0`/`-O1`/`-O2`, `bench/vcc-obj.py` 840/840, `bench/vcc-fpcheck.py` 840/840, 838/838 kärn-TU:er från vcc bootar (`results/boot-20261109-001905.log`).
- **Argument i register, spill bara av levande (2026-11-09, från `-O1`):** ett skalärt argument för x0–x7 som ligger i en temporär parkeras inte längre i en slot: `do_call` klassar först, håller kvar temporären (varken parkerad eller spillad) och flyttar den med `mov` till sitt argumentregister (flyttarna skriver bara x0–x7, aldrig en temporär, så ordningen spelar ingen roll); backward-passet i `opt.c` räknar sedan ofta ut värdet direkt i x0–x7. `spill_live`/`unspill` märker sina poster (`SPILL_REC` i `ins.sh`); `opt.c` tar bort återställningen av en temporär som är död efter den och dess lagring före anropet. Mätt (80 TU:er): instruktioner 268 502 → 250 787 (argument) → 240 941 (spill), sammanlagt −10,3 % (1,86× clang -O2); sp/fp-operationer 100 715 → 65 322; argumentmellanlagring 11,7 k `str` + 12,9 k `ldr` → 0,7 k + 1,6 k; spill runt anrop 5,6 k `str` → 1,8 k. Emit-fasen oförändrad (74 ms, uppmätt växelvis mot b498617 på en belastad maskin). Avstått: att låta en funktion ange vilka register den faktiskt skriver över (som GCC:s `-fipa-ra`) så att anroparen sparar färre: `ftrace_caller` sparar bara x0–x8 och anropar C-kod med `blr`, så med ftrace (och livepatch, BPF-trampoliner) skulle x9–x15 skrivas över; avstängt i den här konfigurationen men en fälla. Verifierat: t1–t7 vid `-O0`/`-O1`/`-O2` (spillborttagning utan livenesskontroll fäller 6 av 7), bygget fil för fil utan varningar, `bench/vcc-obj.py` 840/840, `bench/vcc-fpcheck.py` 840/840, 838/838 kärn-TU:er från vcc bootar (`results/boot-20261109-002714.log`).
- **Stackslotar: vidarebefordran och döda lagringar (2026-11-09, från `-O1`):** slotar återanvänds när ett scope slutar, så emittern håller en stack av levande allokeringar och märker varje FRAME-post (`I_LD`/`I_ST`, adress med `add`) med den allokering den ligger i (`SLOT_TAG` i `rm`/`ra`; en adress i början av en allokering som också är precis efter slutet på den under: `SLOT_TAG2`). Volatila åtkomster märks inte. `opt.c` (`slots`): en cell är ett intervall i en allokering som alla åtkomster når med exakt samma `ldr`/`str` (inget annat intervall i allokeringen, ingen omärkt åtkomst överlappar). Per block blir en laddning vars värde finns kvar i ett register (lagrat därifrån eller laddat dit, registret oskrivet sedan) `mov` eller laddningens utvidgning av registret; en lagring som skrivs över i blocket före en laddning tas bort; en cell som aldrig laddas mister alla lagringar. Pekar en adress in i allokeringen vidarebefordras bara fram till ett anrop, inline asm eller en lagring någon annanstans, och inga lagringar tas bort. En omärkt adress stänger av passet för funktionen. Celler per allokering direkt (bara allokeringar med flera intervall sorteras); register lästa/skrivna per post cachas från livenessanalysen (även den snabbare: 655 → 520 µs på `core.c`). Mätt (80 TU:er): instruktioner 240 941 → 229 027 (−4,9 %; 1,77× clang -O2), sp/fp-operationer 65 322 → 49 134; per offset i stället för per allokering gav bara 235 257. Emit-fasen 74 → ~82 ms (växelvis mot 8f805df; ~0,1 ms per TU av ~90). Verifierat: t1–t8 (ny `t8_slots.c`: smala typer, scope-återanvändning, unioner, adresstagna lokala som en anropad funktion skriver, pekare precis efter slutet bakåt, varargs) vid `-O0`/`-O1`/`-O2`; att strunta i escape fäller 7 av 8 tester, att strunta i överlapp fäller t1 (utvidgningen vid vidarebefordran är defensiv: inget test kräver den, värden i register är redan normaliserade); bygget fil för fil, `bench/vcc-obj.py` 840/840, `bench/vcc-fpcheck.py` 840/840 även med ASan/UBSan och TSan (TSan: bara den kända racen i `arena.h`/`pp_open`), 838/838 kärn-TU:er från vcc bootar (`results/boot-20261109-010043.log`).
- **Kända utvidgningar (2026-11-09, från `-O1`):** `opt.c` (`exts`) går framåt över blocken (möte vid sammanflöden, fixpunkt där bara block vars in-tillstånd ändrats gås om) och håller per register z (värdet ryms i sina låga z bitar, resten noll) och s (det är teckenutvidgningen av sina låga s bitar). Regler för laddningar (`ldrb`/`ldrsw`/…), `mov`-konstanter, `cset`/`csetm`, `csel`, `ubfm`/`sbfm` (utvidgning, `lsr`, `asr`, `lsl`), `and`/`orr`/`eor` (register och konstant), `add`/`add #` (en bit bärs), `lsr`/`asr` med register, `udiv`, `clz`; varje 32-bitarsoperation nollar övre halvan. En utvidgning (`ubfm`/`sbfm #0`, 32-bitars `mov`, `and` med låg mask) av ett register som redan har den formen blir `mov` x, som kopierings­städningen sedan tar bort. Bara x0–x15 och x19–x28 bär fakta; anrop och inline asm glömmer det de skriver. Funktionens ingång, block ingen grenar till och etiketter som inline asm (`asm goto`, trampoliner) eller en etikettadress (`&&l`) kan nå (ny flagga `LREF_EXT` i `lrefc`) börjar utan fakta. Svaret per post tas vid blockets sista besök (fakta blir bara svagare), så inget separat tillämpningspass. Mätt (80 TU:er): instruktioner 229 027 → 218 331 (−4,7 %; 1,68× clang -O2); `ubfx` 7 657 → 1 082, `sxtw` 5 509 → 2 348 (resten mest efter anrop och för argument, där AAPCS64 lämnar de övre bitarna odefinierade; `ldrsw` 2 741 mot clangs 110 kommer av att int hålls 64-bitars teckenutvidgade). Passet ensamt: 13,4 ms över 80 TU:er med separat tillämpningspass och mötet bit för bit → 6,7 ms (~0,08 ms per TU av ~100). Verifierat: t1–t9 (ny `t9_ext.c`: sammanflöden med olika utvidgning, loopar som breddar, `asm goto`, `&&l`, skift, masker, 32-bitars överspill, smala anropsresultat, en asm-funktion som lämnar skräp i x0) vid `-O0`/`-O1`/`-O2`; mutationer som fångas: möte med min, ignorerad `LREF_EXT`, `add` utan bärbit, `asr` av 32-bitars osignerat, maskens teckenregel, fakta kvar över anrop, `<` mot `<=` i `sbfm`-regeln; bygget fil för fil, `bench/vcc-obj.py` 840/840, `bench/vcc-fpcheck.py` 840/840 även med ASan/UBSan, `bench/vcc-cbatch.py` med TSan 840/840 (bara de kända racen i `arena.h` och `include.c`/`enter_file`), 838/838 kärn-TU:er från vcc bootar (`results/boot-20261109-093829.log`).
- **Kopior, döda resultat och inline asm (2026-11-09, från `-O1`):** fyra generiska ändringar i `opt.c`. (1) Etiketter som ingen gren når (scope, inlining) delar inte längre block: bara etiketter som en grenpost i funktionen pekar på eller som har `LREF_EXT` börjar ett block, så alla per-block-pass ser hela raka sekvenser (215 629 → 204 544). (2) Utökad inline asm får exakta register (`RAW_MASKS`: läser in-operander, `+`-utdata, adressregister för minnesoperander samt x18/x29/x30; skriver utdata och clobbers), lagrade efter asm-texten; enkel asm och asm goto (dess etiketter behöver det som lever där) läser fortfarande allt. Kopiepropagering skriver aldrig om ett register asm-texten namnger, `slots` räknar varje asm som en möjlig minnesskrivning, `exts` tar inte asm för en 32-bitarsoperation; `brk` läser och skriver inga register (204 544 → 201 274). (3) Kopiepropagering körs före `slots` också (med omräknade läs/skrivmasker), så vidarebefordran ser registren värdena faktiskt ligger kvar i, och efter `exts`; `mov x, x` tas bort (201 274 → 199 045, sp/fp-operationer 44 012 → 42 184). (4) Döda resultat: bakåt per block från blockets live-out tas rena instruktioner (ALU, `mov`, konstanter, adresser, laddningar från märkta stackslotar; aldrig flaggsättande) vars rd är död, så hela kedjor försvinner (218 331 → 215 629). Totalt (80 TU:er): 218 331 → 199 045 instruktioner (−8,8 %; 1,53× clang -O2), `mov` 50 934 → 39 408. `opt_function` 23,8 → 31,0 ms över 80 TU:er (~0,09 ms per TU). På vägen hittat och rättat (egen commit c4f2b10): callee-sparade register som inline asm clobbrar sparades inte i prologen. Verifierat: t1–t10 (ny `t10_asm.c`: utdata, `+r`, clobbers av x9–x15 och x19–x28, minnesoperander, fasta registervariabler, asm goto, etiketter mitt i block, döda värden, overflow-inbyggda) vid `-O0`/`-O1`/`-O2`; 11 mutationer fångas alla (bl.a. kopia in i asm, utan clobbers/utdata/inout/adressregister, masker för asm goto, `slots`/`exts` utan asm-skydd, utan omräknade masker, etiketter utan grenar, självkopia i 32 bitar); flaggsättande instruktioner med dött resultat och döda laddningar utanför stacken går inte att framkalla i testerna (reglerna är defensiva). Bygget fil för fil, `bench/vcc-obj.py` 840/840, `bench/vcc-fpcheck.py` 840/840 även med ASan/UBSan, `bench/vcc-cbatch.py` med TSan 840/840 (bara de kända racen), 838/838 kärn-TU:er från vcc bootar (`results/boot-20261109-101030.log`).
- **Ram efter städningen (2026-11-09, från `-O1`):** efter `opt.c` räknar `opt_regs` om vad posterna fortfarande använder: callee-sparade register som städningen lämnat oanvända (registervariabler, sparade temporärer) sparas inte, en ram där ingen slot längre refereras får inga lokala, och en funktion utan anrop, slotar, utgående argument, callee-sparade register, dynamisk stack, överjustering och utan läsning av x29/x30 (`__builtin_frame_address`/`_return_address`) får ingen ram alls: bara `ret` (flagga 8 i `ctor` för direktvägen). Som clangs utelämnade lövrampekare; inline asm som nämner x29 i texten ser då anroparens ram, precis som med clang. Mätt (80 TU:er): 199 045 → 195 958 instruktioner (−1,6 %; 1,51× clang -O2). Verifierat: t1–t10 vid `-O0`/`-O1`/`-O2`, ctests 1729/1729, abi (även `--host`), stress, bflayout, ldconst, fpcheck 840/840, `bench/vcc-obj.py` 840/840, bygget fil för fil, 838/838 kärn-TU:er från vcc bootar (`results/boot-20261109-103809.log`). Kvar: lövfunktioner med lokala (clang: bara `sub sp`/`add sp`, ingen x29).
- **`opt.c` snabbare (2026-11-09):** samma utdata, färre instruktioner. (1) `roles`/`brkind` slås upp i tabeller per opkod (byggda en gång ur switcharna; `ldp`/`stp`/asm går fortfarande genom switchen), `reads`/`writes` har en inline snabbväg (registerbit ur en tabell, en `csel` per operand; clang SLP-vektoriserade den grenfria varianten till ~50 NEON-instruktioner) och en långsam väg utanför för anrop, asm och `ret`. (2) Liveness räknar roller och masker en gång per post i första loopen och sparar dem (`W.ro`/`W.rr`/`W.ww`); omskrivningarna (framåt, bakåt, `slots` laddning→register) håller dem aktuella, så omräkningen före `slots` försvann och kopiepropageringen och `dead` läser cachen (kontrollerat med en tillfällig jämförelse mot omräkning efter varje pass: inga avvikelser i 840 kärn-TU:er och ctests). (3) `exts` går tillbaka till en bakkants mål direkt när dess fakta försvagas, så det som följer en loop gås igenom först när loopen är klar (2,25 besök per post förut); funktioner utan en utvidgning som kan vara överflödig hoppar över passet. (4) `slots` slutar direkt utan märkta slotåtkomster. Mätt (`bench/vbench.py ab emit`, 24 TU:er): emit 633 → 575 M instruktioner (−9,2 %); utan `opt.c` (`VCC_NOOPT=1`) 477 M, så passens andel 156 → 98 M (−37 %). `gate` ok (emit −7,4 % mot sammanslagningsbasen). Verifierat: `vbench.py check` 171/171 identiska, alla 840 kärn-TU:ers `.s`/`.o` byte-identiska med föregående commit, t1–t10 vid `-O0`/`-O2`, ctests 1729/1729, abi, stress, fpcheck 840/840, bygget fil för fil.
- **Två fel från optimeraren (2026-11-09, rapporterade av granskningssessionen):** (1) PostgreSQL `logging.c`: "internal: address of a register variable" vid `va_copy`: Apples `va_list` är en `char *` och kan bli registervariabel, men `va_copy`/`va_end`/`va_arg` tar dess adress; förpasset markerade bara `va_start`/`va_copy`:s mål. Nu markeras alla `va_list`-operander när `va_list` är skalär (Linux oförändrat, kärnobjekten byte-identiska). (2) `self-check`: steg 2 segfaultade, `ppdriver.c`:s `obj_state` lagrade −1 i `tl_os`: `opt.c` tog Darwins TLV-åtkomst (`I_TLV`, `blr x16` via deskriptorn) för ett anrop som skriver x0–x18, så `mov x9, x28` före den såg död ut; tunken bevarar allt utom x0 (och x16, lr), vilket emittern räknar med. Nu skriver `I_TLV` bara x0/x16/x30. Hittat genom bisektion per objekt (steg 2-objekt med och utan `opt.c`) och sedan per funktion. Tester: `tests/abi/va_copy`, `tests/abi/tlv_regs` (fångar båda felen med föregående commit, `--host`). Verifierat: `self-check` identiska, `bench/pg/build.sh vcc` 1504 filer, meson test 81/0, abi 7/7 och host 6 + 1 xfail, ctests 1729/1729, t1–t10, fpcheck, driver, stress, kärnobjekten byte-identiska.
- **Kvar:** register-cache för lokala variabler (allt går via stackslotar i dag), kodaren i stället för text (a64.c), PIC för vDSO, flyttal/VLA/TLS saknas, värdeintervall för de `error()`-anrop som nu tas bort.

### Status 2.7 (2026-11-07): assembler för kärnans `.S`, klar (inline asm kvar)
- `ccs/vcc/as.c` (GNU as-syntax), `a64.c`/`a64.h` (tabelldriven A64-kodare, delas med emittern), `elf.c`/`elf.h` (ELF-skrivare i minnet, mmap-reserverade regioner, ingen malloc; ett objekt per tråd). Drivrutin `vasm` (`make -C ccs vcc`).
- Täckning efter inventering av de 30 `.S`-TU:erna: `.macro`/`.irp`/`.irpc`/`.rept`/`.exitm`/`.purgem`, `.if*` enligt MC:s AsmCond, `.pushsection`/`.popsection`/`.previous`/`.subsection`, data/`.inst`/`.incbin`/`.org`/`.fill`/`.align`-familjen, `.set`/`.equ` med lata variabler (framåtreferenser, alias ärver typ/storlek), `.type`/`.size`-uttryck, numeriska labels, `.req`, `ldr =` med litteralpooler, `:lo12:`/`:pg_hi21:`/`:abs_gN(_nc|_s):`. ~200 mnemonics (heltal, ldst, system, SIMD, SHA, CRC, MOPS); 1 625 sysreg-namn tagna en gång ur llvm-mc (`gen-sysreg.sh` → `a64sysreg.inc`). Ingen CFI behövs (kärnan bygger utan `.cfi_*` i dessa TU:er).
- MC-beteenden som krävdes för byte-identitet: sektionsordning (`.strtab` först, `.rela` direkt efter sin sektion), StringTableBuilders svansdelning, symbolordning = registreringsordning (mappningssymbol före operandsymbol), sektionssymboler bara om en reloc använder dem, pc-relativa fixups mot lokala symboler i samma sektion löses, `$x`/`$d` per sektion men inte för utfyllnad, nop-utfyllnad med inledande nollor i kod.
- **Acceptans:** `bench/vas-diff.py cmp`: **30/30 `.o` byte-identiska** med `clang -c`. `bench/vas-diff.py cmp --vpp` (vår `vpp` i stället för `clang -E`, alltså ingen clang alls): **30/30**. För det skriver `vpp` nu källans mellanrum i `asm_mode` (gas-syntax är mellanrumskänslig: `\arg`, `661:`); header-cachen är avstängd i `asm_mode`, och C-pariteten är oförändrad (911/912). `bench/vas-enc.py` (varje distinkt instruktion mot `llvm-mc -show-encoding`): 2 266 ok, 0 fel, 524 går inte att ta fristående (lokala labels, `.req`) men täcks av objektjämförelsen.
- **Fart** (en tråd, indata mmappad, last ~7): alla 30 filer 3,8 ms/pass: in 461 kB = 121 MB/s, ut 123 kB = 32 MB/s, 26 902 satser efter makroexpansion = 7,1 M satser/s (162 MB/s expanderad text). Kritisk väg är `entry.S` (11 221 satser, 1,15 ms). Tak på samma maskin: memcpy 33–65 GB/s, 4-bytes store-loop 12–13 GB/s, så vi ligger 2–3 storleksordningar under taket. Kostnaden är parsning per sats (~140 ns), inte bytes, och den är jämnt fördelad (operandparsning, mnemonic-sökning, kodning). Första profilen dominerades av libc:s `isalnum`/`tolower` (locale); en egen teckenklasstabell och run-kopiering gav 7,27 → 3,8 ms. Som process: 30 `vasm` i följd 102 ms (≈3,4 ms/fil, alltså processgolvet), clang `-c` på samma text 1 413 ms. Parallellt över filer är det make som fördelar; att dela upp en fil i fragment lönar sig inte vid 1 ms.
- Kvar: inline asm (operandsubstitution `%0`/`%[x]`/`%w`/`%x`/`%c`/`%l` och ett snippet-API på ett beständigt `As` bundet till emitterns `ElfObj`, se `assembler-interfaces.md` i anteckningsmappen), integration i `vccd` för `-c X.S` (pp-utdata direkt till `as_run` i minnet), CFI om emittern behöver `.cfi_*`.

### Status 2.8 (2026-11-07): länkare + objcopy, `vmlinux` och `Image` utan lld, bootar
- `ccs/vcc/ld.c` (`vlink`, `ld_main()`): ELF-länkare för kärnans två länkar. Slutlänken med `vmlinux.lds` (skriptparser/-utvärderare med lld:s ExprValue-semantik, PROVIDE, orphan-placering, merge-sektioner i 32 xxh3-shards, AArch64-relokeringar inkl. ADRP+ADD→ADR-relaxering, thunkar enligt lld:s pass-ordning, phdrs, `.rela.dyn` för `-shared -z notext`, symtab/strtab i lld:s ordning, build-id) och `-r` (vmlinux.o för modpost). Indata mmap:as, tunga faser är parallella (egen trådpool, låsfri globaltabell), utdata byggs i en buffert och skrivs med en write. `ld_main` tar ett lås och kan anropas från vccd.
- `ccs/vcc/objcopy.c` (`vobjcopy`/`vnm`, symlänkar till `vlink`): byggets objcopy/nm-användningar enligt llvm-objcopy 23:s ELF-skrivare (segmentbevarande layout, StringTableBuilder med svansdelning, symbolborttagning/-prefix, `--prefix-alloc-sections`, `-O binary`, `-j`, `--dump-section`) och llvm-nm:s BSD-format (`-p`/`-n`/`-S`).
- **Acceptans:**
  - Slutlänken är **byte-identisk** med lld:s `vmlinux.unstripped` (build-id inräknat), med 1 och 8 trådar. `-r`: `bench/vld-rcmp.py` mot lld:s vmlinux.o OK (92 429 symboler, 169 601 relokeringar); modpost ger identiska utdata.
  - `bench/vobjcopy-cmp.sh`: 12/12 byte-identiska mot llvm-objcopy/llvm-nm (vmlinux, Image, modules.builtin.modinfo, vdso.so, 6 pi/*.pi.o, `nm -p -S vmlinux`, `nm vdso.so.dbg`).
  - Hela kedjan med bara våra verktyg (vlink → vnm → sorttable → vobjcopy → vobjcopy -O binary) ger en `Image` som är byte-identisk med lld-byggets; `bench/boot-test.py` med den: **BOOT OK, `running`, 0 felande enheter** (1,9 s).
- **Fart** (maskinen delad, last 2,5–5):
  - Slutlänk: **30–34 ms** med 8 trådar, 85 ms med 1 (lld 340 ms). Faser vid 8 trådar: mmap+parse 6–7 ms, resolve 3 ms, layout 3 ms, kopiera+relokera 2,9 ms, build-id 4,5 ms, write 4 ms. `-r`: ~14 ms.
  - Roofline: memcpy av 21 MB till nymappat minne tar 1,5 ms med 8 trådar (14,9 GB/s; 4,0 ms / 5,7 GB/s med 1), `write(2)` 3,4–7,8 ms. Kopiera+relokera når 7,3 GB/s ≈ 50 % av memcpy. Hela länken: 29 MB in + 21 MB ut på 31 ms ≈ 1,6 GB/s, ungefär 5× I/O-golvet (~6 ms). Resten är symbolupplösning, parse och layout (~15 ms) samt BLAKE3 (4,7 GB/s).
  - objcopy: strip av vmlinux 20,5 ms (llvm-objcopy 45 ms; golvet för en tråd ~12 ms), Image 9,3 ms (30 ms), `nm -p -S` 24 ms (61 ms). Processgolvet ~2 ms.
- Kända skillnader: vnm:s standardsortering ordnar helt lika nycklar annorlunda än llvm-nm (libc++ `std::sort` är instabil). Bygget använder den inte.
- Kvar:
  - ~~vdso-länken via lld~~, ~~`vlink`/`vobjcopy`/`vnm` i bygget~~, ~~länken i vccd~~: klart i 2.12.
  - `Image` kan skrivas direkt från vmlinux.unstripped parallellt med strip.

### Status 2.9 (2026-11-08): vccd kompilerar kärnans `.c` och `.S` själv, objekt utan asm-text
- `ppdriver.c`: `-c` av en `.c` kör pp (header-cachen delas mellan jobben) + cc.c + emit.c och ger ett ELF-objekt; `-c` av en `.S` kör pp-utdata genom as.c i minnet (`as_set_dir` för relativa `.incbin`). Ett `As`/`ElfObj`/utbuffert (4 GB NORESERVE) per tråd, objektet skrivs med en `write`, `-MD`/`-MMD`-depfilen som clangs. `cc_run`:s tabeller byggs en gång (`pthread_once`).
- `vccd.c`: `-c` av namngiven `.c`/`.S` med `--target=aarch64-*-linux*` körs i arbetartråden (jobbtyp `cc-own`); värdprogram (Mach-O), vDSO (`-mcmodel=tiny`, kräver PIC), `-fpic`/`-shared`/stdin går till vroomd som förut, och likaså varje TU vcc fallerar på (räknas, loggas med `VCC_LOG`). *Status 2026-11-10:* även stdin (`-`, läst av vccd så att en fallback får samma bytes) och värdprogram på arm64-Mac (`-c` utan `--target`; en `.c` rakt till program kompileras av vcc till ett temporärt objekt som backend länkar, jobbtyp `cc-link`). Kärnbygget: 928 kompilerade av vcc, 0 fallbacks, 13 vidarebefordrade (6 värdlänkar av flera objekt, 5 `cc-link`-länkar, 2 vDSO); vmlinux, Image, System.map, `.config` och alla `.cmd` byte-identiska med bygget där clang byggde värdprogrammen. `VCC_LOG=1` skriver hela kommandoraden för varje vidarebefordrat jobb (`vccd forward:`). Därefter även vDSO (`-mcmodel=tiny`: ADRP-par är PC-relativa som tinys ADR; `__builtin_memcpy/memmove/memset` med konstant storlek inline som clangs, vDSO har ingen memcpy): 930 av vcc, 11 vidarebefordrade, alla värdlänkar; ingen C-fil i kärnan går längre till clang. vdso.so: samma fem exporterade funktioner, inga dynamiska relokeringar, 5 440 byte kod mot clangs 2 664; Debian 13 bootar (`running`, 0 fallerade enheter). `VCC_CC=0` vidarebefordrar allt (A/B). Räknare vid SIGUSR1/SIGTERM. Trådstackar 16 MB.
- vas fick det kärnans inline asm använder: LSE-atomics (cas/casp, ld<op>/st<op>, swp med a/l/b/h), ldar/stlr/ldapr, exklusiva par, adc/sbc, smulh/umulh, umull/smull, `at`, SVE `ldr`/`str` av z/p med `mul vl`, pfalse/rdffr/wrffr/setffr, `:got:`/`:got_lo12:`, sysregs med op0 0–3.
- **Objektbackend** (`emit.c`): med `-c` är emitterns bitar strömmar av poster i stället för text: direktivtext (sektionsbyten, `.globl`/`.type`/`.size`, asm på toppnivå) går till ett beständigt `As` (as.c delad i `as_begin`/`as_snippet`/`as_end` + `as_sym`/`as_val`/`as_label`/`as_frag`); funktioner kodas med a64.h:s inline-kodare direkt i aktuellt fragment; inline asm körs som snippet på plats; dataobjekt och strängar skrivs med `elf_bytes`/`elf_fill` och ABS64-fixups. Uppspelningen gör samma `ElfObj`-anrop som assemblern gjorde för den utskrivna texten, så objekten är byte-identiska. Hopp i funktioner utan inline asm löses i kodaren; med inline asm är `.LBB`-etiketterna assemblersymboler (asm goto kan namnge dem). `VCC_OBJ_TEXT=1` behåller den gamla vägen (skrivare + `as_run`) för kontroll.
- Fel som hittades: skrivarens `data_bytes` läste 8 byte förbi `e->data.n` för `.xword` och läckte gamla bytes från föregående objekt (`init_groups.ngroups` blev 1; 19 TU:er). Rättat; kärnan som bootade i 2.6 hade felet.
- **Acceptans:** `bench/vcc-cbatch.py` (alla TU:er med `-c` i en `vpp`-batch): objektbackend och `VCC_OBJ_TEXT=1` **byte-identiska för alla 839 distinkta objekt** (före backend: text-vägen byte-identisk med clang `-c` av samma text, `bench/vcc-asdiff.py`); `bench/vcc-obj.py cmp` **840/840**. Kallt bygge genom shimsen (`bench/vccd-kbuild.sh`): **877 kompilerade av vcc, 0 till clang**; Debian bootar till `running`, 0 felande enheter (`results/boot-20261108-075239.log`).
- **A/B** (`bench/vccd-kbuild.sh`, `-j 14`, alternerande, last 10 → 44 av annat på maskinen): kompileringsfasen (första → sista CC) clang via vroomd 16,7/16,6/16,6 s → vcc **11,7/11,8/11,8 s**; totalt 22,1/22,1/22,1 → **17,0/17,1/17,2 s**. Före objektbackenden (text + as_run, en körning): 20,6 → 13,9 s.
- **Var tiden går:** batch med `-j 6` (last ~4): 3,3 s vägg, 16,9 s user + 3,1 s sys; `cc_run` 19,5 s jobbtid, varav kodgenerering + objekt **0,99 s** (text + as_run: 2,04 s), finish + write 0,25 s, 25,5 MB objekt. Front end (pp + parse/sema) är ~90 %. (Briefens "~4,3 CPU-s" för front end var väggtid; CPU är ~20 s, jfr Status 2.4: 22,6 s.) `kernel/sched/core.c` ensam: emit 2,5 ms mot 3,3 ms (utskrift 1,0 → 0,25 ms). I bygget summerar vcc-jobben till 34,3 s (medel 39 ms, max 247 ms `net/core/sock.c`) över 11,7 s, alltså ~3 jobb i luften av 14: kompileringsfasen begränsas nu av make, shim och beroendeordningen, inte av vccd (clang-jobben summerar till 106 s).
- **Kvar:** front end-kostnaden (deklarationsåteruppspelning, RESEARCH-preprocessor §6.4); per-TU mmap/munmap i emit.c och cc.c (sys 3 s) → arenor per tråd som nollställs per jobb; vDSO (PIC) och värdprogram (Mach-O) går fortfarande till clang; varför bara ~3 jobb är i luften under make `-j 14`.

### Status 2.10 (2026-11-08): defconfig med moduler, allt av vcc
- Konfiguration: defconfig med `CONFIG_MODULES=y` (1590 `=m`), utan debuginfo och LTO; bygget `Image` + `modules`.
- **cc.c:** `overloadable` (Itanium-mangling, clangs rangordning); GNU-vektortyper (`vector_size`, initierare, kast mellan lika stora vektorer, `v[i]`, `+ - & | ^` och sammansatt tilldelning); `__builtin_{add,sub}c*` och typade overflow-builtins; `((`-deklaratorer; konstantvikning av adress + heltal, `strlen("…")`, skalära sammansatta literaler, float → heltal; `__builtin_types_compatible_p` på kvalificerade arrayer.
- **emit.c:** `x && 0` / `x || 1` viks (döda anrop försvann, modpost godtog moduler); registervariabler utan initierare får sitt register (`fpsimd.c`, `w12`); `:lo12:`-laddningar bara när objektet verkligen är så justerat (ACPI:s `*(u32 *)"SSDT"`); vektorer i v16+, `"w"`-asmoperander, vektorer som 16-byte-poster i vcc:s egen anropskonvention.
- **Egen `ccs/vcc/include/arm_neon.h`** (före clangs resurskatalog i sökvägen): de ~25 intrinsics kärnan använder som makron kring vektoroperationer och inline asm; körtest på värden mot clang med riktiga `arm_neon.h`: 34 000 fall, 0 fel.
- **vas:** SME `ldr/str za[…]`, `eor3`/`bcax`, NEON-, AES-, SHA512- och MTE-instruktioner, `.octa`, `.reloc`, `.ltorg`, fördröjda uttryck på etikettskillnader; kodningarna kontrollerade mot llvm-mc.
- Identifierartabellen 2²³, delad pp-region 16 GB.
- **Acceptans:** `Image` + `modules` byggs, **10 990 kompilerade av vcc, 0 till clang** (215,6 s med last från annat; tidigare körningar ~150 s). Sviterna gröna: paritet 911/912, sema 878/879 + 878/879, obj 840/840, cbatch 840/840, vas 30/30.
- **Kvar:** boottest av modulkärnan; tidsjämförelse mot clang-baslinjen (575,6 s) på en lugn maskin.

### Status 2.11 (2026-11-08): vcc kompilerar sig själv (macOS, Mach-O)
- `macho.c`: `ElfObj` → Mach-O `MH_OBJECT` (sektioner, symboler, ARM64_RELOC_* med ADDEND/SUBTRACTOR); `elf_finish` delad i layout + fixups så att båda format delar allt utom utskriften. `ppdriver.c` skriver Mach-O för Darwin-mål.
- `emit.c`, Apples arm64-ABI: `_`-prefix, GOT för allt som inte definieras i TU:n, variadiska argument på stacken, packade stackskalärer; flyttal (bitmönster i GPR, beräkning i v16/v17, argument i v0–v7, konstanter vikta på värden; float↔`__int128` via `__floatti*`/`__fix*ti`); `__atomic_*` (LSE, alltid seq_cst); TLV (`__thread_vars`/`__tlv_bootstrap`); VLA (allokeras under sp som alloca, sp återställs via cleanup-stacken vid block-slut, break, goto, return).
- `ccs/vcc/include/`: egna `stdatomic.h` och `arm_acle.h`; `arm_neon.h` är 2.10:s (GNU-vektorer) utökad med det vcc:s lexer använder (cmeq/cmtst/cmhs, `vsriq_n_u8`; `vld4q_u8`, `vqtbl4q_u8` och `vshrn_n_u16` per lane eftersom vas saknar ld4/tbl med fyra register/shrn). Katalogen läggs före SDK:n vid Darwin (`VCC_INCDIR`). cc.c: `?:` med två vektorer av samma typ (clang godtar).
- `make -C ccs self` kompilerar alla 22 källor med vcc och länkar steg 2 (vpp, vccd, vasm, vlink, lextest, vcc) med ld64; `make -C ccs self-check` bygger steg 3 med steg 2 och kräver identiska objekt.
- **Acceptans:** steg 2 ↔ steg 3 **byte-identiska (22/22 objekt)**; steg 2-`vpp`: paritet 911/912 (modpost.c som förut), `vcc-sema.py run` 878/879, kärnans 840 objekt byte-identiska med steg 1; steg 1 oförändrat mot föregående commit (0 av 840). `bench/vcc-diff.py` tar `VPP=` från miljön.
- **Tid:** självkompilering 1,5 s (en tråd, sekventiellt). Steg 2-`vpp` är ~2,6× långsammare än clang `-O3`-bygget (kärnbatch `-j 6`: 4,1 → 10,6 s vägg) — vcc har ingen optimerare.
- **Kvar:** HFA-argument (fatalt fel), `long double` bara som double; optimering i emittern om steg 2 ska ersätta clang-bygget.

### Status 2.12 (2026-11-10): kärnbygget utan clang, vroomd och lld
Portat från grenen `claude/parallel-compiler-optimization-2bb32f` (2026-11-08) till mains layout.
- **vld, delade objekt (vDSO):** `PHDRS` (FILEHDR/PHDRS), `VERSION`-skript (version-lokala → STB_LOCAL), `.dynsym`/`.dynstr`/`.hash`/`.gnu.hash`/`.gnu.version{,_d}`/`.dynamic` i lld:s ordning, `=fill`, `-n`; bara för `-shared` med skript. vDSO:n byte-identisk med ld.lld 23 (utom `$d`-lokalerna, känt). Utan `PHDRS` blir `SIZEOF_HEADERS` 64.
- **vld, GOT under `-shared`:** ADRP+LDR → ADRP+ADD (→ NOP+ADR) bara när varje ADR_GOT_PAGE i sektionen följs av sitt LD64_GOT_LO12_NC (lld:s safeToRelaxAdrpLdr) och symbolen inte kan ersättas (`-Bsymbolic` eller lokal). Kärnan länkar vmlinux med `-shared -Bsymbolic --no-apply-dynamic-relocs`: utan relaxeringen läste koden orelokerade GOT-platser och en vld-länkad kärna bootade inte (tom konsol). `VCC_LDCMP=1` ctests 1773 → 1775/1775.
- **vld, flera länkar i en process:** `reset_state` nollställer nu lazy-arkivtabellerna (`lsyms`, `lcand`, `lout`, `ltaken`); förut pekade de in i frigjorda arenor, så vccd:s andra länk med `--start-group lib.a` fallerade eller kraschade (fanns i main, syntes först när vccd länkar själv).
- **vld, Mach-O (`platform/darwin/ldmacho.c`):** `ld_main` lämnar länken till `ld_macho` när första indatafilen är Mach-O: MH_EXECUTE som ld64 (`__PAGEZERO`, `__TEXT`, `__DATA_CONST,__got`, `__DATA`, `__LINKEDIT`), chained fixups, exporttrie, ad hoc-signatur (`ld_sha256` i ldhash.c). Bara libSystem.
- **vccd:** länkar i tråden genom `ld_main` (`--vroom-stubs`; `--ld PROG`/`--lld PROG` startar en länkare i stället), nm/objcopy genom vnm/vobjcopy bakom shimmarna `vcc-nm`/`vcc-objcopy` (ett i taget), värdprogram (`scripts/`) kompileras av vcc för macOS och länkas av `ld_macho`, `-` läses från förfrågans stdin (`pp_opts.main_file`). Utan `--backend-sock` faller inget tillbaka till clang: en kärn-TU som vcc avvisar fallerar med vcc:s egna diagnostik (`vjob_fail`/`vjob_escape` skriver nu till jobbets fd, inte serverns stderr). Med `--backend-sock` som förut. `-S` skriver depfilen som `-c`.
- **`bench/vccd-kbuild.sh`:** startar bara vccd (ingen vroomd), sätter `NM`/`OBJCOPY` till shimmarna, `VCCD_ARGS` till vccd.
- **Acceptans:** kallt bygge exit 0, 9,9 s (kbuild.py; 11,1 s med skriptets start/stopp), last ~2: 884 kompilerade av vcc, 0 fel, 0 vidarebefordrade, 52 värdjobb, 6 länkar, 14 nm/objcopy, 422 ar. `bench/boot-test.py`: BOOT OK (1,8 s, `results/boot-20261110-133025.log`). Med `VCCD_ARGS="--ld ccs/phase1/vroom-ld"` (lld) bootar samma träd också. vlink-opts 63/63, vlink-bad (ASan) 29/29, vccd-test 21 + 1 skip, vsh-test 37/37, vcc-stress --vccd 23/23, vccd-cc 174/174, var-cmp 12/12 + 111/111.
- **Kvar:** vld:s Mach-O-väg saknar TLV, SUBTRACTOR och andra bibliotek än libSystem; på Linux-värden går värdprogrammen ännu inte via vld (ingen ELF-värdlänk), så där behövs `--backend-sock`; vroomd byggs men behövs inte av kärnbygget. Konfigfasen i kbuild.py kör fortfarande `ld.lld --version`.

### Plan 2.13 (från grenen, 2026-11-08): konfig i ett steg, deklarationer per header-cache-post
Mätt på grenen före de lata kropparna (som main redan har, se "Profil av vcc/vccd:s eget arbete"): konfigsteget 0–2,6 s (allnoconfig, `merge_config.sh`, olddefconfig, `conf` två gånger), huvudmaken före första CC 2,6–4,3 s (`SYNC` = syncconfig + `conf` igen och omstart av make), kompileringsfasen CPU-bunden, svans 0,3 s.
1. **Konfig i ett steg** (bara `bench/kbuild.py`): `make KCONFIG_ALLCONFIG=<fragment> allnoconfig` med samma verktygsvariabler (`LD`, `NM`, `OBJCOPY`, …) som huvudmaken, så att `auto.conf.cmd` stämmer och huvudmaken varken kör `SYNC` eller startar om. Uppmätt en gång: 1,18 s i stället för 2,6 s, `.config` byte-identisk. Väntat −2,5 s. Kontroll: `.config` identisk för `vroom.config`, `vroom-min.config` och defconfig-vägen; bygget bootar.
2. **Deklarationer per header-cache-post** (punkt 2 i listan under "Profil av vcc/vccd:s eget arbete"): postens delta (nya poster per array, ändringar av äldre) och dess beroenden (uppslag utanför regionen med svar, även negativa) sparas; en TU som når posten på en deklarationsgräns med samma svar spelar upp deltat, preprocessorn tillämpar posten utan att leverera token. Steg 0 är mätning (andel token i poster på deklarationsgränser, beroenden per post, träffar); bygg bara vid ≥ 60 %. Ersätter prefixträdet (`ccsnap.c`). Kontroll: objekt och stderr identiska med `VCC_NODC=1`, sviterna, TSan på vccd-cc.

### Status testsviter (2026-11-08): gcc c-torture + tcc tests2 importerade

`ccs/vcc/tests/gcc-torture/execute` (gcc e6bf7e6f, 1 701 test, GPL-3) och
`ccs/vcc/tests/tcc-tests2` (tinycc 70adbb12, 145 test, LGPL-2.1), oförändrade;
README där om källa, licens, uppdatering och varför just de (självkontrollerande
körtester i GNU C, utan dejagnu; gcc.dg/clang-lit bundna till sina diagnostiker).
`bench/vcc-ctests.py -j 6`: varje test byggs av clang (orakel, cachat) och av
`vpp -c`, länkas med en egen bare-metal-runtime (`tests/rt`: start.S, liten libc,
byggd av clang) och körs i `qemu-system-aarch64 -M virt` (semihosting ger
exitkoden); vcc godkänns när utskrift och exitkod är clangs.  Kallt ~2 min,
med cachade referenser ~40 s.  Hoppar över tcc-specifika test, sådant clang
avvisar och sådant clangs bygge inte klarar på runtimen (long double utan
compiler-rt, fil-I/O).

| | gcc | tcc |
|---|---|---|
| utgångsläge | 1193/1589 (75,1 %) | 74/103 (71,8 %) |
| K&R, implicit int/funktion, `-std=` | 1306 (82,2 %) | 75 |
| main utan return ger 0 (strcmp på ointernerat namn) | 1344 (84,6 %) | 78 |
| för lång strängstart i char-array | 1348 | 78 |
| SHF_MERGE-reloc med addend mot symbolen (som MC), `__builtin_expect`-sidoeffekter, wchar_t utan `-fshort-wchar` | **1353 (85,1 %)** | **80 (77,7 %)** |

Kvarvarande fel (gcc+tcc): **fp 131** (flyttal, egen gren pågår), **fe 60**
(vektortyper 40, `__complex__`/`i`-suffix 10, nästlade/okända konstruktioner),
**emit 41** (okänd builtin 55 = `__builtin_*_overflow` m.fl. 15, VLA 11, asm-`g`-
constraint 10, `.Ltmp`-etikett 2, 128-bit switch), **link 14** (implicit `alloca`
borde bli builtin 3, `link_error`-test som kräver konstantvikning 5,
`__sync_add_and_fetch`, `__udivti3`), **run 13** (VLA-parametrars storleksuttryck
med sidoeffekter 970217-1/pr77767, `__builtin_return_address`, beräknad goto i
comp-goto-1, va-arg-21, konstruktorordning 108_constructor, 39_typedef, 129_scopes).
Nästa mål: builtins för overflow, VLA, asm `g`, sedan FP när den grenen landar.
Predef-tabellen för Linux har kärnans flaggor inbakade (`-mgeneral-regs-only`
tar bort `__ARM_NEON`/`__ARM_FP`); bör justeras när FP finns.

Efter merge med main (FP, vektorer, overflow-builtins, VLA från 2.10/2.11):
gcc 1453/1589 (91,4 %), tcc 90/103 (87,4 %).  Därefter: implicit `alloca` som
builtin, pekare till VLA skalar med elementets körtidsstorlek, `register`-lokaler
utan initierare får N_DECL (platsen allokerades lat inne i en loop och
återanvändes efter `foff = mark`: 20000801-1/20000815-1 hängde), `-fgnu89-inline`
→ **gcc 1463 (92,1 %), tcc 90 (87,4 %)**.  Kvar: fe 51 (vektoroperationer,
`__complex__`), emit 45 (builtin 57/67, asm `g`, HFA-argument 9), fp 21
(flyttals-va_arg 12, 128-bitars flyttal 6), link 8 (kräver optimerare:
`link_error`-test), run 14.

Gren claude/vcc-ctests-3 (2026-11-08), en commit per steg, alla grindar gröna
efter varje: GNU-vektorernas luckor, flyttals-va_arg (v-registrens sparområde,
`__vr_top`/`__vr_offs`), HFA-argument/-resultat/-va_arg (nästlade poster och
arrayer; överjusterade på stacken 16-justerade), `va_end` utvärderar sitt
argument, C23 `[[...]]`-attribut (`gnu::`/`clang::` blir GNU-attribut), gamla
`fält: värde`-designatorer, `__sync_*`, isnan/isinf/signbit m.fl. i körtid,
128-bitars switch, K&R-float-parametrar som double, sammansatta literaler lever
till blockets slut (platsen återanvändes av senare satsers temporärer), C99:s
satsscope (if/switch/loopar och deras delsatser är block), `destructor`,
ingen inlining av funktioner vars statiska lokal håller `&&label`, `_Complex`
(post `{ T re, im; }`, sänkt i sema till delarna: N_CPLX; ABI som HFA/två ord),
overflow-builtins som satser (ströks förut!), 128-bitars overflow-resultat,
VLA-parametrars storleksuttryck med sidoeffekter, bitfält över 8 byte, UTF-8 i
symbolnamn (vas), `__udivti3` m.fl. i test-runtimen, qemu `-cpu max` (LSE).
→ **gcc 1572/1588 (99,0 %), tcc 99/101 (98,0 %)**, totalt 1671/1689 (98,9 %).

Därefter (samma gren): `long double` som binary128 (mjukflyttal `__addtf3`,
`__multf3`, `__divtf3`, jämförelser, konverteringar i test-runtimen, bara
heltalsoperationer; i emittern registerpar, q-register i ABI:n, HFA:er av
long double, va_arg), `abs`/`labs`/`llabs` som builtins, `-finstrument-functions`,
etikettdifferenser i statiska initierare, NRVO-ordning för cleanup vid retur av
lokal post (som clang), isnan/isinf (vänsterskiftet var ett högerskift),
libm-funktioner på konstanter viks, const-flyttalsglobaler viks, och små
optimeringar som `link_error`-testen kräver: skift med 0/0-skift,
x op x och motsägande/kompletterande jämförelser i &&/||, intervall för
`a || b` under a:s negation, spårade konstanter i lokala arrayelement `a[K]`
som aldrig flyr, fabs-icke-negativa flyttalslokaler.
→ **1708/1708 (100 %)**, 136 överhoppade (clang avvisar 69, clangs bygge
klarar inte runtimen 34, tcc-specifika 32, kompilatorspecifikt 1).

### Status fuzzning (2026-11-08): lexer och preprocessor
- `ccs/fuzz/`: `fuzz-lex` och `fuzz-pp` (libFuzzer från Homebrew LLVM, ASan+UBSan; fristående drivare utan libFuzzer), korpus genererad ur kärnheaders (`gen-corpus.py`, inget kärntext incheckat), ordbok `c.dict`, differentiell kontroll mot `clang -E -P` (`pp-diff.py`), regressionsfall som `make -C ccs fuzz-regress` spelar upp. lex.c fick `vlex_get_hook` (filer ur minnet) och `VLEX_SCALAR` (skalär referens).
- Fynd, rättade: (1) `vintern_copy` läste 16 byte ur nyckelords- och builtin-literaler (ASan, vid start); (2) fatalt fel i huvudfilen (obalanserad `#if`) gjorde `exit(1)` i `pp_open`, vilket hade dödat vccd: nu NULL; (3) `, ## __VA_ARGS__` tog bort kommat även när variadiska argumentet var tomt men angivet (`VA(x,)`); clang gör det bara när det är utelämnat.
- Körningar: 3 min × 2 arbetare per mål; fuzz-lex utan fynd; fuzz-pp bara falsklarm från en egen kontroll (`#line 0` ger rad 0, borttagen); fynd (2) och (3) kom från seed-korpusen och pp-diff. En andra runda avbröts (maskinen upptagen). Paritet 911/912 (modpost.c) efter varje rättning.
- **Kvar:** längre körningar när maskinen har lediga kärnor; pp-diff över hela fuzz-korpusen.

### Status sanitizers (2026-11-08): ASan+UBSan och TSan, alla fynd rättade
- `make -C ccs vcc-asan` / `vcc-tsan` (`-O1 -g`, egna `build/ccs-asan/`, `build/ccs-tsan/`; normalbyggets kommandon oförändrade). Harnessarna tar `--bin-dir DIR`/`VCC_BIN`; `bench/vccd-cc.py` kör 150 TU:er `-c` genom shimmen mot en egen vccd. De stora reservationerna (16 GB delad pp-region, 4 GB NORESERVE) fungerar utan ändringar. AGENTS.md, "Sanitizers".
- Fynd, rättade (en commit var):
  - ASan: nyckelords- och builtin-namn internades ur ovadderade literaler (`vintern_cstr`; fuzz-sessionen hittade samma); `pp_open` gav anroparens huvudfilssträng (malloc:ad jobbfil i `vpp --batch`) till `vlex_get`, som läser 8 byte förbi; lextest likadant. Kontraktet står i lex.h.
  - TSan: `hc_mode` satte flaggan före `hc_mem_max` utan ordning, så en tråd kunde lagra i header-cachen utan tak (nu `pthread_once`), samma mönster i `dc_begin`; lata env-flaggor och `VPP_TIME`-räknare (`pp_stat_*`, `vdir_probes`) var vanliga `++` från alla trådar; `salloc` (16 GB-regionen) och dircache `dalloc` läste basen med vanlig load efter CAS; `file_line` skrev `nlines` icke-atomiskt även hos förloraren; as.c `names_ops` "benign race".
  - Efter merge av diagnostikarbetet (ASan 7, TSan 19 felande TU:er i vcc-cbatch, 731 TSan-rapporter): `snap_restore` (deklarationscachen) tog snapshotens `c->cases` (malloc) och `c->span`, så alla TU:er som återställde samma snapshot realloc:ade samma block från flera trådar (heap-korruption) och skrev spans i en annan TU:s array; `sym.plb/ple` flyttades inte. Nu egna per TU, spans är en snapshotarray med flyttade platser, och `cc_free` släpper dem (2 GB adressrymd läckte per TU). Objekten byte-identiska med `VCC_NODC=1`.
  - Header-cachens låsfria CAS-publicering, tokencachen, intern-tabellen och vlink:s trådpool gav inga rapporter.
- **Acceptans** (efter merge av main): ASan+UBSan (`UBSAN_OPTIONS=halt_on_error=1`) och TSan: `vcc-diff.py cmp --kernel` 873/873, `batch --cmp` 873/873, `vcc-sema.py run` 878/879, `vcc-cbatch.py` 840/840, `vccd-cc.py` 174/175 (vDSO-skriptet ska till backend), lextest `bench -p`/`idcheck`, vlink `-r` + slutlänk och vobjcopy/vnm byte-identiska med normalbygget; 0 rapporter, inga suppressions (`ccs/tsan.supp` behövs inte). Normalbygget: `vcc-diff.py cmp` 911/912, `vcc-sema.py run`/`layout` 878/879. TSan-batch ~65–100 s, ASan 12–24 s per svit.
- **Kvar:** ASan ser inte överläsningar inuti egna mmap-arenor (ogiftade); manuell förgiftning (`ASAN_POISON_MEMORY_REGION` bakom arenornas `used`) under `__has_feature(address_sanitizer)` vore nästa steg. Fuzzmålen kunde också byggas med TSan.

### Status PostgreSQL (2026-11-08): andra projektet, macOS, meson + ninja, all C av vcc
- PostgreSQL 18.6 (`work/postgres`, REL_18_6, C) byggt nativt på macOS med meson + ninja: `bench/pg/build.sh clang|vcc [setup|build|test|all]`. Referensen är Homebrew LLVM 23 (Apple clang 21 svarar annorlunda, t.ex. på `format(gnu_printf)`). `bench/pg/cc` är CC: `-c` av `.c` → `vpp`, kompilera+länka (meson-prober) → vpp per källa och clang länkar, allt annat (`--version`, `-E`, länkning, `.S`) → clang. meson-prober (`meson-private/`) godkänns bara om clang också godtar dem: vpp ignorerar okända flaggor, och meson lade annars till GCC-flaggor (`-fpch-deps`, `-Wimplicit-fallthrough=3`). Installation till `work/pg-$V-inst` före test: SIP stryker `DYLD_LIBRARY_PATH` när pg_regress kör psql via `/bin/sh`. Alternativ: `-Dauto_features=disabled -Dcassert=true`, debugoptimized.
- Fynd, rättade: (1) `__sync_val/bool_compare_and_swap`, `__sync_lock_release`, `__sync_synchronize` saknades (configure föll tillbaka på semaforer); (2) `__builtin_bzero`; (3) anrop med fler än 128 argument (psql:s sql_help.c: ~260) → argumenten på heapen; (4) NEON-intrinsics till simd.h/pg_popcount (`vqsubq_u8`, `vminq/vmaxq_u8`, `vcnt(q)_u8`, `vpaddlq_u8/u16`, `vpadalq_u32`, `vaddv*`, `vmaxvq_*`, `vceqq_u32`, `vzip1q_u8`, `vdupq_n_u32` m.fl.) och i vas `uqsub`, `umin`/`umax`, `cnt`, `uaddlp`/`uadalp`, `addp` (även skalär `addp d, v.2d`); (5) **lokala variabler med alignment > 16** fick bara 16 (`PGIOAlignedBlock`, aligned(4096): Assert i mdextend): prologen gör nu `sub x16, sp, #ram; and sp, x16, #-A`, locals/outs avrundas till A (ram adresserad från x29, VLA/alloca, ger fatalt fel i stället); (6) **pp:s tmp-arena återanvändes medan parsern höll lookahead**: `string_lit` tittar tre token framåt, och en andra `#x` skrev över den förstas stavning (`CppAsString2('r') "," CppAsString2('p')` blev `'p','p'`, psql `\dt` visade inga tabeller); `-E` skriver token direkt, så pariteten såg det aldrig. Nu `pp_hold_tmp()` runt `peek`; (7) **deklaration som hoppas över** (`switch (k) { T *p; case …`) fick slot vid första användning, i satsens skrapyta, som ett senare anropsargument återanvände (ecpg:s `check_indicator` segfaultade): `dead_decl` binder slotten på deklarationens plats. (8) **`vpp --batch` hängde** efter ~1 470 av 1 574 TU:er: lexerns filcache (`ctab`, 65 536 platser, öppen adressering) och dircache (`dtab`, 16 384) växte aldrig, och varje provad sökväg (även saknade, gånger alla `-I`) får en post; full tabell = oändlig probe-loop i `centry_get`. Båda är nu låsfria kedjor (CAS på hinkens huvud), aldrig fulla. Test `many_probes.c`: 65 536 `__has_include` i en TU (gammal vpp: hänger).
- Test: `ccs/vcc/tests/vcc/` (ny svit i `vcc-ctests.py --suite vcc`, självkontrollerande, en fil per fynd: 7/7, alla 7 fallerar med vcc före rättningarna) och `ccs/vcc/tests/vas/simd-pg.s` (`bench/vas-enc.py $PWD/ccs/vcc/tests/vas`: 47/47 mot llvm-mc).
- **Acceptans:** pg_config.h identisk med clangs utom `USE_SVE_POPCNT_WITH_RUNTIME_CHECK` (vcc har inga SVE-typer, proben fallerar rent); alla 1 504 C-filer av vcc utan reserv; `meson test` 81 ok / 0 fel / 249 överhoppade (TAP av), samma som clangs bygge (regress 231, isolation, ecpg, contrib, testmoduler). Bygge `-j 6`: clang 43 s, vcc ~26 s (via Python-shimmen, ingen vccd). vcc-sviterna: paritet 911/912, `vcc-sema.py run`/`layout` 878/879, ctests 1715/1715, fpcheck 840/840, batchparitet kärnan 873/873.
- **Hastighet** (`bench/pg/speed.py -r 2`, 1 574 C-filer ur clang-trädets compile_commands.json, `-j 6`, bästa av 2, last 10–16 på maskinen):

  | läge | vägg | CPU | filer/s | mot clang |
  |---|---|---|---|---|
  | clang -O2 -g (byggets flaggor) | 45,2 s | 223 s | 35 | 1,0x |
  | clang -O0 -g0 | 26,1 s | 144 s | 60 | 1,7x |
  | vpp, en process per fil | 11,3 s | 54 s | 139 | 4,0x |
  | vpp --batch (en process, 6 trådar, som vccd) | 1,5 s | 5,3 s | 1 074 | 31x |

  Batch: 57 610 träffar i huvudcachen, 935 återställda deklarationsregioner, 32 846 lata kroppar aldrig parsade; 38 MB objekt (clang -O0: 36 MB). 4 filer fallerar i vcc-lägena (3 × `pg_popcount_aarch64.c` med SVE ur clangs konfiguration, `norm_test.c` saknar en genererad header; den fallerar även för clang).
- **vccd i PostgreSQL-bygget** (`bench/pg/build.sh vccd`): CC är vcc:s native shim mot en privat vccd. Med `VCC_DELEGATE=bench/pg/cc` i miljön kompilerar vccd själv `-c` av `.c` (delade huvud- och deklarationscacher) och kör allt annat som `bench/pg/cc` (prober, länkning, `--version`, TU:er vcc fallerar på; `VCC_DELEGATE_SKIP=meson-private` skickar alltid meson-prober dit). Drivern förstår nu `-MQ`. Resultat: 1 504 av 1 504 C-filer av vcc i vccd, 0 reserv; `meson test` 81 ok / 0 fel / 249 överhoppade, som clang.
  - Helt bygge (`ninja -j 6` efter `ninja -t clean`): vccd 9–12 s, Python-shimmen ~26 s, clang 43 s.
  - Bara psql (`ninja src/bin/psql/psql` efter clean, 214 steg, bästa av 2, last ~15): clang 4,4 s, Python-shimmen 2,95 s, vccd 1,06 s. I vccd tar de 145 kompileringarna 0,33 s vägg (~10 ms styck mot 125 ms via Python-shimmen, där 50 ms per fil var Python och resten omparsade headers); kvar är genbki.pl (perl, 0,4–1 s, samma för clang) och länksvansen (libpq.dylib, mesons `.symbols`, psql: ~0,3 s), som inte är vcc:s.
- **Profil av `vpp --batch`** (1 570 TU:er, `sample` vid -j 1, 2026-11-09): av upptagen tid preprocessor ~47 %, parse + sema ~30 %, emit ~10 %, skriva .o ~8 %. Rättat: (a) `find_include` var ~14 %: 175 prober per uppslag, eftersom nyckeln saknade sökvägens id och varje meson-mål (egen `-I` först) lade en post per header under samma hash; nu med `fsig`: 1,5 prober, samma träffar, -6 % cykler och -7 % user-CPU vid -j 6. (b) macho.c:s qsort-jämförare läste objektet ur en filstatisk variabel: vid flera trådar blev symbolordningen fel i 36 av 1 570 objekt; nu `_Thread_local`, och -j 6 ger samma byte som -j 1. (c) Misslyckade `open()` av include-kandidater (~8 % av upptagen tid): 78 644, alla ENOENT, 73 % i kataloger som inte finns. dircache hoppade bara över absoluta sökvägar (kbuild), meson ger relativa `-I`; nu prövas den sammanfogade sökvägen: 21 122 kvar (i befintliga kataloger) mot ~8 500 katalog-stat, sys-tid -25 % vid -j 6, objekten bytevis lika. Säkert eftersom alla byggträdskataloger som include-sökningen tittar i finns direkt efter `meson setup`. De 21 122 som är kvar skulle kräva kataloglistning, och där kan genererade headers dyka upp under bygget: lämnas. Kvar, i storleksordning: skapandet av 1 570 objektfiler (open+close+write ~7,6 %, path-walk; `openat` mot en sparad katalog-fd skulle korta den); parallell överkostnad (-j 6: 11 % fler instruktioner än -j 1, fler missar i huvudcachen när trådar spelar in samma header samtidigt); deklarationscachen når sitt tak på 256 MB (1 GB: -2 % instruktioner, +80 MB).
- Kärnan: 22 av 839 objekt ändras (vcc-cbatch, före mot efter): 9 av (5) (t.ex. `call_single_data_t` på stacken är `__aligned(32)`), 13 av (7) (slotplacering). Inte boot-testat.
- **Kvar:** SVE (`arm_sve.h`-typer, `target("+sve")`); `__has_builtin` svarar 0 för namn som inte finns i kärnan/SDK:n (ppdata.inc, t.ex. `__sync_lock_release`): generera med `EXTRA_INC=work/postgres/src/include`; diagnostiker skriver vektortyper som `''`; TAP-testerna (`-Dtap_tests=enabled`, IPC::Run); Mach-O-länkning (ld64 i svansen).

### Status katalogstruktur (2026-11-09): inför x86-64
- `ccs/vcc/` i komponentkataloger (`lib/`, `lex/`, `pp/`, `cc/`, `emit/`, `as/`, `obj/`, `ld/`, `driver/`, `vccd/`), allt CPU-specifikt i `arch/a64/` (emitterns 18 filer, as-parsern, kodaren, fixups, länkarens relokeringar och thunks), OS-specifikt i `platform/{linux,darwin}/` (ELF- och Mach-O-skrivarna), fas 1-verktygen i `ccs/phase1/`. Datastrukturer i `lib/`: identiska kopior sammanslagna (`vreserve` ×4, `vregion` ×2, `vbuf` ×2), övriga varianter sida vid sida (TODO.md). Moduler över flera kataloger: Makefile-listor `MOD_SRC`, fortfarande en unity-TU per modul (cc, pp, emit, as, obj). AGENTS.md "Layout" beskriver strukturen.
- **Acceptans, varje commit:** 1 326 utdata (`-c` -O0/-O1/-O2, `-S`, `-E`; kärn-TU:er, gcc-torture, tcc, vcc-tester; Linux- och Darwin-mål) byte-identiska med bygget före; `vbench.py check --base` 171/171 (även vmlinux-länkningen); vcc-ctests 1729/1729, paritet 873/873, sema 878/879; gate inom ±0,1 % i alla steg.
- **Kvar:** länkaren som delad modul (dess arm64-delar är `.inc`-fragment), Darwin-grenarna inne i emittern (`DARWIN(c)`) till `platform/`, gemensamma datastrukturer. Redan före: steg 3 av `make self-check` kraschar (steg 2-`vpp` segfaultar), vasm saknar `ldursh` (vcc-asdiff 839/840).

## Linux aarch64 som värd (påbörjat 2026-11-08)

**Mål:** hela verktygskedjan (vcc, vccd, vld, vas) och kärnbygget körs på Linux aarch64
med samma resultat som på macOS. Testmiljö: Debian 13 arm64 i Docker Desktop
(`env/linux/`), som på M1 kör en Virtualization.framework-VM med native arm64, alltså
ingen emulering. Samma image fungerar i en qemu-system-aarch64-gäst med `-accel hvf`.

**Status:**
- Klart: `make -C ccs vcc` bygger på båda (Linux: `-D_GNU_SOURCE`, `-lm`, `-mcpu=apple-m1`
  på Apple-kärnor, annars `-mcpu=native`, utdata i `build/linux/ccs`). vccd använder
  epoll + signalfd i stället för kqueue. QoS, `sysctlbyname` och kperf (pmc.c) ligger
  bakom `__APPLE__`; pmc är en stubbe på Linux (perf_event_open återstår).
  `ppdata.inc` hade en icke-numerisk rad (`__has_cpp_attribute(clang::…)`), som
  gen-ppdata.sh nu filtrerar bort.
- Harnesses hittar LLVM och byggkatalog via `bench/hostenv.py`.
  `env/linux/run.sh CMD` kör i containern, och `--seed-work` kopierar kärnträden till
  volymen `vroom-work` (Docker Desktop kan inte dela work.sparsebundle-monteringen).
- Verifierat i containern mot Mac-trädet (build-o0): ctests 1706/1706 (2 fler ref-fail
  än på Mac: clang 23.1.6 mot 23.1.4) och kärnparitet 873/873. Mac-trädets host-TU:er
  behöver macOS SDK, så de testas i stället i Linux-trädet nedan.
- Prestanda (kernel/sched/core.o, -fsyntax-only, bästa av 5): macOS 115 ms, container
  138 ms med `-march=armv8.4-a`, 125–128 ms med `-mcpu=apple-m1`. Resterande ~10 % är
  troligen 4K-sidor (linuxkit) mot 16K på macOS, plus glibc malloc/memcpy.
  ctests: container 79 s, macOS 105 s.

- Linux-byggträd (2026-11-08): `work/build-linux` i volymen, byggt med clang 23.1.6 och
  env/vroom.config (efter `olddefconfig`, som bara lägger till CC_CAN_LINK och RELR).
  Kommando: `env/linux/run.sh python3 bench/kbuild.py --dotconfig env/vroom.config
  --out /vroom/work/build-linux -j 8`, som tar 97 s med ren clang. Referenserna ligger i
  `work/ppref-linux`. Harnesses väljer träd via `bench/hostenv.py` (KTREE/PPREF/ASREF).
- vcc utan `--target` på Linux betyder hostat aarch64-Linux, alltså glibcs
  sökvägar och clangs resurskatalog (VCC_RESDIR, satt vid bygget). PIE och CFI är på som
  standard. NEON-makron definieras utan `-mgeneral-regs-only`, vilket också rättar
  kärn-TU:er och .S som saknar flaggan. `-fpic`/`-fpie` tar ut varandra som i clang.
- gen-ppdata.sh tar `EXTRA_INC` (en kopia av glibcs /usr/include i `work/glibc-include`),
  vilket gav 213 nya nollskilda svar (`__builtin_*f128`, `__builtin_stdc_*`,
  `__has_extension(__attribute_deprecated_with_message__)`, ...). Numeriska svar med
  L-suffix behålls.
- I/O: källor, byggträd och referenser ligger på ext4 (volymen vroom-work), och
  `build/linux` på volymen vroom-build. `/tmp` är tmpfs. Bara repots källfiler läses över
  virtiofs. ctests: 79 s över virtiofs, 64 s på ext4.
- Verifierat på Linux: kärn- och hostparitet 920/920, vcc-sema run och layout 887/887
  (median 94 ms), ctests 1706/1706, vcc-diag 63/63.

- **vccd-kärnbygge på Linux (2026-11-08):** `env/linux/run.sh sh bench/vccd-kbuild.sh
  /vroom/work/build-vccd -j 8` (work/linux-pf, även den i volymen). vroomd byggs mot
  Debians LLVM 23 (libclang-cpp23-dev). Verktygsvägar på Linux kommer från
  `VROOM_LLVM_BIN` (Makefile: LLVM-katalogen för `clang` på PATH) i vccd, vroomd och
  vroom-cc. vroom-ld letar på PATH. caps.c svarar `-print-file-name=include` med
  VCC_RESDIR. Rättat: vroomds barn ärvde `SIGCHLD = SIG_IGN`, så Linux skördade
  driverns cc1/ld själv och `waitpid` gav ECHILD; barnet återställer nu SIG_DFL.
  Före merge av main (3ec10bf: ccsnap-återställda TU:er får egna span- och case-tabeller)
  gav bygget på Linux slumpvisa "duplicate case value" och en glibc-malloc-assert, och på
  macOS märktes inget. glibcs allokator återanvänder frigjort minne snabbare.
  - Resultat: **877 kompilerade av vcc, 0 till clang** (två körningar). Kallt bygge
    **8,2–8,3 s** (user 6 s, sys 2 s). Samma kod och träd på macOS med -j 8: 12,5–13,9 s
    (user 9–10 s, sys 17–18 s, last ~4 från andra sessioner). Skillnaden är
    systemtiden: process-spawn och fil-syscalls är billigare i Linux-VM:en. Image från
    Linux **bootar Debian till `running`, 0 felande enheter** (`results/boot-20261108-184214.log`).
  - Kärnobjekten är byte-identiska med Mac-bygget, utom versionssträngen (`7.4.0-rc1` mot
    `-rc1+`: .git saknas i volymen) och vDSO:n (clang 23.1.6 mot 23.1.4 via vroomd).

**Nästa:**
1. Profilera bygget på Linux (perf finns; pmc.c via perf_event_open), nu när
   systemtiden inte dominerar.
2. Mät en 16K-sidkärna (egen VM-kärna) mot linuxkits 4K.
3. vroom-build.sh (zsh, perl för tid) portabelt, och shimmen statisk utan libc-init på
   Linux.

## Gemensam optimerare utan SSA

### Beslut och kodläge (2026-11-11)

Implementationsplan efter x86-auditen i `PLAN-x86.md` X4. Detta avsnitt
äger de gemensamma optimeringarna; x86-planen äger målens instruktioner,
ABI, kodning och inkoppling. Ingen ny optimering är implementerad av denna
plan. G5:s kvalitetsmål och G7:s kompileringstid måste mätas separat.

Kodgranskningen är gjord efter rebase på main `4daebd9`; auditens siffror
gäller uttryckligen dess äldre bas `52261ef`. Nästa kodsteg bygger och mäter
en ny bas innan någon prestandajämförelse. De gamla resultaten skrivs inte om.

- `emit/tu.c` väljer tre generatorer. `emit/walk/` används bara av x86;
  `walk.h` har hårdkodat `LO(name) x64_##name`. `wkarg` och dess
  SSE/eightbyte-klasser är också SysV-specifika. Katalogens namn betyder
  därför inte att arm64/wasm redan använder den.
- `emit/prepass.c` har redan `luse`, `lnoreg`, `lspan`, `sflags`,
  `fnsize`, skrivnings- och adressflyktsfakta. Återanvänd dem, med
  konservativ komplettering där de inte bevisar vad en ny regel behöver.
- arm64:s `choose_lregs`/`bind_lreg` håller utvalda skalärer i register.
  `arch/a64/emit/opt.c` har block/liveness, kopior, stackceller, kända
  utvidgningar och döda resultat. `finish_function` väljer ram och sparade
  register efter städningen. Det är befintlig funktionalitet att extrahera,
  inte en ny optimerare att skriva en gång till för x86.
- x86:s `wk_arith`, `wk_decl`, `wk_const_if`, `wk_can_inline` innehåller
  semantiska regler; `x64_finish` kör bara `x64_twoaddr` och skrivaren.
  `rec.h` har ett 16-byte-format och `opdesc`, men ingen inkopplad
  gemensam passmotor eller fullständig x86-op-tabell.
- Wasm håller redan lämpliga skalärer i wasm-lokaler. Dess operandstack
  och strukturerade kontrollflöde kräver inte fysisk registerliveness.
  Dela C-regler, analyser och strategier med wasm; tvinga inte in dess
  värden i en registermaskins representation för att kalla koden gemensam.
  Konkreta anslutningar: `wexpr.c` har `wbinary`/`warith`, `wknown`,
  `wlocal` och `wvlanes`; `wstmt.c` har `wswitch`, `wlabels`, `wterm` och
  strukturerade grenar inklusive `br_table`; `wcall.c:wcall` har anropen.
  Dessa överlappar redan x86/arm64 i semantik, men inte i värderepresentation.

### Gränser mellan gemensamt och mål

| Gemensamt ansvar | Föreslagen plats | Målets ansvar |
| --- | --- | --- |
| Typkorrekta algebraregler, konstantdivisionens parametrar | `emit/opt/identity.c`, `divpow2.c` och separata regelfiler | Shift/mask/mulhi/div, immediatgränser och kostnad för en sekvens |
| Nåbarhet, bevarade sidoeffekter, lokala fakta | `emit/opt/flow.c`, befintligt `prepass.c` | Hur gren/etikett/retur emitteras |
| Inliningens behörighet och budget | `emit/opt/inline.c` | Argument/resultat, register, ABI och målbegränsningar |
| Urval av lokaler som kan bo utanför minnet | `emit/opt/locals.c` | Fysiska register eller wasm-lokaler, callee-save, prolog och spill |
| Switchens sortering, densitet och strategival | `emit/opt/switch.c` | Trädgrenar, kod-/värdetabeller, relokeringar eller wasm `br_table` |
| Blockgränser, kopior, döda resultat, stackceller, utvidgningsfakta | `emit/rec/` | Op-beskrivningar, faktaöverföring för målinstruktioner, säkra omskrivningar |
| Adressens bas/index/skala/displacement och typbredd | Gemensam adressbeskrivning vid `lval`/walk | x86 SIB/LEA, arm64:s index-/offsetformer, wasm-adressberäkning |
| Explicit vektoroperations semantik och fallback | Gemensam regel vid vektoruttrycket | SSE2/NEON/wasm SIMD, målfeatures, alignment och ABI |
| Tail calls: semantisk tillåtlighet | Gemensamt retur-/anropsbeslut | Stacklayout, ABI-kompatibilitet och epilog + jump |

`emit/opt/` är rena emitterhjälpare, inte en ny graf/IR eller ett nytt
optimeringssteg över hela AST:t. Små beslut/sekvensbeskrivningar får
returneras utan allokering. De tar typer, flaggor och explicit anropskontext;
ingen modifiering av delat AST, `ccsnap` eller funktionskroppar som flera
trådar/inlininginstanser använder. Reglerna får inte känna x86-opkoder,
registernummer eller `DARWIN`. Målanpassningen får inte kopiera reglerna.

**Tre konsumenter är leveranskravet.** En generell heltals-/flödesregel
är inte klar när den bara anropas från x86. Koppla in och testa den i
x86, arm64 och både wasm32/wasm64 för typer som målet stöder. Ett saknat
wasm-verktyg rapporteras som overifierat, inte som ett godkänt steg.
Registermaskinens postpass kräver två konsumenter (x86 och arm64); wasm
har i stället samma semantiska regel och en egen strukturerad sänkning.

Det befintliga AST:t och maskinposterna behålls. `ins` och `rec` har samma
fältpositioner men olika betydelse: arm64:s `sz` är inte x86:s bytebredd,
och stores använder olika registerfält. Använd uttryckliga adaptrar; gör
ingen aliasande cast mellan struct-typer och anta inte samma semantik av
att båda är 16 byte. `emit_SRC` i `ccs/Makefile` behöver ta med nya
`emit/opt/*.c` och `emit/rec/*.c`; varje fil skall fungera även utan unity.

### Ordning, leveranser och verifiering

OPT0–OPT4 har levererade delsteg med verifiering och kostnadsmätningar nedan;
OPT5:s gemensamma cell-/värdekärna finns i main (PR46), medan wasm-adaptern
ligger i PR47. Varje rad beskriver hela återstående leveransen, inte ett löfte
om att hela steget redan är färdigt. Statusblocken anger faktisk omfattning.
Fortsätt med små separata commits och uppdaterad status/mätning här.

| Steg | Leverans och beroende | Bevis innan klart |
| --- | --- | --- |
| OPT0 | Ny byggd bas efter rebase; dela probe-förväntningar i C-semantik och per-mål-kodkrav. Utöka auditens anropare med befintliga arm64/wasm-runners utan krav på att alla referenser finns i CI. | Befintligt x86-resultat reproducerat; tillgängliga mål kör samma definierade indata. Separata resultat för optflagga och mål. Inget gammalt mätvärde ges en ny revision. |
| OPT1 | Extrahera och koppla in gemensamma heltalsregler i alla tre generatorerna: `+0`, `-0`, `*1`, bitidentiteter, unsigned `/1`, `/2^k`, `%2^k`. Återanvänd befintlig vikning och normalisering. Kräver OPT0. | Semantiska tester på x86/arm64/wasm; riktade shift/mask-krav där målet stödjer dem, negativa tester för volatile och sidoeffekter. Oförändrad kod för exempel utan regeln. |
| OPT2 | Gemensamma effekt- och postkontrakt, komplett op-tabell för x86 och adapter för arm64. Extrahera blockidentifiering/kompaktering ur arm64; första generiska städning tar bara säkra fallthrough-hopp och död kod efter terminator. Kräver OPT0. | Arm64:s extraktion byte-identisk; x86:s onödiga hopp/döda arm försvinner; asm goto, `&&label`, switch/case i if(0), noreturn, VLA/cleanup och debug-remapping korrekta. |
| OPT3 | Flytta kandidaturvalet ur `choose_lregs` till gemensam `locals.c`, koppla till x86:s bindningar. Begränsad top-K efter befintliga loopviktade `luse`; målet anger K/register. Wasm återanvänder behörighetsreglerna, inte en artificiell registergräns. Kräver OPT0 och effektkontraktet i OPT2. | Parametrar/lokaler slutar gå genom stacken i proberna; blandade anrop, rekursion, fasta asm-register, varargs, returns_twice, överjusterad ram, VLA och registertryck. Arm64:s ursprungliga val och utdata identiska i extraktionscommitten. |
| OPT4 | Återanvänd arm64:s kopie-/dead-result-/utvidgningsalgoritmer i `emit/rec/`, först per block på x86. Måltabeller beskriver registeröverlapp, flaggor och omskrivningar. OPT2 krävs; OPT3 ger större kodvinst men är inte ett krav. | Positiva och negativa kodkrav; inga fel över anrop/asm/blockgränser. 32-bitars självmove på x86 tas bara bort om övre halvan redan är noll. FP- och implicita register/flags bevaras. |
| OPT5 | **Pågår: gemensam cell-/värdekärna; wasm4/8-byte forwarding/överskriven-store-DSE implementerad, x86 kostnadsoptimeras.** Extrahera arm64:s stackcellsalgoritm och anslut x86 via explicit allokerings-/accessmetadata. Återanvänd samma algoritm för load forwarding och DSE. Kräver OPT2/OPT4; välj exakta icke-flydda frame-celler först. | Arm64:s t8/t10-egenskaper portade till C-regressioner: scope-återanvändning, överlapp/union, smala typer, adressflykt, pekare precis efter objekt, anrop/asm/atomics. Volatila läsningar/skrivningar kvar. |
| OPT6 | Gemensamt switchval och utökad inliningbehörighet. Separata commits: först balanserade switchträd och värdetabeller; sedan små vanliga statiska funktioner och oanvända lokala initierare. Kräver OPT1/OPT2; inliningens stacktrafik motiverar OPT3/OPT5 först. | Täta/glesa/signed/range-case, overflow vid densitetsräkning och computed goto. Inlining: recursion, labels, VLA, cleanup, asm-immediater och sidoeffekter. Oanvänd init tas bara bort när både objektet och init saknar observerbar effekt. |
| OPT7 | Gemensam beskrivning för skalade adresser, direkt explicit vektoroperation och store-constant-hook med scalar/register-fallback. Därefter små målspecifika regler för LEA/SIB, NEON/SSE2 och instruktionsfeatures. Kräver OPT1; metadata från OPT2 följer med. | Samma C-resultat på målen; målkrav visar indexerad load/packed add/store-immediate. Negativa alignment-, bredd-, alias-, lane- och featurefall; baseline får aldrig native-instruktioner av misstag. |
| OPT8 | Gemensam konstantdivisionsalgoritm för signed tvåpotenser och övriga konstanter; targetsänkt mulhi/shift eller normal div enligt målbudget. Begränsade tail calls med gemensam kontroll och ABI-hook i egen commit. Kräver OPT1 och, för tail calls, OPT2/OPT3. | Division/remainder 32/64 bitar, negativa tal, extremvärden, slump och gränser kring multipler. Tail calls: inga pekare till egen ram, stackargument, cleanup eller ABI-konverteringar utan uttryckligt stöd. |

**Första kodcommit:** OPT0 + extraktion av en enda heltalsregel i OPT1,
anropad av x86, arm64 och wasm32/wasm64 för stödda typer. Det
visar delningen i faktisk användning innan fler regler läggs till. OPT2
kan förberedas därefter; registerarbetet OPT3 har högre prioritet än
mer aggressiv inlining. Ingen stor flytt av hela arm64-generatorn krävs
för att börja dela regler eller postpass.

### OPT5 status: gemensam cellanalys (2026-11-11)

`emit/rec/slots.c` och `slots.h` innehåller nu arm64:s analys av exakta
allokeringsceller, överlapp och adressflykt. Arm64 använder den direkt;
registerversioner, liveness och själva load/DSE-omskrivningen ligger än
så länge kvar i dess adapter. Ingen ny funktionsvandring eller separat
post-/effekttabell har tillkommit. Scratchkapacitet återanvänds per tråd.
Allokerings-ID skiljer livstider vid samma offset. ID 0 betecknar en
omärkt/observerbar åtkomst och spärrar alla överlappande celler, även om
ingen laddning av cellen överlever. Båda escape-ID:n vid one-past behålls.
Felaktiga bredder/intervall avvisas före offsetaritmetik.

Verifierat på bas main9d022c3: unity och fil-för-fil bygger; 177/177 egna
arm64-körtester. 6069/6069 objekt-/textresultat är byte-identiska mot
eddce37 (2023 filer × O1-objekt/O2-objekt/O2-text; dess arm64-utdata var
oförändrade av kopieextraktionen i PR43). Ett separat ASan/UBSan-kontrakt
jämför analysen med en enkel intervallmodell över 400 deterministiska
48-accessfall, samt explicita livstids-, gräns-, volatile- och escapefall.
Kontraktet körs i CI. En andra agent har granskat extraktionen.

Avgränsad kostnadsmätning: båda kompilatorerna byggda med clang 23,
`-O3 -march=x86-64-v3`, Callgrind 3.26, hela `vpp -O2 -S` till arm64,
`VCC_LREGS=0` för att verkligen belasta slotanalysen. Bas 9d022c3 och
kandidat 36fe740, 500 funktioner per korpus:

| Korpus | Bas, Ir | Kandidat, Ir | Skillnad |
| --- | ---: | ---: | ---: |
| summeringsloop | 35 284 633 | 35 515 725 | +0,655 % |
| rak heltalskod | 17 330 749 | 17 383 341 | +0,303 % |

Assemblern är byte-identisk för båda proberna. Råprofiler, binärhashar,
indata och mätkommandot finns i `work/opt-profile/cells-main-*`,
`cells-main-profile.json` och `cells-measure.py`. Dessa två instrumenterade
prover är inget generellt godkännande av G7 eller en hårdvarucykelmätning.

Nästa kodsteg: flytta värdetillgänglighet och DSE-beslut till en gemensam
motor med målens befintliga read/write/blockdata; x86 måste först bära
stabila allokerings-ID:n och volatile/atomic-fakta från walkern. Wasm
behöver stabila värde-ID:n och strukturerade minnesoperationer före
textemission. OPT5, G5/G7 och hela O2-målet är fortsatt öppna.

### OPT5 status: gemensamma värde- och storefakta (2026-11-11)

`emit/rec/slot-values.c` delar nu arm64:s värdetillgänglighet och
besluten för överskrivna/olästa lagringar. Målet äger värde-ID och
skrivversioner: ett native-register eller en stabil wasm-lokal ryms i
samma uint32-kontrakt. Blockgenerationer hindrar vidarebefordran över
joins utan analys; globala kvarvarande laddningar räknas över alla
block. Escape kräver också samma minnesversion, och hindrar DSE.
Ingen SSA eller ny post-/effekttabell. Befintlig scratch för värdefakta
och load-räknare har slagits ihop till en återanvänd per-cell-tabell.

På eeac13c-basen: 6108/6108 arm64-objekt/text byte-identiska (2036 filer,
O1-objekt/O2-objekt/O2-text), 190/190 egna körtester samt nytt stack_cells
körfall. Isolerade ASan/UBSan-kontrakt testar blockgränser, källclobbers,
minnesversioner, lästa/vidarebefordrade loads, stora osignerade värde-ID:n
och återanvänd kapacitet; en andra agent har granskat adapterflytten.
X86-/wasm-adaptrar och kostnadsmätning av kombinationen pågår i separata
arbetskopior; detta är fortsatt delprogress i OPT5, inte full O2-täckning.

### OPT5 värdekärna: kontroll mot mergad main (2026-11-11)

Bas f866a31 innehåller nu PR43 och PR45. Unity och fil-för-fil bygger;
6117/6117 arm64-objekt/text är identiska mot basen (2039 filer ×
O1-objekt/O2-objekt/O2-text). Egen arm64-svit 193/193, inklusive nya
stack_cells; det nya körfallet passerar också med fil-för-fil-binären.
Cell-/värdekontrakten körs sanitiserat. Den portabla regressionen har
också verifierats O0–O3 på x86, arm64, wasm32 och wasm64 (16 körningar).

Callgrind 3.26, hela `vpp --target=aarch64-linux-gnu -O2 -S`,
`VCC_LREGS=0`, identiska clang23 `-O3 -march=x86-64-v3`-byggen:

| 500-funktionskorpus | Bas f866a31, Ir | Värdekärna e6bad4c, Ir | Skillnad |
| --- | ---: | ---: | ---: |
| summeringsloop | 35 612 350 | 35 511 064 | −0,284 % |
| rak heltalskod | 17 431 966 | 17 400 666 | −0,180 % |

Assemblern är identisk i båda proverna. Rådata, binärhashar, källor och
körkommando finns i `work/opt-profile/values-main-*`,
`values-main-profile.json`, `values-measure.py` och `{loops,control}-500.c`.
Detta mäter refaktoreringens instruktionsarbete på två avgränsade prover;
G5/G7 kräver fortfarande bredare kodkvalitets-/prestandagrindar.
X86:s metadatakostnad och wasm:s typade händelser ingår inte i denna mätning.

### Kontrakt som styr implementationen

- **Wasm-integrering:** aritmetikhjälparen returnerar ett typat beslut,
  inte ett `val` med fysiskt register. `wbinary` kan välja beslut innan
  operandstacken fylls; om en operand redan emitterats måste adapterkoden
  bevara rätt stackdjup och använda drop/local vid behov. Gemensamma
  faktabindningar är per funktions-/inlininginstans. Wasm-inlining kräver
  egna parameterlokaler, strukturerad resultat-/returblockhantering och
  remappade goto/cleanup/VLA-scope; delad policy ensam är ingen inliner.
  `wswitch` återanvänder strategivalet men behåller `br_table` och
  segment/goto-modellen. Wasm32 normaliserar pekare som 32 bitar även på
  denna 64-bitars värd. Explicit SIMD kräver målfeature; utan den behålls
  lane-fallback. Magic division utan billig mulhi och tail call utan
  aktiverad/stödd wasm-tail-call-funktion behåller vanlig div/anrop.
- **Typer och effekter:** första aritmetikreglerna gäller vanliga heltal
  upp till 64 bitar; `_BitInt`, 128 bitar, FP och pekare har explicit
  fallback tills deras regel verifierats. Använd operationens promoverade
  bredd och signedness; signed division avrundar mot noll. Tvåpotens-
  kontroll måste utesluta noll. Ingen FP-identitet eller fast-math-regel
  smygs in. Operander med anrop, volatile eller atomics utvärderas exakt
  som tidigare även när resultatet blir konstant. Valnivåregler efter
  operandgenerering behåller effekter; överhoppning på AST-nivå kräver
  separat effektbevis. Återkommande effektfrågor cacheas per instans eller
  tas ur förpasset, inte rekursiva omvandringar med kvadratisk kostnad.
- **Register:** gemensamt urval är policy, registerbindning är målmekanik.
  Börja som arm64 med ett fåtal callee-saved GPR för icke adressflydda,
  icke-volatila/atomiska heltal och pekare. x86 måste reservera dem från
  temporärpoolen och hantera r12 som rambas samt asm-clobbers. Använda
  callee-saved-register sparas/återställs enligt mål-ABI, inklusive
  unwind/debug. Återförsök är högst ett konservativt utan lokala register;
  kvarvarande tryck använder befintlig spill/felväg. FP och registerpar
  är senare separata utvidgningar. Lägg inte samtidigt ett konkurrerande
  generellt registercache ovanpå bindningarna.
  Wasm behåller även sina redan stödda flyttalslokaler; gemensamma
  behörighetsregler parametriseras av målets typstöd och får inte göra
  registermaskinernas första GPR-begränsning till en wasm-regression.
- **Posteffekter:** tabellerna behöver reads/writes inklusive implicita
  register, flaggor/partiella flaggskrivningar, minnesläsning/skrivning,
  volatile/atomic/order, trap/terminator/barriär, stackförändring och
  externa etikettinträden. Okänd instruktion/asm är konservativ. En load
  kan vara observerbar eller kasta fel även om resultatregistret är dött.
  Börja DCE med rena registeroperationer och bevisat säkra frame-läsningar.
  Anrop/TLS, x87-stack och x86:s delregister kräver egna beskrivningar.
- **Minnesidentitet:** återanvänd inte slot-offset som objektidentitet.
  Behåll stabilt allokerings-id, intervall/bredd, accessart och escape;
  byteoffset är 64 bitar efter mains ramändringar. Överlapp och
  one-past-adresser hanteras konservativt som arm64 redan gör. Föredra
  lat sidometadata i optläget framför att göra varje 16-byte-post större;
  byt inte ut arm64:s taggar förrän ersättningen är bytevis verifierad.
  Sidometadata måste följa insertion/kompaktering/twoaddr och debugrader.
- **Passkostnad:** opcode-tabell på snabbvägen, målcallback bara när en
  verklig omskrivning eller avvikande instruktion kräver det. Buffertar
  återanvänds per worker, utan lås och utan allokering per nod/post.
  Kör inte ett pass utan relevanta instruktioner/celler. Konstantstor
  registerfakta ger O(n); top-K är O(lokaler*K) med litet fast K;
  switchsortering O(k log k) och tabellstorlek har tak. Magisk division
  får O(bitbredd) per ny konstant och begränsad cache, inte sökning.
- **CFG-budget:** inga globala fixpunktsanalyser krävs för första x86-
  passen. Vid blockslut är okända live-out konservativt levande, inte
  döda. Senare delad CFG-liveness får separat arbetsbudget; om den tar
  slut används säkra fakta/utdata utan beroende omskrivningar. Ofärdig
  liveness får aldrig godtas som korrekt. Arm64:s befintliga analyser
  behålls tills delad ersättning ger minst samma korrekthet och uppmätt
  kostnad; en lokal variant får inte tyst försämra dess befintliga O1.
- **Kodordning:** semantikregler vid AST-sänkning; delade record-pass på
  giltiga effekter; x86 tvåadress efter delade pass; målspecifik städning;
  slutligt register-/rambeslut och kodning. Om tvåadress inför nya kopior
  räcker en avgränsad efterstädning, ingen generell fixpunkt. Debug/CFI
  får inte referera till gamla postindex eller borttagna sparningar.

### Filstruktur för optimeringar

Varje avgränsad optimering eller tätt sammanhängande regelfamilj får en
namngiven fil: nu `emit/opt/identity.c` och `emit/opt/divpow2.c`.
Gemensamma postpass får motsvarande filer under `emit/rec/`. Lägg inte
algoritmerna i en växande `walk.c` eller uttrycksvandrare. Vandrarna och
bakändarna behåller små anrop/adaptrar för operandutvärdering, typer,
register och wasm-stack. Undvik att dela upp en liten sammanhängande
regel i en fil per operator. Fil-för-fil-bygget verifierar gränserna.

### Nivåer, testkrav och mätning

`-O0` får billiga direkta sänkningsregler utan att starta dataflowpass.
Vilka regler som är default mäts, inte antas. O1 är de budgeterade
gemensamma förbättringarna ovan. O2/O3 förblir ärligt samma tills en
separat, mätt policy finns; ändra inte bara makron eller nivånamn för
att påstå Clang-paritet. LICM, global GVN, automatisk vektorisering,
loop-unrolling/unswitching och generell IPA ligger efter den första serien
OPT0–OPT8, men ingår i det fortsatta målet att täcka Clang/GCC O2 på
alla tre bakändar. Skapa IR/SSA först för de analyser som behöver det;
varje senare pass måste ha en mätt CPU-budget. Begränsad
blocklokal CSE kan läggas efter OPT4 när dess nytta motiverar en
värdecache med fast storlek; ingen helfunktionssökning eller generell aliasanalys.

Varje semantisk regel får C-regressioner i `tests/vcc` som kör på alla
stödda mål. Positiva kodkrav är arkitekturspecifika; negativa kontroller
för effekter är gemensamma. Upstream GCC:s C-fall är komplettering, inte
ersättning för kontroll av deras `dg-final`. Lägg en liten vcc-only
regressionskörning i `ci/premerge.sh` för mål där den kan köras; den
interaktiva trekompilatorsauditen skall inte bli CI:s beroende av native
GCC, GNU objdump eller alla valfria runners.

Rena extraktionscommits kräver byte-identiska utdata mot före-bas på
berörda mål, unity och nonunity. När regeln aktiveras får bara de
avsedda kodskillnaderna uppstå: kör suites/ABI/stress för berörda mål,
arm64:s slot/asm/extension-regressioner, och de CFI/DWARF-/kernel- eller
hostkontroller som förändringen faktiskt berör. Parser/pp ändras inte
för emitteroptimeringar. Om de ändå måste ändras gäller AGENTS.md:s
sema-/paritetsgrindar före ändringen. G1/G2 blir inte klara av denna plan.

Mät före/efter med korta körningar och högst `-j 6`: kompilatorns
instruktioner/cykler och minne, genererad kodstorlek/instruktioner och
riktig körtid på ett fast litet korpus. Redovisa O0/O1 och per mål,
inklusive uteslutna fall och saknade verktyg. Befintlig `vbench.py gate`
gäller; tills dess x86-stöd är bekräftat får den inte anges som körd
x86-grind. Godkänn ingen tänkt passbudget som en uppmätt siffra. Spara
basrevision, binärhash och mätresultat vid varje färdigt OPT-steg här.

### Status OPT0/OPT1 (2026-11-11): första delade aritmetikregeln

Ny bas byggd på `990c9fe` (main-kod från `4daebd9`); vpp SHA256
`b1d0c06a74de9695c8c04e67993c81e686d91d946afd8f32ecea169843af91f3`.
Basens x86-egensvit går 130/130, fem filtrerade fall. Basen sparades som
`build/linux/opt-baseline/vpp` före ändringen.

`emit/opt/divpow2.c:em_divpow2` äger nu en gemensam regel för unsigned
vanliga heltal upp till 64 bitar: division med konstant 2^k blir logiskt
högerskift och rest blir AND med 2^k−1. Divisorn normaliseras till
operationens promoverade typ före testet; noll, signed, pekare, FP,
vektorer, _BitInt och bredare heltal faller tillbaka. Ingen allokering,
AST-modifiering, SSA eller passloop; regeln gäller även O0.

Anslutningar: arm64:s centrala `arith`, x86-vandrarens `wk_arith`, och
wasms `wbinary`/`wassign`. Binära uttryck samt skalära `/=` och `%=`
omfattas. Registermålens operander är redan utvärderade; wasm byter bara
en bevisat effektfri konstant RHS och behåller övrig evaluation och
stacktyper. Vektorlaner med dynamisk representation och andra algebra-
identiteter är inte nya leveranser i detta steg.

`tests/vcc/unsigned_pow2.c` testar literalbaserade svar, 32/64-bitars
maxvärden/högbit, divisor 1, trunkerade/signed-konverterade konstanter,
signed/non-power/dynamiska kontrollfall, volatile-effekter, anrop,
comma-RHS och compound-tilldelningar. `bench/vcc-opt-rules.py` granskar
funktionerna separat vid O0–O3 och kan köra utan libc på alla fyra mål.
Wasm använder vpp:s egna C→objektväg och vlink, inte Emscripten; se
`PLAN-wasm.md` W9. En .s-fil genom vpp -c väljer i dag inte wasm-
assemblern, så testet kompilerar samma C med samma nivå till eget objekt.
Detta är en känd driverlucka, inte en LLVM-fallback i testet.

Kandidat SHA256 före dokumentation
`77c05982e3bc056476aeeac6cc02c3309cba10026db19ecbd5e3a846b1dde440`.
Kort interfolierad mätning på Ryzen 7 9700X: tre repetitioner, åtta
6000-funktions-TU:er, seriell batch, warm=1, O1 -S, utan header-/declcache.
Median för summan av jobbtiderna (ms, bas→kandidat):

| Mål | Tvåpotensaritmetik | Oförändrat kontrollkorpus |
| --- | --- | --- |
| x86-64 | 44,370 → 37,864 | 36,940 → 36,176 |
| arm64 | 57,737 → 54,005 | 53,619 → 53,747 |
| wasm64 | 34,475 → 35,131 | 32,468 → 32,616 |

Detta är väggtid i korta varma jobb, inte hårdvaruinstruktioner eller
bevis på en 1 %-grind: perf är blockerat (perf_event_paranoid=4) och
Linux-vperf ger noll. Ingen systeminställning ändrad. Små differenser är
brus; mätningen visar ingen stor kontrollregression. Källor, kommandon,
rådata och parade resultat finns i `build/linux/opt-perf/paired.json`.
Två minimala kodexempel, unsigned /8 och %8: x86 16→12 instruktioner
vardera; arm64 11→10 för division och 12→10 för rest; wasm32/64 får
shr_u/and i stället för div_u/rem_u med lika många stackoperationer.
Ingen påstådd förbättring av programmets körtid är uppmätt här.

Verifierat: kandidatens x86-egensvit 130/130 (samma fem filtrerade fall
som basen), arm64-egensvit 135/135 via QEMU, med -j 6 och --no-cache.
Unity och fullt fil-för-fil-bygge gröna; båda byggsätten klarar alla
16 kombinationer av fyra mål och O0–O3 i kod-/körharnessen. Systemets
Node 24.22.2 och därefter 25.9.0 klarar wasm32/64 utan SDK-sökväg.
Den nya kodgrinden avvisar före-basens division på alla fyra mål, så
den detekterar själva optimeringsluckan utöver korrekt C-resultat.
Syntaxkontroll av Python och shell samt `git diff --check` gröna.
GHA-hookar är tillagda men ingen fjärr-CI eller full Emscripten-baserad
wasm-svit har körts i detta steg.

OPT1 är därmed **påbörjat**, inte helt klart: identiteterna och övriga
OPT-steg återstår. Emscripten-oberoende wasm-testväg är levererad separat
från ersättningen av full runtime/explorer. G5/G7 är fortsatt öppna.

### Status OPT1 (2026-11-11): separata filer och heltalsidentiteter

Tvåpotensregeln har flyttats oförändrad till `emit/opt/divpow2.c`.
`emit/opt/identity.c` äger nu typkontrollerade RHS-regler för +0, −0,
*0/*1, /1, %1, &0/&alla-ettor, |0/|alla-ettor, ^0 och skift med noll.
Alla tre generatorer använder samma recept. Operandeffekter behålls;
32-bitars registerresultat normaliseras även efter lat trunkering.
FP, pekare, vektorer, _BitInt och bredare heltal omfattas inte.
Ingen allokering, ny IR eller analysloop införs.

Bas: `e4c1c9a`, vpp SHA256
`77c05982e3bc056476aeeac6cc02c3309cba10026db19ecbd5e3a846b1dde440`.
Kandidat före dokumentation:
`f3899f1f5e51ff895d15903f547797bf6096ba16c8f2016f00aa005e1c70178f`.
Verifierat unity och fil-för-fil: 32 kod-/körkombinationer (två regler,
fyra mål, O0–O3), utan Emscripten. Systemets Node 26.13.0 kör wasm.
X86-egensvit 131/131 (samma fem filtrerade fall), arm64 136/136 via QEMU,
-j 6 och --no-cache. Regressioner täcker smala typer, högbitar efter
trunkering, signed extremvärden, volatile och exakt en sidoeffekt.

Kort parad kompilatormätning finns i
`build/linux/opt-identity-perf/paired.json`. Stort värdtidsbrus och
blockerade hårdvaruräknare hindrar slutsatser om små CPU-skillnader;
ingen 1 %-grind eller programkörtidsvinst påstås. Minimala /1-funktioner
minskar från 16→12 instruktioner på x86, 11→10 på arm64 och 9→7 på
wasm32/64. Kontrollkorpus och rådata sparades separat.

OPT1:s identiteter och unsigned tvåpotenser är levererade. Detta slutför
inte OPT2–OPT8 eller hela O2-målet; nästa steg är separat gemensam
fallthrough-städning med bevarad debug-remapping.

### Status OPT2 (2026-11-11): delad fallthrough-städning

`emit/rec/fallthrough.c` delar nu arm64:s befintliga borttagning av
ovillkorliga hopp till den direkt följande följden av lokala etiketter.
X86 kör samma pass vid O1 och uppåt före tvåadresskonverteringen.
Postlayouten beskrivs med stride/fältoffset; label läses med memcpy så
arm64:s `ins` aldrig typ-punnas till x86:s `rec`. Asm och namngivna
etiketter är barriärer. En linjär framåtsvepning, ingen allokering eller
fixpunkt. Senare borttagningar kan exponera tidigare hopp; kaskadfallen
är fortsatt körtestade men utlovas inte borttagna i detta steg.

Wasm:s befintliga konservativa `wlabels`/`wterm` har fått egen fil,
`arch/wasm/emit/reachability.c`. While emitterar inte längre en extra
loopgren efter en kropp som bevisligen inte kan falla igenom. Explicit
continue har kvar sin gren och cleanup; do-villkor och for-inkrement
bevaras. Detta är strukturerad målsänkning, inte ett artificiellt
maskinpostpass på wasm.

Bas `f7ccaff`, vpp SHA256
`f3899f1f5e51ff895d15903f547797bf6096ba16c8f2016f00aa005e1c70178f`;
kandidat före dokumentation
`384cfd5f6c83fba8852dbc9ff0c1e7ad3ce323a489519164d304d55fcc8b36eb`.
Arm64:s 131 befintliga kompilerbara egensvitfiler ger byte-identisk
O1 -S med -g och unwind-direktiv mot basen; fem tidigare avvisningar är
oförändrade. De två nya testfilerna undantogs från den jämförelsen då de
fortfarande redigerades när basen togs. Båda byggsätten klarar 64
mål-/nivå-/regelkombinationer, med målrelevanta kodkrav och körning på
alla fyra mål. X86-egensvit 133/133 (samma fem filtrerade), arm64
138/138, -j 6 --no-cache. O1 DWARF-verifiering, oförändrad .text med -g
och direktobjekt mot assemblerutdata gröna för båda nya testfilerna.
Befintliga CI-anrop inkluderar automatiskt de nya reglerna.

Genererad kod vid O1, antal ovillkorliga grenar i respektive nya testfil:
x86 `branch_fallthrough.c` 31→21, arm64 20→20 (ren extraktion);
wasm32/64 `while_backedge.c` 44→38. Körsemantik täcker bakåthopp,
label-följder, switch-fallthrough, sidoeffekter, asm-barriär, goto in/ut
ur loop, cleanup och continue.

Korta parade mätningar (tre repetitioner, fyra 3000-funktions-TU:er,
-j 1 --warm 1, O1 -S) finns med bas/kandidathash och rådata i
`build/linux/opt-flow-baseline/paired.json`; riktad ommätning i
`recheck.json`. Första medianen för flödeskorpus i ms var x86
25,586→25,436, arm64 29,313→29,784, wasm32 24,381→28,059 och wasm64
22,922→22,644. Kontrollkorpus sparas separat. Ommätningen varierar
kraftigt, särskilt wasm32; ingen liten CPU-regression eller vinst kan
fastställas och 1 %-grinden är inte bevisad. Hårdvaruräknare fortsatt
blockerade; inga systeminställningar ändrade. Programkörtidsvinst ej mätt.

OPT2 är endast **påbörjat**: full effektbeskrivning, blockkontrakt och
död kod efter terminator återstår. Registerlokaler (OPT3) och resterande
OPT-steg/O2-målet är fortsatt öppna.

### Status OPT2 (2026-11-11): onåbara poster och x86-effekter

`emit/rec/unreachable.c` tar i en linjär, allokeringsfri svepning bort
posterna efter en säker ovillkorlig transfer fram till nästa möjlig
inträdespunkt. Arm64:s adapter ligger separat i
`arch/a64/emit/unreachable.c`; x86 har en billig enbyte-flödestabell i
`arch/x64/emit/effects.c`. O1 och uppåt, med befintlig noopt-spärr.
Alla etiketter, raw asm, namngivna etiketter, okända instruktioner och
indirekta transfers är konservativa barriärer. Debug-remapping körs för
varje gammalt postindex och slutmarkören. Wasm:s strukturerade dödkod
använder redan motsvarande AST-bevis; dess semantik körtestas också.

Arm64 BRK är **inte** en säker terminator: debugger/arm_trap kan återgå.
Granskningen fann även att gemensamma side_effects saknade DEBUGTRAP;
den klassas nu som effekt så ett oanvänt debugtrap-anrop behålls.
Regressionerna kontrollerar både breakpoint och fortsättning utan att
exekvera dessa compile-only-funktioner.

X86:s fulla register-/effektkontrakt för kommande kopie-/livenesspass
finns i samma målspecifika effects.c. Det beskriver implicita register,
flaggor, partial writes, TLS, REP, x87, FP-miljö, R11-scratch och R12 som
justerad rambas. Okänd/saknad metadata blir opak, aldrig pure.
`bench/vcc-rec-effects.py` testar dessa kontrakt normalt och med
ASan/UBSan; det normala snabba testet ingår i arm64:s premerge-jobb.
Ingen full registeranalys startas för enkel flödesstädning.

`unreachable_records.c` testar goto/return/break/continue, nåbara effekter,
volatile, computed goto, asm goto, asm-barriärer, cleanup och VLA.
Kodkrav avvisar basens x86-kod; literalbaserade körresultat bevaras på
alla fyra mål vid O0–O3. Auditens x86-probe `oa_cond_fold` går vid O1/O2
från 16 till 11 instruktioner. Båda byggsätten och relevanta DWARF-test
verifieras tillsammans med den följande signed-tvåpotensregeln; fulla
mätdata och revisionshashar redovisas vid OPT8 nedan.

OPT2 är fortfarande **påbörjat**: delade block-/effektadapter för hela
arm64-optimeraren och full CFG-nåbarhet återstår. Detta är inte generell
SimplifyCFG. Nästa större steg är registerlokaler med gemensam behörighet
(OPT3), därefter kopie-/utvidgningsstädning (OPT4).

### Status OPT8 (2026-11-11): signed tvåpotenser med målkostnad

`emit/opt/sdivpow2.c` beskriver en gemensam regel för signed division och
rest med positiva/negativa konstanta tvåpotenser upp till 64 bitar,
inklusive minsta divisorn och −1. Bias före aritmetiskt skift ger C:s
avrundning mot noll. Rester beräknas från lågbitmask och samma bias.
Magnituden räknas i i128 för att inte ge overflow i kompilatorn.
Separata `sdivpow2.c` i arm64, walk och wasm äger sänkningen; anropen i
uttrycksvandrarna är små. Ingen ny analys, allokering, SSA eller AST-mutation.
_BitInt, bredare heltal, pekare, FP och vektorer omfattas inte.

**Wasm:s kostnadsval:** direkt jämförelse med lokal clang 23 -O2 visar
att LLVM lämnar /8 och %8 som en div_s/rem_s. Därför expanderar wasm
inte den långa biassekvensen. Den använder samma recept för billig /−1,
%−1 och quotient med MIN-divisor (likhetsjämförelse, i64 utvidgas från
i32). Övriga signed tvåpotenser behåller kompakt wasm-kod för motorn.
Det är ett målbeslut, inte saknad C-semantik. Pure konstant RHS krävs
fortfarande i wasm; en effektfull comma-RHS utvärderas normalt.

Tester: `signed_pow2.c` täcker 32/64 bitar, båda divisortecknen, skift
1/3/4/30/31/62/63, extremvärden, −1 (utesluter MIN/−1 som är C-UB),
smala promoverade typer, lat trunkering med skräp i högbitarna,
compound/volatile, exakt en sidoeffekt och nästlade temporärer.
Referensresultat räknas med volatile dynamiska divisorer, så provet
jämför maskinens divide-väg mot den nya konstantsänkningen.

Verifiering för OPT2/OPT8 tillsammans: unity och fil-för-fil bygger utan
nya varningar; båda klarar alla 96 mål-/nivå-/regelkombinationer med
kodkrav där relevanta och körning på x86, arm64, wasm32 och wasm64.
X86-egensvit 135/135 (fem tidigare filtrerade), arm64 140/140, -j 6 och
--no-cache. O1 DWARF-test för nya flödes-/divisionsfall gröna; metadata-
kontrakt testade med ASan/UBSan. Ingen fjärr-CI eller full wasm-runtime
med Emscripten har körts. Varken G5/G7 eller hela O2-målet är klara.

Mätbas för serien: `98da0c3`, vpp SHA256
`384cfd5f6c83fba8852dbc9ff0c1e7ad3ce323a489519164d304d55fcc8b36eb`;
slutkandidat före dokumentation
`294cc2a1264fdee3a21c413e826d1ab5062b12a4076c51178882022ed21691a9`.
`build/linux/opt-next-baseline/paired.json` innehåller tre alternerade
repetitioner, fyra 3000-funktions-TU:er per batch, O1 -S, -j 1 warm=1.
Utöver jobbtid sparas barnprocessens user+sys CPU-tid, inklusive warmup
men utan väntan på andra processers schemaläggning. CPU-tid är inte
hårdvarucykler och varierar också med värdens frekvens/cache/minneslast.
Medianer för signed-korpuset i ms (bas→kandidat): x86 46,668→53,825,
arm64 46,220→52,126, wasm32 46,286→41,464, wasm64 40,145→48,886.
Flödes- och oförändrat kontrollkorpus samt alla råa repetitioner sparas
separat. Native biasexpansion genererar fler enkla poster och har en
mätbar kostnad i det avsiktligt divisionstäta mikrokorpuset. Ingen
1 %-grind eller generell frånvaro av kompilatorregression påstås.
Tidigare wasm-biasexpansion förkastades till förmån för kompakt kod.

Trekompilatorsauditen före/efter finns i samma katalog som
`optaudit-before/` och `optaudit-after/`. Alla 12 baslinjekombinationer
har samma checksumma `1922a5c83358247b`. X86 signed-/8-proben har vid
O1 lika många instruktioner (14) men divide går 1→0; onåbar arm i
`oa_cond_fold` går 16→11 instruktioner. Auditen efter är från kandidaten
före den separata debugtrap-korrigeringen; slutlig fokuserad verifiering
omfattar korrigeringen. Programkörtidsvinst är inte uppmätt.

OPT8 är **påbörjat**: övrig konstantdivision (mulhi), gemensam
kostnadsmodell, samt tail calls återstår. Fortsatt full O2-plan kräver
även loop-/alias-/globala analyser efter OPT0–OPT8. OPT3 bör nu minska
stack-/kopietrafiken före mer avancerade pass.

**Förberedelse OPT3:** kodgranskning visar att x86 kan använda
RBX/R13–R15 för lokaler; R12 tillhör justerade ramar. Gemensamt urval
återanvänder lspan/luse/lnoreg. Befintliga L_VREG kan användas, men läsning
måste kopiera till en ägd temporär och normalisera smala typer. Alla
potentiellt promoverade callee-save-register måste dessutom sparas av
builtin setjmp även när setjmp-funktionen själv inte väljer dem: annars
kan longjmp förbi en promoverad mellanram förstöra anroparens lokaler.
Detta och asm-clobbers/inline-instansgränser är krav före aktivering.

### Status OPT3 (2026-11-11): gemensamt urval och x86-registerlokaler

`emit/opt/locals.c` äger nu gemensam behörighet och stabilt viktat top-K
med befintliga lspan/luse/lnoreg. Ingen ny AST-vandring, graf eller
allokering. Arm64 behåller sitt fysiska registerval och wasm sina
representationskrav, inklusive FP/i128/SIMD-lokaler. Gemensamma undantag
är adressflykt, volatile, atomic, VLA, cleanup och fasta asm-register.
Prepassens adressmarkering följer nu även comma-RHS som emitterarnas
lvalue-sänkning. Parser och preprocessor är inte ändrade.

X86:s målbindning ligger i separat `arch/x64/emit/locals.c`: högst fyra
hem i RBX/R13/R14/R15, O1+, !noopt, med VCC_LREGS-budgeten (0 stänger av).
R12 tillhör justerade ramar. L_VREG läses till en ägd temporär; smal
läsning använder en direkt X_EXT från hemmet, så högbitar normaliseras
utan en extra kopia. Hemmens originalvärden sparas i vanliga frame-slotar
före parameterbindningen och återställs vid gemensamma returetiketten.
Stackargumentens offset ändras inte. Parametrar går ännu via sina
befintliga ABI-slotar och läses sedan till hemmet en gång. Inline-kopior
använder separata minnesbindningar (inst != 0), ingen aliasning av hem.

**Billig lönsamhet:** x86 kräver minst fyra loopviktade referenser och
utesluter lokaler som redan konstantbinds utan lagring. Högst fyra sådana
kontroller, ingen andra symbol-/AST-svepning. Filtrerade kandidater fylls
ännu inte på. Detta undviker extra ABI-spill i kalla enkla funktioner;
auditen visar nu oförändrad kod för alla kalla prober. Loopfunktionerna
har fler statiska instruktioner för prolog/epilog men mindre dynamisk
stacktrafik. Direkt ABI→hem och kopiestädning är nästa möjligheter.

Asm kräver ingen TU-global registerspärr: befintlig asm-sänkning sparar
alla operand-/clobber-register och återställer dem på varje asm-goto-kant
före output-skrivningar. Fyra samtidigt levande hem testas mot verkliga
clobbers och både tagen/fallthrough asm-goto. Inga upprepade asm-skanningar
eller globala avstängningar av orelaterade funktioner införs.

**Nonlocal jumps:** separat `arch/x64/emit/setjmp.c` sparar alltid alla
icke-frame callee-save-register RBX/R12/R13/R14/R15, även ärvda värden från
anroparen när destinationen själv saknar promoverade lokaler. Publik
builtin-bufferlayout oförändrad. En X_RESUME_FRAME-post återskapar R12
som align_down(RBP−8, slutlig falign) före privata snapshot-laddningar.
Skrivaren använder literal LEA/AND så normal minnesomskrivning inte byter
bas. Ojusterad ram emitterar ingenting. Detta stöder även alignment som
upptäcks efter setjmp eller i en inline-kropp; den tidigare lokala
avvisningen kan tas bort. Regressionsfallet hoppar förbi en annan ram
med alignment 256. Metadata beskriver pseudopostens register/flaggor.

Verifierat: unity och fil-för-fil utan nya varningar; 116 tillämpliga
regler/nivåer/mål per byggsätt (32 x86, 28 vardera arm64/wasm32/wasm64),
alla med körning vid O0–O3 och avgränsade kodkrav. Efter aligned-resume-
ändringen kördes relevanta fyra nonlocal-fall om för båda byggsätten.
X86-egensvit 137/137 (fem tidigare filtrerade), arm64 142/142, -j 6
--no-cache. X86 kors-ABI clang/vcc 22/22, O1 DWARF för nya testfiler 2/2,
metadata-kontrakt med ASan/UBSan grönt. Basen avvisas av nya register-
kodkrav och nonlocal-regressionen; clang klarar nonlocal-fallet O0–O3.
Arm64-extraktion: 135/135 kompilerbara befintliga testfiler byte-identiska
med O1 -g/unwind; wasm32 114 och wasm64 117 byte-identiska. Alla äldre
avvisningar oförändrade. Indatahashar och resultat i snapshot-JSON.

CI installerar nu qemu-user och kör x86:s libc-fria optimeringssvit från
arm64-jobbet; QEMU-versionen ingår i toolfingerprint. Harnessen väljer
qemu-x86_64 på annan värd och kan tvinga emulator lokalt med --qemu-x86.
Alla x86-fall har även provats med denna prefixväg, inklusive slutliga
nonlocal-fall. Ingen fjärr-GHA körd eller full wasm-runtime ersatt här.

Bas `eddfc69`, vpp SHA256
`294cc2a1264fdee3a21c413e826d1ab5062b12a4076c51178882022ed21691a9`.
Mätkandidat före aligned-resume-tillägget:
`645d2d05f88339aa11d316bb85fbd1eefba1e354f9f30ff8b47dc63ff51d8c75`;
slutkandidat före dokumentation:
`3b399256f3b795cc5267d72394cfdd4dfc9b3a271fc3c30f38ff1dbaef202ade`.
Artefakter i `build/linux/opt-locals-baseline/`, inklusive före/efter-audit,
snapshots, kompilatormätning och runtime.py/runtime.json.

Kompilator-CPU (barnets user+sys, tre alternerade repetitioner, fyra
3000-funktions-TU:er per batch, O1 -S, -j 1 warm=1):

| Mål | Loopkorpus ms, bas→kandidat | Oförändrat kontrollkorpus ms |
| --- | --- | --- |
| x86-64 | 55,650 → 61,701 | 24,630 → 25,112 |
| arm64 | 88,148 → 88,696 | 33,659 → 32,337 |
| wasm32 | 47,222 → 45,502 | 22,401 → 22,011 |
| wasm64 | 43,927 → 43,046 | 22,156 → 22,139 |

Native loopkorpus kostar cirka 11 % mer kompilator-CPU i denna syntetiska
mätning; cold-kod förblir oförändrad men själva urvalet kostar också tid.
Ingen 1 %-grind eller generell regressionsfrihet är bevisad. Hårdvaru-
räknare saknas; delad värds frekvens/cache/minneslast påverkar CPU-tid.
Mätkandidaten ovan är från före det sista, sällan använda resume-stödet.

Kort native programtest, unsigned summa av 4096 element anropad 2000
gånger via extern funktion (ingen LTO, identisk checksumma 4096177840),
tre alternerade user+sys-sampel inklusive processstart: vcc O1 bas
7,830 ms → kandidat 6,345 ms (cirka 19 % kortare). Clang O2 0,778 ms,
GCC O2 3,330 ms på samma korpus visar fortsatt stor kvalitetslucka.
Detta är ett litet loopprov, inte generell programspeedup. Auditens
sum-funktion har 33→43 statiska instruktioner, stackreferenser 14→12
(ABI-spill ingår); minskad looptrafik betalar här prolog/epilog.

OPT3:s första del är levererad, men direkt parameterbindning, bättre
lönsamhetsurval och OPT4:s kopie-/utvidgningsstädning återstår. Full O2-
täckning, allmänna SSA-/alias-/loopanalyser och G5/G7 är fortsatt öppna.


### Status OPT3 (2026-11-11): direkt ABI-bindning

X86 binder nu utvalda skalära registerparametrar direkt från ABI-registret
med deklarerad bredd/signering. Utvalda stackparametrar laddas en gång
från anroparens befintliga offset. Den separata efterladdningshooken och
onyttiga parameterslotar är borttagna. Befintlig klassificering äger
fortfarande GP/FP-budget, aggregate rollback, hidden sret och varargs;
ingen ny analys, AST-vandring eller allokering.

Unity och fil-för-fil bygger; parameterfallen kör O0–O3 på x86, arm64,
wasm32 och wasm64. Kodkraven avvisar basens spill/laddning. Nya fall täcker
smala signed/unsigned typer, register-/stackargument, blandad FP/GP,
24-byte sret och varargs. X86 kors-ABI 22/22; nonlocal-regression O0–O3; x86-egensvit
137/137 med fem oförändrade filtreringar (--no-cache, -j 6).

Bas d8e02c8 SHA256
`3b399256f3b795cc5267d72394cfdd4dfc9b3a271fc3c30f38ff1dbaef202ade`;
kandidat `08fa144ebfd7166f9e5b499b6f209413a6ba131cf9374064b5d5c0816744a20c`.
Artefakter `build/linux/opt-params-baseline/`. Samma metod som föregående
OPT3-mätning, tre alternerade repetitioner: x86 loopkompilator-CPU
60,282→58,437 ms, kontroll 24,806→25,012 ms. Övriga bakändars källkod är
oförändrad; deras mättider varierar inom värdens brus. Samma lilla summa-
program ger 3,263→3,057 ms, identisk checksumma; clang O2 0,508 ms och
GCC O2 1,793 ms. Tider från olika mättillfällen får inte jämföras direkt.
Ingen generell speedup eller godkänd 1 %-grind hävdas.

Direktbindningen är levererad. Bättre urval, kopie-/utvidgningsstädning,
minnesoptimeringar och full O2-täckning återstår; ingen SSA införd.

### Status OPT4 (2026-11-11): delad framåtriktad kopiestädning

`emit/rec/copies.c` innehåller nu den gemensamma framåtsökningen från
arm64:s kopiepass. Postlayout, effekter, livslängd och tillåtna operander
kommer från målet; omskrivningen förhandskontrolleras innan något ändras.
Högst 256 användarposter sparas i trådlokal arbetsyta. Ingen ny AST-vandring
eller SSA byggs. Arm64 återanvänder befintliga block-/livslängdsdata och
debugmappning. X86:s adapter i `arch/x64/emit/copies.c` gör ett effektpass,
lokal bakåtlivslängd och kompaktering med återanvänd trådlokal arbetsyta.
Den aktiveras från O1 för 64-bitars GPR-kopior till temporärer, inom ett
rakt block och med bevisad lokal överskrivning. Anrop, asm, atomics,
blockgränser, implicita/tvåadressoperander och partiella skrivningar
stoppar osäkra omskrivningar. 32-bitarskopior och XMM hanteras inte ännu.
Wasm har inga sådana maskinposter; dess kodgenerering ändras inte här.

Unity och fil-för-fil bygger. `bench/vcc-rec-copies.py` passerar normalt
och med ASan/UBSan, inklusive negativa gräns-/bredd-/registerfall och
transaktionell gräns vid 256 omskrivningar. `--rule copies --run` passerar
O0–O3 på x86, arm64, wasm32 och wasm64 i båda byggena (32 kombinationer).
Basen avvisas av det nya x86-kodkravet för `cp_stores` vid O1. C-fallen
täcker division/skift, smala värden, källöverskrivning, anrop, loop/kanter,
overflow-flaggor, atomics och native asm. Alla tidigare kompilerbara
ögonblicksbilder är byte-identiska: arm64 137/137 med `-g` och asynkrona
unwind-tabeller, wasm32 116/116 och wasm64 119/119; samma källhashar.

Basens vpp SHA256 är
`08fa144ebfd7166f9e5b499b6f209413a6ba131cf9374064b5d5c0816744a20c`;
kandidat `e8b8d472eb989f2dfe254317b6c81c916f12c1dca2e631584d3e0cd479b7f77b`.
Artefakter och reproducerande skript: `build/linux/opt-copies-baseline/`.
Kompilator-CPU, tre alternerade repetitioner med fyra 3000-funktions-TU:er,
O1 -S, -j 1, warm=1, cacher av (barnets user+sys, inklusive uppvärmning):

| Mål | Loopkorpus ms, bas→kandidat | Oförändrat kontrollkorpus ms |
| --- | --- | --- |
| x86-64 | 76,871 → 69,113 | 26,334 → 27,432 |
| arm64 | 90,907 → 90,196 | 32,528 → 33,213 |
| wasm32 | 45,342 → 45,830 | 23,019 → 27,058 |
| wasm64 | 44,886 → 45,779 | 22,625 → 23,036 |

Ingen konsekvent stor x86-regression syns, men även oförändrad wasm-kod
varierar 17,5 % i kontrollen. Delad värd och saknade hårdvaruräknare gör
att detta inte verifierar 1 %-grinden eller en kompilatorvinst. Det korta
unsigned summaprovet ger 3,785→3,136 ms CPU (clang O2 0,627; GCC O2
1,776), identisk checksumma 4096177840. Basens och kandidatens assembler
och objekt är dock byte-identiska, så skillnaden är mätbrus och ingen
programspeedup från ändringen. Bredare kopiestädning, utvidgningsfakta,
minnesoptimeringar, full O2-täckning och G5/G7 återstår.

## Risker
- **GNU C-ytan och inline asm** är det stora jobbet. Uppskattat: lexer + pp ~2 kloc,
  parser + sema ~10–15 kloc, emitter ~5 kloc, assembler ~4 kloc, länkare ~3 kloc.
- **Korrekthet vid -O0-liknande kodgenerering.** Kärnan förutsätter inlining och DCE.
  Det löses i sema (steg 2.5–2.6), inte genom kärnpatchar.
- **make självt.** Även med v5 tar make ~0,3–0,5 s att läsa alla Makefiles och starta
  ~1 000 jobb. Shimsens latens och make `-j` blir golvet.
- **Mätbrus.** Maskinen används till annat. Bara A/B-mätningar körda direkt efter
  varandra jämförs.

### Optimeringsrigg (2026-11-08): `bench/vbench.py`
- Mått: **instruktioner** per tråd via `thread_selfcounts` (`ccs/vcc/vperf.h`, ingen root, ~1 600 instr per läsning), brus 0,03–0,6 % även med last 15; cykler/vägg bara i `ab` (alternerande). Räknarna inkluderar kärnläge (sidfel), därför är `emit` brusigare (~1–2 %).
- `vpp --batch` fick `--warm N` och `--stats F` (per jobb: ms, instr, cykler, instr i cc_run/emit/obj).
- Korpus `bench/vbench-corpus.txt`: 24 kärn-TU:er (16 kvantiler + emit- och parse-tunga), 9 `quick`.
- Lägen: lex, pp, pphc, fe, cc, emit, real, as, ld. `check` hashar -E, tokenström, layout, objekt (med och utan cache), lexer, .S, vmlinux mot en bas (`mkbase REV`, cache i `work/vbench/golden/`).
- Utgångsläge, full korpus, en tråd: lex 783 M, pp 8 074 M, pphc 7 187 M, fe 11 463 M, cc 12 080 M (emit 600 M, obj 14 M), real 11 212 M, as 57 M, ld 763 M instruktioner. Alla 840 TU:er `-c` utan cache: 330 G instr, varav pp ~86 % (med kall tokencache).
- **Tio parallella optimeringsagenter (2026-11-08)**, en per bana (lexer, pp-makron, pp-direktiv, cc.c deklarationer / uttryck / symboler+tokens, emit, arenor, as/a64/elf, länkare), var och en i egen worktree mot bas 32bc779, regel: ≥ 1 % färre instruktioner och `vbench check` identisk. Alla tio sammanslagna; konflikter lösta mot arenornas `vmreg`-pooler (`ccs/vcc/arena.h`). En sammanslagningsbugg (två banor flyttade `dops` in i cc med olika snapshot-hantering → krasch med deklarationscachen) fångades av `check`. Fullt korpus, bas → nu (instruktioner / cykler):

  | läge | bas M | nu M | instr | cykler |
  |---|---|---|---|---|
  | lex | 783 | 424 | −45,8 % | −26,5 % |
  | pp | 8 066 | 3 245 | −59,8 % | −53,0 % |
  | pphc | 7 125 | 4 706 | −34,0 % | −22,4 % |
  | fe | 11 450 | 5 453 | −52,4 % | −48,3 % |
  | cc | 12 036 | 5 817 | −51,7 % | −45,6 % |
  | emit | 601 | 353 | −41,2 % | −37,0 % |
  | real | 11 232 | 7 354 | −34,5 % | −25,3 % |
  | as | 57,4 | 31,6 | −45,0 % | −45,3 % |
  | ld | 762 | 378 | −50,4 % | −34,4 % |

  Kvar enligt agenterna: lat/överhoppad parsning av oanvända `static inline`-kroppar (1,8 G av fe, ändrar numrering → kräver design), interneringsmissar i den globala tabellen (~hälften av lex-cyklerna), cache för inline-asm-snippets i emit (~93 M), en-pass-tokenisering per sats i as.c, open() dominerar länkarens kärntid.

  Alla kärn-TU:er `-c` i en process (`vcc-cbatch.py -j 4`, alla cacher på): 124,6 G instruktioner / 11,9 s user + 0,8 s sys / 3,2 s vägg (före: 186 G, 17,4 + 2,8 s vid `-j 6`). Sviterna på det sammanslagna trädet: paritet 873/873 (kärna) och 911/912, sema 878/879 + layout 878/879 (18,8 M kontroller), cbatch 840/840, vcc-obj 840/840, vas 30/30. **Kallt bygge + boot** (`bench/vccd-kbuild.sh`, `-j 14`, alternerande bas/ny, last ~8): 877 kompilerade av vcc, 0 till clang; vccd:s jobbtid summerad 65,6 → 43,6 s (medel 75 → 50 ms, max 484 → 327 ms), men väggtiden rör sig inte (totalt 31,1/31,2 → 27,5/42,9 s, kompileringsfasen 19,3/23,6 → 17,6/24,6 s; konfigfasen ensam varierar 4,6–11,6 s): kompileringsfasen begränsas av make/shim/beroendeordning, inte av vccd (jfr Status 2.9). Debian 13 bootar till `running`, 0 felande enheter (`results/boot-20261108-160427.log`). **Nästa hävstång för väggtiden är alltså hur många jobb make håller i luften, inte kompilatorn.**

### Varför make bara håller ~3 jobb i luften (2026-11-08)
- vccd loggar nu starttid per jobb (`@ms`, sista argv). Tidslinje över ett kallt bygge (`-j 14`): **jobben startar i jämn takt, ~100/s, oavsett hur lång tid de tar**; i snitt 2,2 i vccd samtidigt, 3,9 s helt utan jobb (varav 1,5 s i slutet). Det är en starttaktsgräns, inte vccd (64 → nu 2 × kärnor arbetstrådar; ingen kö i vccd).
- Processprov (`ps` var ~0,1 s) under kompileringsfasen: i snitt 8,4 `make`, ~6,4 skal (`sh` = bash), ~3 `vcc`-shims, ~1 `fixdep`. Kbuild går ned i kataloger med recept som kör `$(MAKE)`, och varje sådant håller en jobbplats medan undermaken lever: med `-j 14` går ~8 platser till make-nästlingen.
- Byggets barnprocesser (utan vccd) kostar stabilt **~25 s user + ~50 s sys** per kallt bygge, mer än vccd:s egen kompilering (29–48 s jobbtid). Per C-TU kör receptet `sh -c` + vcc-shim + `fixdep` + `rm`; `fixdep` ensam ~37 ms vägg / ~29 ms CPU per TU (öppnar alla ~930 headers i dep-filen för att leta `CONFIG_`), en tom process ~8 ms. En no-op-ombyggnad (inga CC) tar 8,3–8,9 s / 10 CPU-s.
- `-j 32`/`-j 64` gick inte att bedöma: maskinen hade last 16 av andra sessioner och samma `-j 14` tog 31–52 s.
- **Nästa steg:** (1) fixdep i vccd: vccd har redan dep-listan och alla headers lexade i tokencachen, så `CONFIG_`-uppsättningen per fil kan räknas en gång och `.cmd` skrivas av vccd; receptet blir ett enda kommando (inget `fixdep`, `rm`, helst inget skal) via en Kbuild-patch i `cmd_and_fixdep` när CC är vcc. (2) mäta `-j` på en lugn maskin. (3) färre undermakes / nästlingsnivåer.
- **fixdep i vccd (klart, 2026-11-08):** `ccs/vcc/fixdep.c` är en rak port av `scripts/basic/fixdep` (samma dep-filsparser, samma råtextsökning efter `CONFIG_`, även i kommentarer), men filerna läses genom tokencachen och varje fils `CONFIG_`-namn räknas en gång per process. vccd kör den efter varje lyckad kompilering (egen eller vidarebefordrad) när förfrågan har `VCC_FIXDEP_D/T/O/CMD`, skriver `.cmd` och tar bort dep-filen. Kärnpatch `patches/0008-kbuild-vcc-fixdep.patch` (gren `vroom-pf`): `cmd_and_vccdep` för `rule_cc_o_c`/`rule_as_o_S` när `KBUILD_VCC_FIXDEP=1` (vccd-kbuild.sh sätter den; `VCC_FIXDEP=0` = som förut). Receptet kör nu skal + shim, inte `fixdep` och `rm`.
  - **Verifierat:** alla 1 387 `.cmd`-filer byte-identiska med fixdeps; inga `.d` kvar; no-op-ombyggnad kompilerar inget; `touch include/linux/delayacct.h` bygger om exakt de 13 objekt som listar den; Debian bootar till `running` (`results/boot-20261108-162531.log`).
  - **A/B** (alternerande, last 21–47, väggtid oanvändbar): byggets barnprocesser 26 + 48 / 27 + 50 s (user + sys) → 20 + 28 / 18 + 26 s, alltså **~30 CPU-s (40 %) mindre per kallt bygge**.

### Profil av det kalla bygget efter fixdep i vccd (2026-11-08)
- Rigg: `bench/tsh.c` är ett spårande `SHELL` (kör `/bin/sh`, loggar start/slut i `CLOCK_MONOTONIC` som vccd:s `@ms` samt skalets och barnens rusage); binären ska heta `sh` så att make behåller sin snabbväg. `TSH_LOG=OUT.tsh bench/vccd-kbuild.sh OUT -j 14 SHELL=…/trace/sh` (skriptet skickar nu vidare extra make-argument), sedan `bench/kprof.py OUT`. Spårningen kostar ~1–2 s. Last 4–28 av andra sessioner under mätningarna.
- **Kallt bygge nu (denna gren, `-j 14`):** 15,2/14,5/14,0 s (main utan grenens arbete: 24,0 s vid samma last). Uppdelning: **konfig 3,5 s** (kbuild.py före huvudmaken: `allnoconfig` 2,2 s, som bygger fixdep/conf/bison/flex från noll med värdkompilering via vroomd; `olddefconfig` 0,7 s; `merge_config` 0,05 s) → **1,5–1,9 s till första CC** (makefile-parsning, `conf --syncconfig` 0,9 s, genererade headers) → **kompileringsfasen ~8,5 s** → **länk 0,6–0,9 s**.
- **Kompileringsfasen är CPU-bunden, inte jobbplatsbunden:** `-j 28` gav 17,0/17,3 s mot `-j 14` 15,2/17,5 s. Under fasen kör ~12 lövrecept och ~20–25 undermake-recept samtidigt, men bara 2–7 kompileringar i vccd. Byggets barnprocesser (allt utom vccd) kostar 17 s user + 23 s sys = **40 CPU-s, mer än vccd:s 32 s jobbtid**.
- Per CC-recept: **15 ms (median) innan jobbet når vccd** (bash + shim + anslutning), 28 ms i vccd (p90 74 ms), ~2 ms efter (fixdep är borta). Summerat 17 s före mot 32 s i vccd.
- **`llvm-ar` är den största enskilda processkostnaden:** 416 `AR built-in.a`, 29 s jobbplatstid, median 43 ms, p90 149 ms (upp till 290 ms), 44 ms CPU var under bygget. Ett anrop kostar **167 M instruktioner** (dyld binder `libLLVM.dylib`; 1 800 sidfel), mot 12 M för vcc-shimmen, 22 M för `/bin/sh` (bash) och 8 M för `/bin/dash`. 416 × 167 M ≈ 70 G instruktioner, mer än halva kärnkompileringens 124,6 G.
- **Undermakes:** 432 rekursiva make (en per katalog). En no-op-ombyggnad tar 2,7 s / 8 CPU-s nästan bara i dem (plus 400 `depcheck`).
- **`SHELL=/bin/dash CONFIG_SHELL=/bin/dash`** (alternerande A/B): 14,5/14,0 → **13,0/13,4 s**, barn-CPU 40 → 36 s. `/bin/sh` på macOS är bash.
- **Hävstänger i ordning:** (1) egen `ar` för tunna arkiv (`cDPrST`, platta ut nästlade tunna arkiv) som vcc-shim/vccd-förfrågan i stället för `llvm-ar`: ~70 G instr och ~29 s jobbplatstid; (2) dash som `SHELL`/`CONFIG_SHELL` i bygget: ~1 s, inga ändringar i koden; (3) konfigfasen 3,5 s: värdverktygen (conf, fixdep, …) byggs av clang via vroomd från noll, och `allnoconfig` + `olddefconfig` är två extra toppnivå-make (antingen `--dotconfig` med färdig `.config`, eller värdverktyg från vcc/cache); (4) färre undermakes; (5) skalet + shimmen per CC (15 ms): receptet utan skal när kommandot är ett enda vcc-anrop.
- **Egen `ar` i vccd + dash (klart, 2026-11-08):** `ccs/vcc/ar.c`: tunna arkiv med r/m/t och modifierarna c D P s S T u i b a samt `--thin`, allt Kbuild gör med `$(AR)` (`cDPrST` per katalog, `cDPrsT` för lib.a med symboltabell, `mPi` + `t` för built-in-fixup.a, `cDPrS --thin`). Byte-identiskt med llvm-ar, även dess regler: nästlade tunna arkiv plattas ut, `r` byter ut en befintlig medlem på plats, `m` flyttar i arkivordning före/efter POS (POS jämförs bokstavligt med lagrade namn), symboltabell bara om någon medlem är ELF (tom: 8 byte), sökvägar relativa lexikalt. Shimmen under namnet `var` (även `ar`, `llvm-ar`) → vccd kör den i arbetartråden; **ingen llvm-ar i bygget, ingen reserv**. `bench/kbuild.py --ar/--shell`; `bench/vccd-kbuild.sh` använder `var` och `/bin/dash` som `SHELL`/`CONFIG_SHELL` för hela körningen (`VCC_AR=0`, `KB_SHELL=` = som förut).
  - **Verifierat:** `bench/var-cmp.py` (llvm-ar bara som referens): alla 416 sparade arkivkommandon i ett llvm-ar-träd ger byte-identiska arkiv, `t` identisk för 8 sökvägsformer, 40/40 kantfall (dubbletter, nästlade, saknad fil, r på befintligt arkiv, mPi/mPa/mPb, ELF utan symboler, icke-ELF, arkiv i underkatalog). Kallt bygge: alla 417 `.a`, `vmlinux` och `Image` byte-identiska med llvm-ar + bash-bygget; `.cmd` skiljer bara i AR-sökvägen och `CONFIG_SHELL`; Debian bootar till `running`, 0 felande enheter (`results/boot-20261108-171002.log`).
  - **A/B** (alternerande ×3, last 9 → 22 av andra sessioner, väggtid oanvändbar: 17,4/24,2/25,2 → 18,0/21,9/21,3 s): byggets barnprocesser **18 + 25 → 11 + 20–21 CPU-s (user + sys), ~12 CPU-s (27 %) mindre**. ar i vccd: 422 jobb, i snitt 3,6 ms (förut ~44 ms CPU per llvm-ar-process).
- **Sammanslagning med main (diagnostik, _Complex, long double, … t.o.m. 8094aeb, sedan sanerare/fuzz t.o.m. f323896), 2026-11-08:** main@8094aeb bootade inte (ingen konsolutskrift alls). Bisekterat till 26c2d50 (flyttals-`va_arg`): varje variadisk funktion sparade v0–v7 i prologen även med `-mgeneral-regs-only`, så kärnans första `printk` rörde FP-register innan FP slagits på. Nu som clang: med `-mgeneral-regs-only` ingen v-sparyta, `__vr_offs` 0 och `__vr_top` = `__gr_top` (`cc_opts.general_regs_only`). Dessutom: deklarationscachens ögonblicksbilder bar med sig andra TU:ers `c->cases`/`c->span` (11 felaktiga "duplicate case value" i cbatch); `span` är nu en egen array i bilden (med platskodning), `cases` behålls per TU.
  - **Verifierat:** paritet 911/912, sema run 878/879, warn 879/879, layout 878/879, ctests 1708/1708, diag 63/63, cbatch 840/840 (samma stderr med och utan deklarationscache), vcc-obj 840/840, vas 30/30, var-cmp 416/416 + 8/8 + 40/40. Kallt bygge 13,9 s (last ~8), 877 av vcc, 0 till clang; Debian bootar till `running` (`results/boot-20261108-174855.log`). Efter andra sammanslagningen (f323896): samma siffror, fuzz-regress 4/4, `pp_open` skriver nu "1 error generated." även när huvudfilen har ett fatalt fel (diag 63/63); kallt bygge 15,3 s (last ~11), bootar (`results/boot-20261108-175726.log`).
  - **Varför det inte bootade, bekräftat:** kärnan utan rättelsen i QEMU/TCG med `-d int`: ESR-klass 0x07 (FP/SIMD-åtkomst fångad) vid `_printk+0x2c` (`fmov x16, d0` direkt efter `str x0..x7`), första `printk` i `start_kernel` före `setup_arch`, alltså före earlycon; fällan tar sig tillbaka in i `printk` och upprepas (~1,5 M gånger på 25 s), ingen utskrift. Utan rättelsen sparar 83 variadiska funktioner i `vmlinux` v0–v7, med den 0. vcc-obj såg det inte (jämför symboler och relokeringar), ctests är användarprogram.
  - **Vakt:** `bench/vcc-fpcheck.py` kompilerar alla 840 kärn-TU:er med `-mgeneral-regs-only` (`vpp --batch -S`) och underkänner genererade FP/SIMD-instruktioner (b/h/s/d/q/v/z-register, FP-mnemonics, `.inst`-ord med bit 27:26 = 11) utanför inline asm, som `-S` nu omger med clangs `//APP`/`//NO_APP`. 840/840 nu; med rättelsen avslagen 801/840 (`panic`, `__warn_printk`, `sysfs_emit`, …). ~10 s.

### Profil av kallt bygge på macOS, 2026-11-08 (efter Linux-sammanslagningen)
- Rigg: `TSH_LOG=OUT.tsh bench/vccd-kbuild.sh OUT -j 14 SHELL=…/trace/sh` + `bench/kprof.py OUT`
  (nu även jobbplatser per sekund och recepten före första CC); `sample <vccd> 25` +
  `bench/vccd-sample.py` (ny) för vccd:s CPU per steg. vccd skriver vid avslut
  huvudcachens och deklarationscachens räknare samt sin egen rusage.
- **15,0 s** (last ~5): konfig 2,9 s (`--config`: allnoconfig + olddefconfig bygger conf/fixdep
  från noll via vroomd) → 1,9 s till första CC (`conf --syncconfig` 0,39 s, dtc-värdverktygen
  via vroomd/clang 100–270 ms st.) → kompileringsfas ~7 s → länk 0,7 s.
- **CPU:** vccd 16,9 s user + 1,7 s sys. Resten av bygget (make, skal, shims) 14 s user +
  **27 s sys**. Bladrecept: CC 2,7 + 4,5 s, AR 2,3 + 9,5 s; resten (~15 s) är de 432 makeprocesserna.
- **Kompileringsfasen har två halvor:** först 7–9 TU:er samtidigt i vccd, sedan bestämmer
  arkiven: upp till 6 AR-recept i luften medan kompileringarna sjunker till 2–3. Ett AR-recept
  tar ~55 ms vägg (skal + `rm` + `var`-shim + printf), men bara 2 ms i vccd. `built-in.a`
  kedjar uppåt i katalogträdet, alltså kritisk väg. Per CC: 13 ms (median) före vccd
  (skal + shim), 34 ms i vccd (p90 94).
- **vccd:s CPU** (59 k prov): förprocessning som parsern drar 43 %, parse + sema 36 %, fixdep 6 %,
  emit 5,5 %, I/O-tråd 2 %, skrivning 2 %. Hetast: `rec_tok` 11 % (inspelning till
  huvudcachen, även av uppspelade barnhuvuden), `pp_next` 9 %, `decl_specs`/`declarator` 4 % var.
  Huvudcache: 40 790 träffar, 12 095 missar (11 069 inspelade, 43,7 MB), 548 MB uppspelat;
  deklarationscache 845 återställningar.
- **Hävstänger:** (1) skal och processer per recept: CC = skal + shim, AR = skal + `rm` + shim
  (nästa steg); (2) konfigfasen (färdig `.config`); (3) inspelningen i huvudcachen.
- **Recepten i vccd: vsh + rsh (klart, 2026-11-08):** make:s `SHELL` är `build/ccs/vsh/sh`
  (shim.c med `-DVSH`; heter `sh` så att make behåller snabbvägen). Den skickar `sh -c SKRIPT`
  till vccd, som tolkar skriptet med `ccs/vcc/rsh.c`: Kbuilds delmängd av sh (`set -e`, `:`,
  `trap` (ignoreras), `echo`, `printf` med `%s`, `rm -f`, `> FIL`, `NAMN=V verktyg …`,
  `printf … | xargs verktyg …`, `scripts/basic/fixdep D T CMD > FIL`, citat och `\X`).
  Verktyg = vcc/vas/vld/var bredvid vccd; de körs som vanliga förfrågningar i samma tråd
  (fixdep efteråt som i 0008). Hela skriptet parsas innan något körs; allt annat (`$`, `&&`,
  subskal, glob, `>>`, okända kommandon …) avböjs och vsh kör `$VSH_REAL` (dash) med samma
  argv. Ingen Kbuild-patch. `bench/vccd-kbuild.sh` använder vsh som standard (`VSH=0` = dash).
  - **Verifierat:** `bench/vsh-test.py` 32/32 mot dash (status, stdout, stderr, filer; fall som
    måste köras i vccd får `VSH_REAL=/bin/false`, avböjda räknas), även med ASan och på Linux
    (29/29 utan byggt fixdep). Kallt bygge: alla 3 477 filer i byggträdet (`.cmd`, `.o`, `.a`,
    `vmlinux`, `Image`) byte-identiska med dash-bygget; no-op-ombyggnad kompilerar inget;
    Debian bootar till `running` (`results/boot-20261108-192143.log`). En krasch på vägen:
    `vfixdep` före första kompileringen (efter en vidarebefordrad värdlänk) internerade utan
    `vintern_init`.
  - **Mätt (macOS, `-j 14`, alternerande, last 5–11):** 1 359 recept körs i vccd (alla 854 CC,
    416 AR, HOSTCC, fixdep-recepten), 1 378 avböjs (mest `$(shell …)` och konfig, 0,1 ms
    vardera). Totalt 13,6/13,0 s (dash) → **11,6/11,9 s**; byggets barnprocesser 10 + 18 →
    **8 + 10 CPU-s** (user + sys). kprof: CC före vccd median 13,2 → 5,7 ms (med tsh), AR-recept
    55 → 8 ms (summa 22,9 → 3,3 s), receptens CPU CC 7,2 → 1,9 s, AR 11,8 → 0,9 s. Linux (`-j 8`):
    fork/exec var redan billigt, 9,8/8,9 → 8,8/13,0 s, brus.
  - **Kvar:** de 432 undermakes (≈ 100 CPU-s inkl. barn i spårningen) och `$(shell)` (vsh
    avböjer, en extra rundtur per anrop).
- **Ny profil med vsh (2026-11-08) och två lågt hängande frukter:**
  - Bladrecepten är nu billiga (CC + AR ~3 CPU-s). Kvar i spårningen: 422 `$(shell mkdir -p
    $(obj))`, ett per undermake under parsningen (vsh + dash + mkdir, median 14,5 ms, 6,2 s vägg
    summerat). **rsh kör nu `mkdir -p`** (mkdirat, föräldrar uppifrån): 1 786 recept i vccd,
    951 avböjda.
  - vccd: `rec_tok` 13 % av proven. Under inspelning kopierades varje token från en uppspelad
    barnpost in i inspelningen, fast publiceringen hoppar över barnsegmenten. **Nu reserveras
    bara positionerna** (`ctx.norec`, `ntk += total`). cbatch (alla kärn-TU:er `-c` i en
    process): 150,7 → **135,4 G instruktioner** (−10 %), 48,0 → 41,1 G cykler, user 15,0 → 12,8 s.
    Kallt bygge: vccd user 16,7 → 14,6 s.
  - **Verifierat:** paritet 911/912, batch --cmp 873/873, cbatch 840/840 (även ASan), fuzz-regress,
    vsh-test 35/35 (även ASan); byggträdet (3 478 filer) byte-identiskt med dash-bygget; Debian
    bootar (`results/boot-20261108-193415.log`). Väggtid 12,1 s vid last ~10 (brus).
  - **Nästa kandidater:** konfigfasen har två seriella omgångar med 125 Kconfig-`$(success,…)`
    var (521 + 308 ms), `{ …; } >/dev/null 2>&1 && echo y || echo n` med `${o#-Wno-}` m.m. —
    antingen mer sh i rsh eller en Kconfig-patch som frågar vccd direkt; 60 värdkompileringar
    via vroomd före första CC; de 432 undermakes.

### Profil av vcc/vccd:s eget arbete (2026-11-08): vad som bör byggas om
- Rigg: `bench/vccd-kbuild.sh` (vroom.config, `-j 14`) med `sample <vccd> 25` +
  `bench/vccd-sample.py`; samma TU:er utan byggsystem i `vpp --batch` (som
  `vcc-cbatch.py`, `--stats`, `-j 1` under `sample`, `--warm 1`, `VCC_NODC`/`VPP_NOHC`);
  `bench/vcc-bodies.py` (ny) för vilka tokens parsern får. Last 5–6.
- **Bygget:** 13,9 s, 877 TU:er av vcc. vccd 15,6 s user + 2,4 s sys; 877 `cc-own` summerar
  46,4 s vägg (median 39 ms) mot ~13,6 s CPU i batch (median 14 ms): jobben väntar på CPU
  (9–11 i luften, make och andra sessioner delar kärnorna), inte på lås (84 % av de blockerade
  proven är lediga arbetare; inga mutexväntor i kompileringen). vccd använder ~2,1 kärnor i
  snitt under kompileringsfasen, så det är fortfarande make som sätter väggtiden där.
- **Kompilatorn ensam** (840 kärn-TU:er, en process, kall): 131 G instruktioner, IPC 3,4,
  12,2 s på en tråd, 2,3 s på 6. cc_run (förprocessning + parse + sema) **98 %**, emit + as
  3 %, objekt 0,8 %. Proven utan väntan: parse + sema 50 %, förprocessning som parsern drar
  42 %, emit 5 %, skrivning 2 %. Hög IPC: mikrooptimering är i stort sett gjord; det som
  återstår är att göra mindre arbete.
- **Cacharnas värde:** utan båda 189 G; huvudcachen 189 → 151 G (−20 %); deklarationscachen
  151 → 135 G (−10 %; bara TU:er med samma *prefix* av toppnivåhuvuden, i snitt 1,5 regioner
  per återställning, ~19 % av tokens). En helt varm process (`--warm 1`, = en vccd som överlever
  mellan byggen) 133,6 → 106,7 G (−20 %): resten är arbete per TU som ingen cache tar bort.
- **Vad parsern får** (`vcc-bodies.py`, 24 korpus-TU:er, 20,8 M tokens): 95 % kommer från
  headers; **51,6 % är kroppar av funktioner i headers och 49,2 % kroppar som TU:n aldrig når**
  (transitivt från huvudfilen och initierare); huvudfilens egna kroppar 4,1 %. Typisk TU:
  ~8 000 inline-funktioner i headers, ~7 800 oanvända. I tid: `compound_stmt` inklusive den
  förprocessning den drar är **55 % av kompileringens CPU** (27 % pp, 28 % parse).
- **Ombyggnader i ordning:**
  1. **Lat parsning av funktionskroppar i headers** (~−45–50 % av kompileringens CPU om
     överhoppet är gratis, ~−25 % om bara parsningen sparas). Vid `{` efter en
     funktionsdeklarator på filnivå: spara tokenintervallet och hoppa till matchande `}`;
     parsa när namnet först refereras (anrop, adress, inlining ≤ 40 noder), annars aldrig
     (emit är redan lat). Det dyra är att tokens fortfarande dras genom `pp_next`; därför
     ska huvudcachens poster bära kroppsintervallen (räknade vid inspelning: djup 0, `)` `{`)
     så att uppspelningen hoppar över en kropp i O(1) och parsern senare läser den direkt ur
     postens tokenarray (nollkopia). Namnuppslag i en sen kropp: symboler bär en
     deklarationsordning och uppslaget ignorerar senare deklarationer (exakt C-semantik även
     för sena taggar/omdeklarationer). Kostnad: diagnostik i oanvända kroppar försvinner
     (clang ger den) → `VCC_EAGER=1` för sema-/diag-sviterna; numrering (strängar, statiska
     lokala) följer användningsordning, vilket clangs CodeGen också gör (deferred decls).
  2. **Deklarationscache per huvud i stället för per prefix** (modul-lik): när en huvudpost
     spelas upp med samma makroingångar och samma *semantiska* ingångar (de namn/taggar dess
     deklarationer slog upp och vad de band till), applicera postens deklarationsdelta
     (typer, symboler, taggar) i stället för att parsa. Efter (1) är det som återstår av
     headers mest deklarationer (~40 % av dagens tid); prefixträdet når bara ~19 % av tokens.
  3. **fixdep i vccd** 6 % av vccd:s CPU i bygget (`in_hashtable`, mutexväntor i `vfixdep`):
     `CONFIG_`-mängden per fil som internerade id:n, union per TU med generationsstämplad
     array, inget globalt lås. **Låset borta (2026-11-10, från grenen):** namnen per fil
     hänger i `vlex_file.fixdep` (CAS), ingen mutextabell.
  - **Diagnostikintervall bara när något rapporteras (2026-11-10, från grenen):** `pp_tok_end`
    räknades vid varje tilldelning (`need_lvalue`), varje `/ % << >>` med konstant och varje
    `sizeof (T)`; nu först på felvägen. Grenen mätte cbatch 135,4 → 128,7 G instruktioner
    (−5 %, före de lata kropparna). `vpp --batch` skriver nu antal token till parsern och
    hur många deklarationscachen hoppade över.
  4. Småsaker: `VCC_LOG`:s `fprintf` tar `flockfile`-lås (bara profilering); `open()` vid
     skrivning 1 %.
- **Lata kroppar, steg 1: på parsernivå (klart, 2026-11-08).** `cc/lazy.c`: en `static`
  funktion som definieras i en header (inte huvudfilen; utan used/constructor/destructor/alias,
  inte overloadable, ingen `#pragma pack` aktiv) får sin kropp sparad som tokens (`lztok`,
  16 byte; identifierare som internerat id, övrigas stavning i `lzs`), `SF_DEFINED | SF_LAZY`,
  och tokenströmmen fortsätter efter `}`. Efter TU:n parsas de kroppar som något namnger
  (`SF_REF`: varje `N_VAR`, även i sizeof/typeof, block-scope-`extern` märker den globala,
  `cleanup()`, alias-mål), tills inget nytt namnges. Namnuppslag i en sen kropp hoppar över
  filskopssymboler skapade efter definitionen (`lz_wm`, längs `shadow`), alltså exakt det
  som syntes där. `#pragma` i kroppen sparas som `P_DIRECTIVE` och verkställs när kroppen
  parsas (`fetch_directive` läser vidare ur kroppen). Deklarationscachens bilder har tre nya
  arrayer (platser kodas). Emit avbryter med internt fel om en aldrig parsad kropp refereras.
  `VCC_EAGER=1` = som förut.
  - **Mätt** (840 kärn-TU:er, en process, `-j 6`, samma binär med/utan `VCC_EAGER`):
    137,6 → **101,7 G instruktioner (−26 %)**, 43,8 → 35,1 G cykler, user 14,1 → 10,9 s.
    4,39 M kroppar sparade (264,6 M tokens), **85,6 k parsade (6,5 M tokens, 2 %)**. Cyklerna
    sjunker mindre än instruktionerna: kopieringen av tokens är minnesbunden, och de dras
    fortfarande genom `pp_next` (steg 2). Kallt bygge (`-j 14`, alternerande ×3, last 7–9):
    vccd user 14,5–15,1 → **12,1–12,3 s**, summerad jobbtid 26–28 → 22–24 s; väggtiden
    10,8–11,0 → 10,4–10,9 s (make-bunden, som väntat).
  - **Verifierat:** cbatch 840/840 (även ASan+UBSan), vcc-obj 840/840 med samma 31 odefinierade
    symboler som utan lata kroppar, sema run 878/879, layout 878/879 (18,8 M kontroller),
    fpcheck 840/840, diag 63/63, ctests 1708/1708, paritet 911/912. Objekten mot `VCC_EAGER=1`:
    maskinkoden identisk i alla 839; 284 skiljer i byte-form, bara placering: strängar i
    `.rodata.str1.1`, ordningen av statiska lokala objekt (`__msg`) i `.rodata`/`.data`,
    suffixet på statiska lokalas namn (sym-index) — de skapas när kroppen parsas. Debian
    bootar till `running`, 0 felande enheter (`results/boot-20261108-214004.log`).
- **Lata kroppar, steg 2: i huvudcachen (klart, 2026-11-08).** `publish_tok` sparar per post
  de yttersta `{`–`}`-paren i postens egna tokens som inte har något barn inskarvat (`hent.br`).
  När parserns `{` för en lat kropp just spelades upp ur en post och paret finns där
  (`pp_lazy_skip`; inte under inspelning utom i en `norec`-uppspelning, inget `pend`),
  flyttas uppspelningen förbi `}`: positionerna räknas (`ntok_out`, deklarationscachen),
  `__COUNTER__`-posterna hoppas över. Kroppen är då `(post, open, close, bas, cbase, ci)`
  (`pp_lazy` i `lzfn`), och den sena parsningen läser postens tokens direkt (`pp_lazy_tok`,
  samma `__COUNTER__`-omskrivning som `replay_tok`, nu `ctr_spell`). Missar i huvudcachen
  kopierar som i steg 1. `prevloc`/`prevflags` blir `}`:s som förut.
  - **Mätt** (samma körning som ovan, 840 TU:er, `-j 6`): steg 1 101,7 → **76,0 G
    instruktioner** (från 137,6 utan lata kroppar: **−45 %**), 35,1 → 27,4 G cykler (−37 %),
    user 10,9 → 8,3 s, vägg 2,1 → 1,6 s. 4,21 M kroppar sparade, 3,81 M ur huvudcachen,
    samma 85 647 parsade. Deklarationscachen tar nu 121 bilder (förut 64) inom samma tak.
    Kallt bygge (`-j 14`, alternerande mot `VCC_EAGER=1` ×3, last 5–13): vccd user
    14,8–15,1 → **9,4–9,5 s (−37 %)**, summerad jobbtid 30–42 → 19–24 s; väggtiden brus.
  - **Verifierat:** alla 839 objekt byte-identiska med steg 1; paritet 911/912, batch --cmp
    873/873, sema run och layout 878/879, fpcheck 840/840, diag 63/63, vcc-obj 840/840 (samma
    31 odefinierade), cbatch ASan+UBSan 840/840, fuzz-regress, vccd-cc 174/175 (som förut;
    obs: med ett långt `TMPDIR` blir socketvägen för lång och vccd-cc hänger i `forward`).
    Debian bootar till `running`, 0 felande enheter (`results/boot-20261108-220251.log`).
- **Inkrementellt:** make-nivån fungerar redan (vccd:s `.cmd` = fixdeps, `touch` på ett huvud
  bygger om exakt beroende objekt; no-op 2,7 s, nästan bara undermakes). En vccd som lever
  mellan byggen ger −20 % instruktioner. Det som ger mycket är en **innehållsadresserad
  objektcache i vccd** (direktläge som ccache: nyckel = vcc-bygg-id + argv + hash av
  huvudfilen och varje fil i förra körningens dep-lista; filhash en gång per process i
  tokencachen; träff = skriv `.o` + `.cmd`), mmap:ad på disk. Ett om-bygge i tom utkatalog
  blir då nära noll kompileringstid; det första kalla bygget hjälps inte.

### Felmeddelanden (clang-format)

Status: `bench/vcc-diag.py` jämför vccs stderr med clangs (-fsyntax-only) på
`ccs/vcc/tests/diag/*.c`: 63/63 identiska (från 4/48).  `bench/vcc-sema.py
warn` kör vcc och clang med kärnans flaggor på alla 879 TU:er och kräver att
varje diagnostik vcc skriver också skrivs av clang: 879/879 (inga falska
varningar).

Motorn i pp.c skriver clangs format (källrad, caret, intervall `~`, fix-it-rad
med clangs utfyllnad, "In file included from", färger, "N errors generated.",
sökvägen som den stavats, "./a.h" för includes från en fil utan katalog, även
i `__FILE__` som i clang).  cc.c: intervall per nod (`c->span`),
`report()`/`note()`/`warnx()`, typnamn, stavningsförslag med återhämtning,
NF_ERR mot följdfel, "expected ';' after …" med fix-it, int-conversion och
incompatible-pointer-types per sammanhang med parameternot, antal argument
("expected single argument 'a'", parameterlistans intervall i "declared
here"), dubbla case-värden/medlemmar, tentativa ofullständiga typer,
static_assert med "expression evaluates to", `.`/`->`, omatchade parenteser,
"declared const here" med deklarationens intervall, -ferror-limit (19 fel +
fatal utan position).  Varningsramverk: -W/-Wno-/-Werror(=)/-Wno-error=/-w i
ordning, grupper (all, most, everything), tysta i systemhuvuden och för
operatorer från makron (PF_MACRO): -Wparentheses, -Wunused-comparison,
-Wswitch, -Wmacro-redefined.  pp: #error/#warning, oavslutade villkor,
saknad include, makroargument med "macro 'X' defined here".

"expanded from macro": caret och intervall över hela anropet, noten vid
kroppens token (hittas vid diagnosen via `#define`-raden, så tokens bär ingen
extra position); inga stavningsförslag för namn från makron (som clang).
Nåbarhetsvarningar: -Wdivision-by-zero, -Wshift-count-overflow/-negative,
-Warray-bounds ("array 'a' declared here"), tysta i kod som inte nås
(if/?:/&&/|| på konstanter).  Hittills bara för odeklarerade identifierare i
makron.

Kvar: noten för fler diagnostiker än odeklarerade namn, nästlade makron (en
nivå i dag), fler -Wall-varningar.

### Integration på aktuell main (PR-serie, 2026-11-11)

X86:s nytillkomna debugrader remappas nu genom både onåbarhets- och
fallthrough-kompaktering, och slutraden remappas även genom twoaddr.
`bench/vcc-opt-debug.py` kontrollerar källrader vid levande operationer,
.text med/utan -g och dwarfdump --verify vid O0–O3; ingår i CI.

### OPT3 på aktuell main (PR-serie, 2026-11-11)

Registerhemmens ABI-sparning använder nu x86-skrivarens befintliga csslot
för prolog/epilog och korrekt CFI, även vid realignment. Den separata
lsave-/restore-hooken är borttagen. Asm tar dessutom privata snapshots av
aktuella registerhem när fasta operand-/clobber-register överlappar dem;
temporärägandet återställs på varje kant. Alla asm-output hålls adresserbara
av prepassens lnoreg-regel, så deras resultat skrivs före snapshot-restore.
Fasta RBX-output och asm-goto-output testas mot ett samtidigt levande hem.

Rebaserad unity och fil-för-fil bygger. Unity: 116 fokuserade
regel/nivå/mål-kombinationer med körning, inklusive smala parametrar och
setjmp/alignment. Relevanta åtta x86-local/nonlocal-fall även fil-för-fil.
X86 kors-ABI 22/22. `bench/vcc-x86-unwind-locals.py` använder libgcc:s riktiga
unwinder för att återvinna anroparens RBX/R13–R15 genom vcc-funktioner med
vanlig och 64-byte-justerad ram vid O1/O2, båda byggsätten. Clang-referensen
klarar testet; äldre audit-binär avvisas. Skriptet kräver native x86 Linux,
CI:s arm64-jobb kör fortfarande de libc-fria x86-proberna under QEMU.
`bench/vcc-opt-debug.py` och ASan/UBSan-effektkontrakt gröna.

Dessa resultat ersätter inte en ny prestandagrind på aktuell main.
De tidigare mätningarna ovan behåller sina dokumenterade baser/hashar.
G5/G7 och full O2-täckning är fortsatt öppna.

### OPT4 på aktuell main (lokal fortsättning, 2026-11-11)

Kopiepasset är överflyttat till PR-seriens nya bas i separat lokal gren
`opt/native-copies`; ingår ännu inte i de fem öppna PR:arna. X86:s
kompaktering remappar nu varje debug-index och slutraden före twoaddr.
Debugregressionen har en extra funktionslokal markör efter borttagbara
registerkopior. Unity och fil-för-fil bygger; 16 nya regel/nivå/mål-fall
med körning klaras i unity, fyra x86-fall även fil-för-fil. Debugrader,
.text-likhet och DWARF-struktur klaras O0–O3 i båda byggsätten; sanitiserade
record-kontrakt och riktig native unwind genom vanliga/justerade ramar
klaras. Äldre mätningar ovan är fortfarande knutna till sin äldre bas;
ny prestandagrind och PR/CI för denna fortsättning återstår.

Den delade värdens /tmp fick slut på inoder. 756 befintliga snapshotfiler
arkiverades och verifierades byte-för-byte före lokal städning i
`/source/vroom/work/opt-audit-artifacts/copy-snapshots.tar`; JSON/hashar och
mätdata är kvar i den ursprungliga opt-copies-baseline-katalogen.
### Leveransintegration (2026-11-11)

PR33–36 mergades i sina stackbaser efter att föregående bas redan var
mergad till main. Deras optimeringar fanns därför inte i main. Grenen
`opt/optimizer-integration` samlar de redan granskade ändringarna med
main b0b483d och behåller samtliga CI-kontroller, inklusive C2y-cache.
Unity, 116 fokuserade körkombinationer på fyra targetkonfigurationer,
debugrad-/DWARF-/textkontroll och riktig x86-unwind O1/O2 är gröna efter
integrationen. Prepassens trådpublicering och VLA-fakta är granskade mot
registerurvalet. Ny CI krävs på den integrerade committen.

### OPT4 kostnadsgranskning efter integration (2026-11-11)

Den ivriga x86-adaptern avvisas för publicering: parat mot d1ee6f6 gav
kompilator-CPU loop 68,524→75,594 ms och kontroll 26,064→27,775 ms,
med konsekvent högre kandidatsampel. Artefakter:
`/source/vroom/work/opt-pr-tmp/copies-measure/integrated-pair/`.

X86 avkodar nu effekter vid behov via den gemensamma kopiekärnan. Ingen
egen allokering, full effekt-/liveness-tabell eller bakåtvandring. Utan
live-tabell krävs uttrycklig full överskrivning av temporären; källans
ändring eller en opak gräns avbryter hela transaktionen. Detta är mer
konservativt när ett värde dog före en senare källändring. Arm64 använder
sina befintliga tabeller utan ny analys. Kopieanropet är dessutom infogat
i den befintliga twoaddr-vandringen; separata skannings-/kompakteringspass
är borttagna och samma debugremappning hanterar borttagna kopior.
Returkopior före hopp/etikett avvisas utan effektavkodning.

Det nya passet behåller cp_stores 26→23 instruktioner och 12→9 register-
kopior. Sanitiserade kontrakt, x86/arm64:s fokuserade O0–O3-körningar,
debugrader/DWARF/text och riktig unwind O1/O2 är gröna. Ny mätning mot
14379fe: före snabbvägen x86 loop 64,638→66,536 och kontroll
25,153→26,793 ms. Med snabbvägen, vid annat lastläge, loop
85,539→94,029 och kontroll 30,644→29,524 ms; föregående kandidat i samma
mätning gav 103,384 respektive 31,356 ms. Absoluta tal mellan rundor är
inte jämförbara. Looppåslaget är fortfarande en öppen kostnadsfråga,
inte ett bevisat godkännande av 1 %-grinden. `ondemand-pair/`,
`fused-pair/` och `fastpath-pair/` innehåller råsampel, hashade binärer och
kandidatpatchar under samma copies-measure-katalog. Kopiesteget ligger
fortsatt separat från integrations-PR38; ingen ny PR för det ännu.

### OPT5 konkret genomförandeordning

1. Flytta arm64:s befintliga cell-/överlapps-/escapeanalys och slots-pass
   till `emit/rec/slots.c`, återanvänd befintliga rr/ww/live/blocktabeller.
2. Beskriv åtkomster med stabilt allokerings-ID (även andra ID:t vid
   one-past), offset/bredd/värderegister och explicit observable-markör.
   X86:s nuvarande LD/ST saknar volatile/atomic-fakta: dessa måste följa
   med från de typade walker-anropen innan någon DSE kan aktiveras.
3. X86 börjar med exakta GPR-celler 1/2/4/8 byte; laddning ersätts med
   MOV/EXT enligt rätt signering. Otaggade/överlappande/observerbara
   åtkomster blockerar optimering. Testa särskilt olästa volatile stores,
   scope-återanvändning, adressflykt, one-past, unioner, calls/asm/atomics.
4. Wasm delar cellanalysen men behöver stabila lokala värde-ID:n och
   strukturerade minnesoperationsposter eller scalar replacement före
   textemission. Ingen omskrivning av färdig assemblertext; adress-
   producenter, smala extensioner och stackbalans måste bevaras.

### Verifierad leverans (2026-11-11)

Integrations-PR38 är mergad till main med head 14379fe. Slutlig CI-körning
43017255611 klarade arm64, wasm, ASan, TSan och gate. Det tidigare felet
med saknad X_FSQRT-effektmetadata är åtgärdat, med riktade kontrakt för
skalära FP-resultat och bevarade övre SIMD-lanes. DNS-felet från första
ASan-installationen återkom inte. OPT4:s nya kopiearbete ligger kvar i
separat lokal gren och är ännu inte publicerat på grund av kostnadsfrågan.

### OPT4: direkt känd nollutvidgning och instruktionsprofil (2026-11-11)

`arch/x64/emit/extensions.c` bevisar nu från endast föregående post att ett
GPR-resultat redan är nollutvidgat från 32 bitar. `x64_ext` utelämnar då
sin redundanta unsigned 32→64-normalisering vid O1+, !noopt. Inga nya
vandringar, tabeller eller allokeringar. Regeln gäller explicita fulla
32-bitarsskrivningar och unsigned laddningar/utvidgningar, inte okända
poster, etiketter, partiella/signed skrivningar, shifts eller BSF/BSR.
Signed normalisering behålls. Arm64:s befintliga extensionspass och wasm:s
typade operandstack ändras inte av denna instruktionsegenskap.

Nya known_extensions.c kör O0–O3 på x86, arm64, wasm32 och wasm64.
X86:s funktionsavgränsade kodkrav avvisar äldre binären. Höga 64-bitar,
negativa värden, smala typer, wraparound, skift0 och volatile läsning
kontrolleras; register-/breddkontrakt klarar ASan/UBSan. Debugproben är grön.

Valgrind3.26 installerades endast i en lokal artefaktkatalog, inte i
systemet. Native-march-byggen använder AVX512 som verktyget inte stöder;
båda profileringsbinärerna byggdes därför identiskt med -O3
-march=x86-64-v3. Bas är14379fe. 500-funktions-TU:er, mål x86-64 O1 -S,
hela processen inklusive uppstart, Callgrind Ir:

| Korpus | Bas | Kopiepass | Kopior + direkt utvidgningsregel |
| --- | --- | --- | --- |
| Loop | 35 695 355 | 36 202 950 (+1,42%) | 35 775 862 (+0,23%) |
| Kontroll | 16 278 936 | 16 328 031 (+0,30%) | 16 158 711 (−0,74%) |

Detta gör kostnaden reproducerbar för dessa v3-prober; det är inte en ny
vbench-/native-CPU-/helkorpusgrind. Native CPU-sampel ovan har stor värdlast-
variation. Råprofiler, versionerad lokal verktygspaketfil och binärer finns
i `/source/vroom/work/opt-profile/`. Slutlig verifiering och CI på aktuell
main krävs fortfarande före leverans av OPT4.

### OPT4 verifiering på ny main (2026-11-11)

Main9d022c3 är integrerad utan att ta bort dess bitcast/packed-bool-stöd.
Unity och fil-för-fil bygger. 148 fokuserade regel/nivå/mål-kombinationer
med körning är gröna (40 x86, 36 vardera arm64/wasm32/wasm64); åtta
x86-kopie-/utvidgningsfall och debugkontroll upprepade fil-för-fil.
Båda host-kontrakten klarar ASan/UBSan. X86:s riktiga unwind och
funktionsavgränsade debug-/text-/DWARF-kontroller är gröna. Ny CI följer
PR:n; CPU-mätningarna och instruktionsprofilen ovan har sina uttryckliga
äldre baser och är inte ett generellt godkännande av G5.

### OPT6 status: bitmask för switchmedlemskap (2026-11-11)

En begränsad gemensam analys känner igen minst fyra staplade case-etiketter
med samma ingång och högst 64 möjliga värden. Arm64, x86, wasm32 och wasm64
använder vid O1–O3 en bitmask med unsigned intervallkontroll före skiftet;
default, kroppar och fallthrough behåller sina ursprungliga etiketter. Täta
masker, skilda ingångar, dolda case, `_BitInt` och större spann använder
befintlig dispatch. O0 och `VCC_NOOPT` påverkas inte.

`vcc-switch-bitsets.py --run` passerar O0–O3 och `VCC_NOOPT` på alla fyra mål,
inklusive signed/unsigned-gränser, intervall, sidoeffekter och fallback. Unity
och fil-för-fil bygger; fil-för-fil kör också x86 och wasm64-testen. Vcc-sviten
passerar 246/246 med en befintlig xfail. På exakt föräldern 032e9dc jämfört
med ändringen är x86-O2-funktionen `bs64` 131 → 85 byte (utan alignment),
ett riktat kodstorleksprov. Tolv alternerande körningar av en korpus med 500
matchande switchfunktioner gav median 4,52 → 4,34 ms för hela kompilatorn
(intervall 4,27–5,06 respektive 4,13–4,76 ms); dessa korta väggtider är inte
en generell prestandagrind. Hårdvaruräknare var otillgängliga på värden.
Värdetabeller och bitset-kluster med flera destinationer återstår.

### OPT6 status: balanserad switchdispatch (2026-11-11)

Gemensam policy, 32/64-bitars normalisering och sortering finns i
`emit/opt/switch.c`; arm64, den delade x86-walkern och wasm använder den för
verkliga balanserade jämförelseträd vid O1–O3 och minst åtta case-intervall.
Små switchar behåller den billiga kedjan; wasm behåller sin täta `br_table`.
O0, `VCC_NOOPT` och 128-bitars fallback är oförändrade. Selektorn utvärderas
en gång. Case-kroppar, etikettidentitet, fallthrough och goto-modellen flyttas
inte. Om normalisering vänder/överlappar intervall eller allokering misslyckas
används den befintliga kedjan. Ingen IR, SSA eller extra funktionspass byggs.

`switch_dispatch.c` och `vcc-opt-rules.py --rule switches --run` passerar
O0–O3 för x86, arm64, wasm32 och wasm64: osorterade glesa fall, GNU-intervall,
signed/unsigned 32/64-bitars gränser, intervall över noll och nästan hela
unsigned64-domänen, default utan/med case, fallthrough, nested case i if(0),
goto förbi selektorn och wide-fallback. Ett faktiskt utskrivet CFG med 31
sparse case har högst 9 villkorsbeslut på x86 och 10 på arm64 (tidigare 31);
wasm har fyra balanserade splitnivåer och behåller den täta tabellen. Den gamla
basens kedja underkänns av kodkravet. Sanitiserade kontrakt kontrollerar
normalisering, etikettidentitet, överlapp, densitets-overflow och tabellgräns.
Unity och fil-för-fil bygger; fil-för-fil kör även x86/wasm64-fallen O0–O3.
O0- och `VCC_NOOPT`-assemblern är byte-identisk mot basen för alla fyra mål.

Kostnad mot exakt fded15a, identiska clang23-byggen med
`-O3 -march=x86-64-v3`, Callgrind 3.26 för hela kompilatorprocessen
`vpp --target=... -O2 -S`. Tre avgränsade korpusar om 500 funktioner:

| Mål | Rak heltalskod, Ir | Summeringsloop, Ir | Små tre-case-switchar, Ir |
| --- | ---: | ---: | ---: |
| x86 | −0,0123 % | −0,0069 % | +0,0351 % |
| arm64 | −0,0242 % | −0,0104 % | +0,0356 % |
| wasm32 | +0,0095 % | +0,0258 % | +0,1795 % |
| wasm64 | +0,0097 % | +0,0259 % | +0,1794 % |

Alla tolv assemblerpar är identiska. Råa instruktionstal, binärhashar och
kommandon finns i `work/opt-profile/switch-profile.json`, respektive `.cg`/
`.log`, med reproducerbar mätare `switch-measure.py`. Den portabla
switchregressionens x86-O2-binär exekverar 1 183 812 → 610 952 instruktioner
(−48,39 %) och 396 541 → 109 244 villkorsgrenar i Callgrind; detta är ett
riktat kodkvalitetsprov, ingen generell runtime-/nativekostnadsgrind.
`switch-runtime-profile.json` innehåller hash/kommandon för just detta prov.
Värdetabeller, utökad inlining och oanvända lokala initierare återstår i OPT6;
övriga O2-steg och de breda G5/G7-grindarna är fortsatt öppna.

OPT6-integrering: 19c6a63 är switchimplementationen ovan, därefter har PR47:s
main-uppdatering b63c269 mergats. På kombinationen passerar unitybygget,
16/16 riktade switchfall, sanitiserade plankontrakt och 219/219 arm64-körtester
(ett befintligt xfail) samt 214/214 x86-körtester (sex referensskips).
Fil-för-fil-bygget hittade en ärvd synlighetsmiss i autozero-koden:
`stmt.c` anropade `expr.c`-lokala `wk_store`. Hjälparen är nu VPRIV med
prototyp i walk.h. Efter fixen bygger fil-för-fil och passerar samtliga
164 regel-/nivå-/målkombinationer med körning samt x86-debugkontrollen O0–O3.
Mätningarna ovan avser uttryckligen fded15a + 19c6a63:s kompilatorkod,
inte main-uppdateringens separata ändringar.

### OPT8 status: unsigned32 konstantdivision (2026-11-11)

`emit/opt/udivconst.c` bygger en typad 32-bitars reciprok med fast
multiplikation/shift och vid behov add-korrektion. Konstruktionen har
kontrollerats mot Hacker's Delight kapitel 10 och
[libdivides referensgenerator](https://github.com/ridiculousfish/libdivide/blob/master/libdivide.h),
med separata clang23-prober. Divisorer större än 2^31 behöver bara en unsigned
jämförelse. Noll, tvåpotenser, signed, 64/128 bitar och `_BitInt` behåller sina
befintliga vägar. Detta är ett avgränsat u32-delsteg, inte hela OPT8/O2.

Separata adaptrar i `arch/a64/emit/udivconst.c`, `emit/walk/udivconst.c` och
`arch/wasm/emit/udivconst.c` använder befintlig 32→64-multiplikation; ingen
SSA/IR, allocator eller ny funktionsgenomgång. O1–O3 använder regeln; O0 och
`VCC_NOOPT` är byte-identiska mot basen för alla fyra mål. Native normaliserar
lata smala värden före 64-bitars produkten och behåller originalet för rest
eller korrektion. Den faktiska registertoppen kontrolleras före allokering:
en konstant plus eventuellt sparat original/korrektion, och ytterligare ett
register om input fortfarande är en adress. Tryck behåller vanlig division.
Wasm väljer bara enkel reciprok-kvot och jämförelsekvot; add-korrektion och
rester är uttryckligen kvar i `div_u`/`rem_u` tills deras större sekvenser har
nytta. Det valda wasm-receptet skickas vidare utan en andra hostdivision.

`vcc-udivconst.py --sanitize` kontrollerar divisorer 2–65 för alla n<65536,
4096 seedade fullbreddsdivisorer, gränser runt tvåpotenser/multipler samt
kända reciproker för 3/7/10, mot oberoende division och modulo. Kontrakten
prövar även båda native-adaptrarnas registerbudget vid 0–8 fria register,
adress-/registerinput, korrektion/rest och reserverade register.
`unsigned_divconst.c` kör normal/lazy-cast input, konverterade divisorer,
17 divisorklasser, compound /= och %=, sidoeffekter, volatile och index++.
Ett djupt nästlat adressuttryck prövar native-tryck; arm64 behåller där udiv.
Unity och fil-för-fil bygger; 180 regel-/nivå-/målkombinationer körs,
220/220 arm64-regressioner (ett befintligt xfail), 215/215 x86-regressioner
(sex referensskips) och debug O0–O3 passerar. Fil-för-fil-binären kör också
alla 16 nya u32-fall.

Mätbas: exakt 781ca69; identiska clang23-byggen `-O3 -march=x86-64-v3`,
Callgrind 3.26 för hela `vpp --target=... -O2 -S`-processen. Korpusar om
500 funktioner för rak heltalskod, summeringsloop och avsiktligt mättad
`x/7u + x%10u`-kod:

| Mål | Rak kod, Ir | Loop, Ir | Mättad division, Ir |
| --- | ---: | ---: | ---: |
| x86 | +0,0050 % | −0,0019 % | +4,32 % |
| arm64 | +0,0263 % | −0,0041 % | **+19,54 %** |
| wasm32 | +0,4416 % | +0,0067 % | +1,06 % |
| wasm64 | +0,4410 % | +0,0067 % | +1,06 % |

Alla åtta icke tillämpliga kontroll-/loop-assemblerpar är identiska. Den
mättade arm64-kostnaden är ett verkligt byte av kompilering mot dyrare
jämförelse-/multiplikationssekvenser: två udiv per funktion försvinner, men
antal emitterade instruktioner ökar. Wasm behåller båda operationerna i just
mättnadsprovet enligt sin kostnadspolicy. En separat konfigurerad mix av
befintliga C-regressioner, med samma c23/resource/runtime-include-flaggor som
tidigare OPT5-mätning, ger följande instruktionsarbete:

| Mål | Bas Ir → ny Ir | Skillnad | Filer |
| --- | ---: | ---: | ---: |
| x86 | 132 564 698 → 132 542 609 | −0,0167 % | 12 |
| arm64 | 141 117 374 → 141 112 147 | −0,0037 % | 12 |
| wasm32 | 87 119 802 → 87 135 002 | +0,0174 % | 10 |
| wasm64 | 97 155 779 → 97 165 511 | +0,0100 % | 11 |

Wasm32-basens `atomic_ops` (layoutassert) och `bitint_runtime`, samt wasm64-
basens `bitint_runtime`, kompilerar inte med denna konfiguration och ingår
inte; exakta diagnostiker är sparade. Samma typer/filer stöds på native.

Den mättade aritmetikklassens x86-körprov, 3 miljoner LCG-indata per process,
två warmups och nio alternerade par, ger median barn-CPU 7,374→3,277 ms
(−55,56 %); båda svarar 3967102618 som clang. Detta mäter just denna värd
och aritmetikklass, inte arm64-hårdvara. För wasm undersöktes faktiskt
clang23-O2-utdata: `/3u` behåller `i32.div_u`. Ett separat Node v26.13.0-prov
med förvärmda moduler, nio alternerade par och 3 miljoner LCG-indata för
vardera 3/5/10/11/2147483649 ger −1,5…−5,1 % median väggtid med
`--no-liftoff` och −28…−31 % med `--liftoff-only`. Alla svar är lika; den
verifierade objektkoden är oförändrad efter recept-/tryckfixarna. Resultaten
motiverar den begränsade wasm-klassen på denna engine/värd; inga andra engines
eller generella prestandalöften ingår.

Kommandon, källor, hashvärden och rådata finns i `work/opt-profile/udivconst/`:
`compiler-profile.json`, `mixed/result.json`, `native-runtime-profile.json`,
`runtime-{turbofan,liftoff}.json` och respektive `*-measure.py`/`runtime.js`.
Signed och u64 konstantreciproker, billigare wasm-rest/korrektion, tail calls,
bredare G5/G7-grindar och full O2-täckning är fortsatt öppna.

### OPT6: gemensamma rena kontrollbeslut (delsteg)

`emit/opt/control.c` bevisar konstanta heltalsvillkor, logisk negation,
kortslutning och `__builtin_strcmp` mellan rena smala strängliteraler. Den
gemensamma besökaren stannar efter högst 64 noder och besöker aldrig en död
högeroperand. Okända anrop, volatile/atomic, satser och inlining lämnas till
den befintliga genereringen; ingen SSA eller AST-mutation tillkommer.
`__builtin_constant_p` använder respektive måls befintliga policy genom en
callback. Arm64:s callback behåller sin befintliga dry-generering med
återställning, och x86:s tidigare dubbla formel i builtin-/villkorsvägen är
sammanslagen. Det gemensamma beviset anropas en gång vid respektive faktisk
villkorsingång, inte igen via den delade range-hjälparen `cond_fold`.

Nya beslut gäller O1–O3 med optimeringar aktiva. Åtta fokuserade källfiler
är byteidentiska mot `781ca69` för både O0 och O2 med `VCC_NOOPT=1`, på
x86, arm64, wasm32 och wasm64: 64 jämförelser. Native-målens gamla O0-regler
för strängjämförelser är bevarade. Det nya kör-/kodformsprovet passerar
16 nivå-/målkombinationer, även fil-för-fil, och kontrollerar exakt en
utvärdering, bevarade globala volatile-/atomic-laddningar, vanliga
användardefinierade strcmp-anrop samt höga strängbyte och inbäddad NUL.
Nio befintliga native-test för constant_p/inlining passerar. Wasm32/64
passerar båda `if_call_const` och `cond_dead_and`; bara dessa två xfail
är borttagna. Övriga sex granskade luckor kräver fortsatt arbete med
switchsegment, värdefakta för statement expressions eller inlining.

Kostnaden mäts mot exakt `781ca69`, lika värdflaggor
`-O3 -march=x86-64-v3`, hela processen i Callgrind, 500 funktioner per
prov, O1/O2. De rena skalärkontrollerna är i praktiken oförändrade.
Pekarslingans kostnad är cirka +0,094 % x86, +0,065 % arm64 och −0,002 %
wasm. Positivt kortslutnings-/constant_p-prov minskar värdinstruktionerna
med cirka 16,90 % x86, 2,75 % arm64 och 18,15–18,36 % wasm. Detta är
avgränsad kompileringskostnad, inte hårdvarucykler eller en full grind.
Den första provvarianten med null-callback på native gav extra arbete
utan vinst och ersattes; dess profiler är sparade separat. Bredare
prestandagrind, CI och full O2-täckning är fortsatt öppna.

Slutlig verifiering av samma implementation: 164 befintliga regel-/nivå-/
målkombinationer passerar med körning. De nya 16 kombinationerna passerar
också efter fil-för-fil-bygge. Råprofiler, kommandon och käll-/binärhashar
finns lokalt i `work/opt-profile/control-final-cost/result.json`; O0/noopt-
jämförelserna ligger i `work/opt-profile/control-parity/result.json`.
Den generella algoritmen gör ingen extra helträdsanalys för accepterade
bevis; wasm:s befintliga fallback för övriga former är avsiktligt kvar.

Efter integration av `54580f7` (PR48:s regressionsunderlag) bygger unity
och alla 16 nya nivå-/målkontroller passerar igen. Hela egna native-sviten
ger 216/216 x86-PASS med sex synliga referensundantag och 221/221 arm64-PASS
med ett befintligt kört XFAIL. Loggar finns i
`work/opt-pr-tmp/control-integrated-{build,x86,arm}.log`.

### OPT5 regressionsunderlag: genererade minnesceller (2026-11-11)

`bench/gen-memory-cells.py` skapar deterministiskt 12 funktioner med totalt
384 operationsgrupper och 24 bokstavliga kontrollsummor från en separat
Python-tolk med explicit breddmaskning. Sex funktioner behåller privata
32-bitarsceller; sex täcker adressflykt via noinline-hjälpare. Smala celler,
källregisterändringar, kontrollflöde, unioners flyttalspayload och volatila
accesser ingår. CI kontrollerar att den incheckade C-filen är regenererbar;
den vanliga körsviten hittar filen automatiskt. Körresultat kan inte bevisa
att olästa volatila lagringar finns kvar; det kräver det separata
`vcc-slot-observable.py`-kontraktet. Detta är avgränsat regressionsunderlag,
inte ett prestandabevis eller full O2-täckning.

Verifiering: O1/O2 på x86, arm64/QEMU och wasm32/64 gav 192 matchande
bokstavliga kontroller; Clang ASan/UBSan och generatorns `--check` passerar.
Strukturens fält initieras separat: aggregatinitiering och arrayindexering
skapar konservativ adressflykt i nuvarande adaptrar. Ett positivt O2-fall
på samma experimentella x86-binär med slots av/på går från 88 till 77
frame-moves (382 till 378 instruktioner). Motsvarande wasm32-fall går från
46 till 32 i32.load och 31 till 29 i32.store mot tidigare referensbinär.
Det sista är en avgränsad jämförelse mellan binärer, inte isolerad kostnad.

OPT5 gemensam cellklassificering: em_slot_collect samlar intervall i en enda
ström. Första exakta intervallet sparas när en allokering övergår från ett till
flera intervall; alla följande intervall går direkt till gemensam klassificering.
Anropare behöver varken spela upp posterna eller skapa en separat indexbuffert.
Blanda inte insamlingslägen efter övergången till flera intervall.
ASan/UBSan: samma deterministiska intervalloracle för båda API-lägena,
400 slumpade återanvändningar av scratch, sortgränser 15/16/17, överlappningar,
ID0-förgiftning, escapes, duplicerade första intervall och ogiltiga ID/storlekar.
Mutationen som tappar första intervallet avvisas. x86 används först;
arm64/wasm kan byta till hjälparen separat med paritets- och kostnadskontroll.


OPT5 strömmande arm64-/wasm-insamling (2026-11-11): båda adaptrarna använder
nu `em_slot_collect`; den andra hela genomgången av poster/händelser försvinner.
Hjälparen validerar kända intervall en gång och uppdaterar allokeringsstatus direkt;
ID0 använder oförändrad fysisk förgiftning. Klassificering, värdeflöde och utskrift
är oförändrade. Separata backendfiler behålls.

Matchad v3-mätning (`-O3 -march=x86-64-v3`, hela processens Callgrind-Ir,
O1/O2, bas 781ca69 mot 54580f7 + dessa ändringar) ger för tvåfält/500 funktioner
arm64 −0,294 %, wasm32/64 −0,160 %; för 16 fält/100 funktioner −0,453 %
respektive −0,145 %. Ett fält/500 funktioner kostar +0,053 % respektive
+0,050 %. Skalär kontroll +0,006–0,007 %, loopkontroll arm64 +0,071 %,
wasm +0,006 %. Detta är instruktionskostnad vid kompilering, inte körtid.
Första access+collect-varianten och en mellanvariant avvisades: wasm ökade
på stora fallet (+0,246 %). Slutversionen behålls eftersom flerfältsfallen
förbättras på båda mål utan stor enkelfältsregression. Rådata, exakta kommandon,
käll-/binärhashar och byteparitet finns lokalt under
`work/opt-profile/stream-collectors-{direct-cost,direct-large-cost,parity}`;
avvisade `stream-collectors-{cost,first-range-cost,large-cost}` finns kvar.
Basens kompilatorfiler matchar 54580f7; endast testfixturen från PR48 skiljer.

Verifiering: ASan/UBSan för slumpad intervalloracle i båda API-lägena och
mutation som tappar första intervallet; 118 byteidentiska par (O1/O2, fyra mål,
inklusive 24 arm64-objekt), separat observerbara volatile-/atomic-accesser med
mutation samt wasm32/64 celltester O0–O3. Två wasm32 atomic_ops-par avvisas
identiskt av basen p.g.a. testets LP64-antaganden och räknas inte som godkända.
Unity- och separatfilbygge med j2 passerar, liksom värdetillståndets sanitizerkontrakt.
Ingen ny emissionsregel eller full O2-täckning; full prestandagrind återstår.


OPT5 fysisk förgiftning, liten uppföljning: när två senast insamlade ID0-intervall
har exakt samma offset och storlek sparas bara ett. Klassificeringen använder
intervallens union; validering sker fortfarande före jämförelsen. Ingen ny
buffert eller status införs. Slumporaclet innehåller nu upprepade poison-runs;
en andra mutation som ignorerar storleken avvisas av ett samma-offset/bredare-fall.
ASan/UBSan, båda mutationer, 118 byteparitetspar och observerbarhetskontrakt
passerar. Mot föregående d870b64 minskar en avgränsad upprepad volatile-range
kompileringens Ir med 0,042 % på wasm och 0,039 % på arm64; alternerande
intervall utan träff kostar 0,116 % respektive 0,105 %. Tidigare kontroller och
flerfältsfall är i praktiken oförändrade. Båda belastningarna finns kvar i
`work/opt-profile/stream-collectors-prune-poison{,-repeat}-cost/result.json`;
hela slutversionens basjämförelser finns i `stream-collectors-prune{,-large}-cost`.
Separat x86-integration rapporterar 13 397 färre Ir i sin riktiga grind;
det är inte en full grind för denna arm64-/wasm-ändring. Ingen emissionsändring.

### OPT4 fortsättning: budgeterad flödeskärna och första x86-konsument (2026-11-11)

`emit/flow/{graph,maskflow}.c` bygger validerad blockadjacens med valfria
föregångare och löser bakåtriktad 64-bitars may-liveness. Källpositioner,
register/effekter och omskrivningar tillhör adaptern; kärnan antar varken
maskinposter, fysisk registermodell, wasm-stack eller SSA. Arrayer återanvänds
per worker. Varje block-/kantbesök belastar en uttrycklig arbetsbudget;
resultatet är giltigt först efter full konvergens. Felaktiga kanter,
allokeringsfel eller budgetslut ger inga beroende omskrivningar.

Första nyttan finns i `arch/x64/emit/dead.c`: en absorberande heltalsregel
som lämnar ett redan emitterat registerresultat oanvänt sätter en billig
kandidatflagga. Utan den byggs ingen graf. Konsumenten tar bara bort rena,
icke-trappande registeroperationer vars samtliga utdata är döda. Läsningar,
lagringar, atomics, FP-effekter, anrop och barriärer bevaras. Flaggläsningar
propageras konservativt bakåt; en flaggskrivning antas inte döda hela
flaggtillståndet. Partialregister och implicita register tas ur målets
befintliga effekter. Epilogens ABI-resultat, frame-/stackbaser och
callee-saved-register är konservativt levande vid utgång.

Gränserna är 256 block, 1024 kanter och 65536 poster; solverbudgeten är
`n + 32*(block+kanter) + 64` besök. Det är implementationens fallbackgränser,
inte en uppmätt kompilatorkostnadsgrind. Okända/indirekta flöden, rå/named
asm, label-adresser och interrupt-/noreturn-konventioner avstår helt.
Alla ändringar väntar tills analysen lyckats; kompaktering använder befintlig
`dbg_remap` före tvåadresspasset. Arm64:s befintliga block-/livenessanalys är
oförändrad. Vid detta första checkpoint använder wasm ännu inte grafen;
den separata wasm-konsumenten beskrivs i nästa statusavsnitt.

Kodformsbevis mot exakt uppdaterad förälder `0dde275`: `unsigned f(unsigned
x) { return (x*13u)&0u; }` behåller `imull $13` i basen men inte i kandidaten
på `7f88daa`. Skyddade stackläsningar står kvar. Detta visar en verklig
begränsad lucka/vinst, inte generell DCE eller uppmätt körtid. Basbinärens
SHA256 är `a26750515e4e215f6485e3f75624a5422d9b29b25d9268bb81c9e3dfbb2fc8c6`;
assembly och bygg-/testloggar finns i `build/linux/flow-evidence/`.
Värdflaggorna skiljer mellan dessa byggen, så de används inte för kostnads-
jämförelse. Senare main-integrering och dess verifiering redovisas nedan.

Verifierat hittills: unitybygge med `-j 1`; `--rule deadresults --run`
passerar 16/16 nivå-/målkombinationer (x86, arm64/QEMU, wasm32/64 med Node
26.13). Positiva x86-kodkrav gäller O1–O3; kontroller behåller division,
volatile access, anrop och opaque-asm-fallback. `vcc-flow.py` jämför med en
oberoende bitvis vägnåbarhetsmodell, inklusive loopar, joins, dublett-/self-
kanter, exakta/tomma budgetar och återanvändning efter fel. `vcc-rec-dead.py`
prövar riktiga posteffekter, ABI, flaggor/carry, partialregister,
kompakterings-/debugcallback och transaktionellt budget-/opaque-fallback.
Båda kontrakten passerar ASan/UBSan och ligger i den snabba CI-grinden.

Naked-proben är kodformsunderlag: giltig asm-kropp är opaque och avstår.
Vcc:s befintliga ignorerade naked-attribut ger fel prolog för direkt asm-ret;
detta reproducerades även utan det nya passet. Proben kör därför inte den
unsupported funktionen. Clang 23 avvisar motsvarande rena C-kropp med
`non-ASM statement in naked function is not supported`. Vanlig asm med
absorberande uttryck provas däremot både i kodform och vid körning.

Efter integration av uppdaterad PR53, leaf50 och ledgern (`7691f6c`,
förälder `69c1bd7`) bygger både unity och fil-för-fil med `-j 1`. Även
fil-för-fil-binären passerar alla 16 nya regel-/nivå-/målkombinationer.
Kontrollerna för carry, partialregister, overflow, loop och båda ABI-
resultatparen innehåller nu faktiskt evaluerade absorberande uttryck, så
den nya konsumenten aktiveras; utvalda kontroller kräver också att den
extra multiplikationen verkligen försvinner. En separat vanlig funktion
utan kandidat stannar utan graf-/scratchallokering i kontrakttestet.

Egna x86-sviten ger 223/223 PASS och sex explicita referensskips; arm64
228/228 PASS och ett befintligt XFAIL (`--no-cache`, `-j 1`). Befintliga
cross-compiler-ABI-fixturer ger 32/32 på vardera native-målet. Debugharnessen
har ett faktiskt aktiverat dead-result-fall med en efterföljande källrad;
O0–O3 passerar radkrav, DWARF-verifiering och identisk `.text` med/utan `-g`.
Mot exakt `69c1bd7` är 17/17 assemblypar byte-identiska: fyra mål vid O0 och
O2/noopt, samt arm64/wasm32/wasm64 vid O1/O2/O3. Samma förälder behåller
fortfarande `imull $13` i det positiva x86-provet. Råloggar ligger i
`work/opt-pr-tmp/flow-final-{x86,a64}.log`; övriga artefakter, kompletta
paritetskommandon och käll-/binärhashar i `build/linux/flow-evidence/`.

Kostnad mot exakt `69c1bd7`, båda byggen med Clang 23
`-O3 -march=x86-64-v3`, Callgrind för hela `vpp -S`-processen. 21 seriella
par; måttet är värdinstruktioner, inte hårdvarucykler eller väggtid:

| Korpus | Mål/nivå | Ändrat instruktionsarbete | Genererad assembler |
| --- | --- | ---: | --- |
| Rak kod, 500 funktioner utan kandidat | x86 O1 och O2 | +0,0468 % | Identisk |
| Summeringsloop, 500 funktioner utan kandidat | x86 O1 och O2 | +0,0009 % | Identisk |
| Avsiktligt mättade dead-result-diamanter, 500 funktioner | x86 O1 och O2 | **+7,7486 %** | Döda multiplikationer försvinner |
| Tolv befintliga C-regressioner, summerat | x86 O2 | +0,06964 % | 10 identiska, 2 ändrade |
| Rak kontrollkod, 500 funktioner | arm64 O2 | +0,0221 % | Identisk |
| Samma kontrollkod | wasm32 och wasm64 O2 | +0,0187 % | Identisk |

Den blandade sviten kostar 140 485 975 → 140 583 816 instruktioner, utan
exkluderade filer. Den använder tidigare `deep_mixed`, `register_locals`,
`bit_cast`, `stack_cells`, `integer_identities`, `deep_conditional`,
`unsigned_pow2`, `signed_pow2`, `atomic_ops`, `mem_builtin_inline`,
`bitint_runtime` och `wasm_frame_cells`, inte den nya positiva fixturen.
Tradeoffen är verklig: mättad kandidatkod kostar cirka 7,75 % mer att
kompilera; kontroll-/blandproverna ligger under +0,1 %. Ingen generell
runtimevinst, minnesgrind eller komplett prestandagrind påstås. Körningen
är en instruktionräkning under delad maskinlast, inte en tidsjämförelse.
Råa `.cg`/loggar/assembly, versionssträngar, kompletta kommandon och käll-/
binärhashar finns i `work/opt-profile/flow-cost/{measure.py,result.json}`.

Fjärr-CI och nästa wasm-konsument återstår. OPT4/O2 och G5/G7 är fortsatt
öppna.

### OPT5 fortsättning: wasm-DSE över strukturerade joins (2026-11-11)

På `1d8e29a`, med flödesföräldern `7691f6c`, använder nu även wasm32/64
den gemensamma graf-/masklösaren. `arch/wasm/emit/flow.c` registrerar typade
block-, if/else-, slut- och branchhändelser från funktionens början under
ordinarie emission. Varje öppning får en unik token; wasm:s återanvända
lexikala branch-depth översätts till token när grenen emitteras. Ingen
assemblertext tolkas, extra AST-vandring eller SSA införs inte.

Första konsumenten tar bort lagringar till privata exakta 4/8-byte ramceller
som skrivs över före varje följande läsning, även genom nästlade diamanter.
Heltal och flyttal stöds; befintlig cell-/överlapps-/escapeanalys avgör
behörighet. Adress och högerled utvärderas fortfarande av befintlig renderer,
inklusive eventuella fångade wasm-locals. Rena kontrollgränser bryter den
lokala vidarebefordringen men är inte minnesläsningar i DSE. Riktiga anrops-/
minnesbarriärer sammanslås bara inom samma kontrollepok: en barriär efter
en tom else/join får inte flyttas till bara ena föregångaren.

Liveness förenar efterföljares läsbehov och betraktar alla celler som lästa
vid funktionsutgång. Detta är alltså överskrivnings-DSE, inte generell
unread-/livstidssluts-DSE eller global värdevidarebefordring. Alla beslut
väntar på full konvergens och publiceras först efter separat staging.
Loopar, switch/dispatchers, goto, asm, SJLJ och dynamiska ramar avstår.
Gränser: 8192 kontrollhändelser, djup 256, 64 cellbitar och högst 1048576
solverbesök. Graf-/solverarbete startar först efter komplett emission och
verkliga privata upprepade stores genom en join. O0/noopt avstår; fel och
budgetslut lämnar denna konsuments omskrivningar opublicerade.

Verifierat: unitybygge med `-j 1`, fristående syntaxkontroll av samtliga
ändrade wasm-C-filer samt **114** SDK-fria konfigurationer genom egna
assemblern/länkaren och Node: 10 nya flödeskontroller (wasm32/64 O0–O3
med körning och kodform, samt O2/noopt-kodform), 16 befintliga cellfall och
88 opt-regelkontroller. Positiva fall behåller exakt en slutlig store;
negativa fall täcker läsningar/utgångar, escape, överlapp, volatile/atomic,
anrop, effekter, flyttalsbitar och uteslutet flöde. En isolerad mutant som
tar bort epokvillkoret avvisas av den avsedda tom-else-kodkontrollen på
både wasm32 och wasm64; originalkällorna ändrades inte. Oberoende review
fann inga kvarstående korrekthetsblockerare. `vcc-wasm-flow.py` ligger i
wasm-jobbets snabba CI-uppsättning; fjärrkörning återstår.

Hela egna SDK-baserade sviter på samma `1d8e29a` avslutar med status 0:
wasm32 **178/178 PASS**, 36 körda XFAIL och 16 skips (12 clang-reject,
1 noref, 3 ref-fail); wasm64 **182/182 PASS**, 37 körda XFAIL och 11 skips
(8 clang-reject, 1 noref, 2 ref-fail). Undantagen är inte PASS.
Loggar: `work/opt-pr-tmp/wasm-flow-full32.log` och `wasm-flow-full64.log`;
källbevis, mutanthashar och fokuserade resultat i
`build/linux/flow-evidence/`. Efter sammanslagning av den exakta
flödesföräldern `f7327bd` till `1458a90` passerar återigen alla 114
konfigurationer och fristående syntaxkontroll av ändrade wasm-C-filer.
66 O0-/O2-noopt-par är byteidentiska mot den föräldern; två ytterligare
par avvisas av båda byggenas befintliga ILP32-layoutassert i `atomic_ops`.
De SDK-baserade helsvitssiffrorna ovan hör fortfarande till `1d8e29a`,
inte den senare integrationen.

Avgränsad kostnadsjämförelse: exakt `f7327bd` mot `1458a90`, båda vpp
byggda av clang 23 med `-O3 -march=x86-64-v3`. Callgrind räknar hela
processen inklusive start, en seriell mätning per binär/källa/mål, wasm32
och wasm64 med `-O2 -S`, uttryckliga LLVM-resurs-/rt-huvuden och samma
källfiler. Inga väggklocke- eller programkörtidsvärden används.

| Prov | wasm32 extra värd-Ir | wasm64 extra värd-Ir |
|---|---:|---:|
| Rak skalärkod, 200 funktioner | +0,314 % | +0,314 % |
| Villkor utan privata ramceller, 200 funktioner | +0,552 % | +0,552 % |
| Loopar som avstår från analys, 200 funktioner | +0,351 % | +0,351 % |
| Positiva diamanter, 200 funktioner | +4,495 % | +4,494 % |
| Positiva nästlade diamanter, 200 funktioner | +4,683 % | +4,682 % |
| Befintlig fast blandning, processernas summa | +0,106 % | +0,095 % |

Kontrollernas assembler och objekt är byteidentiska. De positiva proven
minskar 800 respektive 1000 stores till 200; separat vanlig `-c` ger
2,886–3,325 % mindre relocerbara objekt totalt. Detta är objektstorlek,
inte laddad kodstorlek eller uppmätt programfart. Aktiv analys kostar
alltså mer än 1 % i dessa koncentrerade positiva prov; tabellen gäller
inte som en generell kostnadsgrind. Den fasta blandningen är elva
befintliga regressionsfiler för wasm64 och tio för wasm32: `atomic_ops`
avvisas av både bas och kandidat på sin LP64-layoutassert i wasm32 och
utesluts öppet. Två av blandningens assemblerutdata ändras per mål.

Fulla kommandon, käll-/binärhashar, råa Callgrind-filer, objekt och
feltexter finns i `build/linux/flow-evidence/cost/result.json` med
angränsande artefakter; pariteten i `noopt-parity.json`. Dessa mätningar
hör till denna konsument och ersätter inte full prestandagrind,
programkörtidsmätning eller fjärr-CI. OPT5/O2 och G5/G7 är fortsatt öppna.

### Fortsatt O0–O2-täckning: familjeledger efter OPT0–OPT8 (2026-11-11)

**OPT0–OPT8 är första delsteg, inte hela O2-målet.** Den här kartan binder
fortsättningen till verkliga inventarier och källkod i den privata
kombinationen `da64f05` (ingen ny kompilatormätning i detta dokumentsteg).
`bench/optaudit/coverage-ledger.json` grupperar **varje** distinkt post i
rapportens GCC-`enabled_options` och LLVM-`observed_passes`, O0/O1/O2, exakt
en gång i 20 familjer. Där finns fullständiga namn, nivåmedlemskap,
konkreta kodfiler per mål, familjestatus och återstående arbete. Det är en
avprickad *inventariemappning*, inte full optimeringstäckning.

Underlag: `work/opt-profile/audit-switch-integrated/report.json`, SHA256
`cb92d9f847cc89ce574ef6fccc91c2ad8ab862a0989f0fa19c76cf607a1658ca`.
Rapportens vcc-revision är `54580f7`, alltså **före** kodkombinationen ovan;
dess assemblymått får inte märkas om till `da64f05`. Referenserna är GCC
15.2.0 (Ubuntu 15.2.0-16ubuntu1) och Ubuntu Clang 23.1.6 (byggrevision
c47d7823685a), på Ryzen 7 9700X. Samma 29 C-funktioner används vid
`-march=x86-64 -fno-pie -fno-stack-protector -fcf-protection=none`.
Probe-/anroparkällornas SHA256, vpp-binärhash, full versionssträng och
SHA256 för granskade kompilatorkällor finns i annexet.

| Inventarium | O0 | O1 | O2 | Union O0–O2 |
| --- | ---: | ---: | ---: | ---: |
| GCC: booleska alternativ märkta `[enabled]` | 50 | 92 | 141 | 142 |
| LLVM: unika observerade `Running pass`-namn | 6 | 74 | 87 | 88 |

Aktiverat GCC-alternativ betyder inte att motsvarande transform kördes
eller ändrade något; även O0 listar flera sådana alternativ. Namn som
`-fno-inline-stringops`, FP-/aliasregler och unwind är dessutom policy.
Ett observerat LLVM-pass kan vara en tom genomgång, en wrapper eller
metadatahantering. Rapportens GCC-`configured_passes_on` är ett separat
inventarium och ingår inte i denna namnmappning. Parameterstyrda GCC-val,
fullständiga maskinpass, körordning/antal, andra ISA:er, O3, LTO och PGO
är inte inventerade här. Backendkolumnerna nedan är källgranskning,
**inte** nya GCC/Clang-inventarier på arm64/wasm eller nya körbevis.

I tabellen betyder **delvis** att en verklig begränsad implementation
finns, inte att alla flaggor/pass i familjen täcks. **Saknas generellt**
betyder att ingen motsvarande generell analys/transform identifierades
i den granskade kodvägen; **overifierat** kräver riktade prober innan
ett påstående om stöd eller avsaknad görs.

| Familj, exempel ur inventariet | Faktiskt underlag i vcc och kvarvarande lucka |
| --- | --- |
| Heltalsförenkling: InstCombine, reassoc, div/rem | **Delvis, alla tre.** `opt/{identity,divpow2,sdivpow2,udivconst}.c` med separata adaptrar. U32-reciproker har målkostnad/fallback; inte generell InstCombine. Små konstantmultiplikationer, fler bitregler, delat kvot/rest-recept och signed/u64-reciproker återstår. Float2Int är inte bevisat av heltalsreglerna. |
| Värdefakta, CSE/PRE, intervall: CCP, GVN, SCCP | **Delvis lokalt.** `rec/copies.c`, `rec/slot-values.c` och förpassfakta; a64:s `opt.c`, x64:s `copies.c`/`extensions.c`, wasms `slots.c`. Ingen gemensam global GVN/PRE/SCCP eller generell intervallanalys. Börja med begränsad blocklokal cache för rena uttryck. |
| Död kod och minne: DCE/DSE, store merging | **Delvis.** Gemensamma exakta frame-celler/storefakta och nåbarhetsstädning; a64 har egna dead-result-pass, x64 effektbeskrivning, wasm typade accesshändelser. Heap/global-DSE, bitvis DCE, allokeringseliminering, generell last/store-motion och looplasteliminering återstår/är overifierade. |
| Kontroll och switch: SimplifyCFG, jump threading, bit tests | **Delvis.** `opt/control.c`, `opt/switch.c`, `rec/{fallthrough,unreachable}.c`; alla mål använder delade beslut, begränsade medlemsmasker och värdetabeller, wasm har tät `br_table`. Bitset-kluster med flera destinationer, generell threading, compare merging och tail merging återstår. |
| Skalär ersättning/promotion: SROA, mem2reg | **Delvis.** `opt/locals.c`, förpass och celler; a64 registerbindning, x64 fyra valda GPR-hem, wasm typade lokaler. Detta är inte generell aggregatsplittring eller join-medveten mem2reg. |
| Loopmotion/induktion: LICM, IV opts, loop sink | **Saknas generellt.** Alla mål sänker loopar; förpassets loopviktade lokalval är inte LICM. CFG, dominans, loop-/beroendefakta och bevis om noll iterationer behövs före säker hoisting/IV-omskrivning. |
| Loopform: rotation, deletion, unroll, unswitch/distribute | **Saknas generellt.** Målen har direkt satssänkning, inte dessa transformpass. Kräver trip-count-/effekt-/entrybevis och kodstorleksbudget; börja eventuellt med små exakt konstanta loopar. |
| Vektorisering: loop, SLP, VectorCombine | **Delvis endast explicit.** A64 `expr.c:gen_vbin` har utvalda NEON-operationer plus lanefallback; x64-walkern och wasm `wexpr.c` använder lanevis väg för granskade GNU-vektorer. Packed SSE2/simd128-sänkning är ett eget tidigare steg. Automatisk loop/SLP kräver beroende-, alignment-, reduktions-, tail- och kostnadsmodell. |
| Inlining/anrop: early/partial inline, TailCallElim | **Delvis, asymmetriskt.** A64 `call.c:can_inline`, x64 `walk/stmt.c:wk_can_inline` har olika behörighet; x64 stöder redan små vanliga statiska funktioner. Wasm `wcall.c` saknar motsvarande inliner. Delad policy, wasm-instansiering och begränsade tail calls återstår; general varargs/partial inline är senare arbete. |
| IPA/globalt: IPSCCP, attrs, global DCE, ICF | **Begränsade förpassfakta; generell IPA saknas.** `prepass.c`/`tu.c` och native-inlining är inte helprogramsanalys. Callgraph, linkage/interposition och escape/modref-sammanfattningar behövs. Länkarens section-GC är inte compiler-GlobalDCE eller ICF; C++-devirtualisering är en annan språkfråga. |
| Bibliotek/idiom: strlen, CRC, MemCpyOpt | **Delvis.** Delad literal-`__builtin_strcmp`, utvalda native-minnesbuiltins; wasm har sin egen anropsväg. Loopidiom, generella memcpy-kedjor och strlen/printf/CRC-förenklingar är inte etablerade. Behåll freestanding-, builtin-, bounds- och overlap-kontrakt. |
| Registerpolicy: IRA/LRA, remat, caller saves | **Delvis, annan modell.** Gemensamt lokalurval, a64 `bind.c`/`opt.c`, x64 `locals.c` och temporärpooler. Ingen generell live-range-splitting/IRA/LRA identifierad. Wasm-lokaler är VM-lokaler, inte ett krav på samma fysiska allocator. |
| Maskinkombinering/adresser: peephole, compare elim, REE | **Delvis.** A64 aritmetik/ext-fakta, x64 `lower.c` och sista-post-u32-faktum i `extensions.c`; wasm stack-/uttryckssänkning. Delade adress-/konstantrecept kan hjälpa, men SIB/NEON/SSE/flags är målmekanik. Generell compare-/combine-paritet är overifierad. |
| Layout/schemaläggning: align/reorder/sched, CGProfile | **Overifierad policy; generell scheduler saknas i granskningen.** `tu.c` och målens printers har ordning/alignment; detta bevisar inte profilbaserad placering eller latensschemaläggning. Börja med sannolika grenar och mät code-size/fetch. Wasm behöver egen strukturerad policy. |
| ABI/ram/debug/säkerhet: omit-frame, shrink-wrap, unwind | **Delvis policy.** X64 `frame.c` bevisar ramfri leaf; målens prolog/CFI har egna kontrakt. Inte generell shrink wrapping. Stack-clash, assignment tracking, instrumentering, PLT/interposition är separata användar-/korrekthetskrav; GCC-default i inventariet bevisar inte vcc-flaggstöd. |
| FP-semantik: signed-zero, errno, trapping | **Policy, exakt flaggparitet overifierad.** `cc/const.c`, `lib/f128.h`, native `fp.c` och wasm FP-sänkning finns. Dessa flaggor är främst begränsningar för tillåtna omskrivningar, inte bevis på fast-math eller tillstånd att återassociera FP. |
| Alias/alignment/assumptions: PTA, strict-alias, InferAlignment | **Delvis exakta objekt.** Förpass/escape/cellöverlapp finns på målen, inte generell TBAA/points-to/global-AA. Utöka objekt-/disjunkthetsfakta före minnesmotion; språkpolicy och char/union/overlap måste testas separat. Analysinvalidering är infrastruktur. |
| Pipeline/metadata: annotations, intrinsic lowering, remarks | **Ingen ett-till-ett-lucka.** LLVM:s passmanager, metadata och representationssänkning kräver inte samma vcc-pass. AllocToken/MemProf och intrinsicer måste kopplas till verklig källsemantik före eventuell implementation. |
| Andra språk: Coro*, OpenMP* | **Utanför den här C-probens transformbevis.** Att passnamnen körts säger inget om coroutine-/OpenMP-optimering i provet. Eventuellt språkstöd planeras separat. |
| Atomics/unreachable-policy | **Delvis sänkning; flaggparitet overifierad.** `cc/builtin.c` och måladaptrar har atomic-vägar. Inline/libcall-val, ordering, featurefallback och unreachable-trap-policy behöver egna konkreta kontrakt; de är relevanta C-krav. |

**Nästa implementationer, i kostnadsordning:**

1. **Billiga gemensamma recept utan ny IR/SSA.** Slutför källnära
   OPT6/OPT7: oanvända *rena* lokala initierare, små multiplikationer/
   bitregler, typade skalade adresser och konstant-store-hook. Använd
   befintliga förpassfakta och O(1)-beslut, inte upprepad AST-sökning.
   Lägg till bounded switch-värdetabeller/bitset-kluster där målet visar
   vinst. Explicit packed vektorsänkning använder gemensam semantik men
   målets instruktioner/features; den kräver inte en autovektoriserare.
2. **Begränsade lokala värdefakta.** En faststor cache per block för rena
   uttryck/konstanter, med registerversioner, exakta clobbers och tydlig
   invalidation. Återanvänd cellkärnan för små icke-flydda aggregatfält.
   Bevara trapping/volatile/atomic och implicit flags/asm. Kvot/rest-delning
   och rematerialisering prövas på konkreta tryckfall, inte som nya
   helfunktionspass utan mätt nytta.
3. **Anrop med målkontrakt.** Gemensam bounded inline-behörighet och
   kostnad, därefter wasms egna parameter-/resultat-/scope-mekanismer.
   Inför begränsade sibling calls med ABI-, ramflykt-, cleanup- och
   unwindbevis. Delad policy ensam levererar ingen wasm-inliner/tail call.
4. **Gemensam CFG innan globala omskrivningar.** Extrahera återanvändbara
   block/edges, komplett label-entry och konservativ liveness utan att
   försämra a64:s befintliga analys. Sätt arbetsbudget för dominans och
   dataflow; budgetslut ger fallback, aldrig ofärdiga säkerhetsbevis.
   Därefter join-medveten konstant/intervallpropagering, branch threading,
   dead-result/DSE över block och de första säkra loopinvarianterna.
5. **Avancerad O2 i separata familjesteg.** Global GVN/PRE, aggregat-
   promotion, loop-/induktionsomskrivning, autovektorisering och IPA får
   varsin korrekthets-/kodformsgrind och mätt CPU-/minnesbudget. SSA är
   motiverat att utvärdera för många join-värden, sparsam global dataflow,
   GVN och loopomskrivning; varken CFG, lokal CSE, AST-inlining eller ett
   enkelt säkert LICM-fall kräver automatiskt SSA. Auto-SIMD behöver även
   minnesberoenden och målkostnad, inte bara en ny representation.

Varje familjesteg kräver ett negativt basfall som visar luckan, ett
positivt per-mål-kodkrav och C-resultat/effektkontroller på a64/x64/wasm32/
wasm64. Mät hela kompilatorns extra arbete, undvik pass när kandidater
saknas och redovisa vad som inte täcks. Separata O1/O2-policyer införs först
när mätningar motiverar dem; ingen nivå döps om för att hävda paritet.
G5/G7 och full O2-täckning förblir öppna. Detta dokumentsteg kör inga byggen
eller nya benchmarks; annexets mängd-/unikhets-/nivå-/filhashkontroller
är datakontroller av befintligt underlag.


### OPT1: små konstantmultiplikationer med målkostnad (2026-11-11)

Implementerat första avgränsade steget: `opt/mulconst.c` normaliserar konstanten
till 32/64 bitar och väljer i konstant tid skift 1/2/3 för faktorerna 3/5/9.
Separata `arch/{a64,x64}/emit/mulconst.c` använder receptet vid O1+ och utan
`VCC_NOOPT`: arm64 väljer befintlig ADD med skiftad operand, x86 en ny ren
skalad LEA-post med båda registeranvändningarna och utan flagg-/minneseffekter.
32-bitarsresultatet nollutvidgas innan en eventuell senare teckenutvidgning.
Ingen extra temp, AST-genomgång eller operandutvärdering behövs. Wasm behåller
mul, som Clang 23; ett påtvingat skift/add-recept skulle lägga till operationer.

Lokal audit mot 69c1bd7 (aritmetikfiler identiska med main 2dc4ea3) och
Clang 23.1.6/GCC 15.2.0 visar att detta saknas på båda native-målen.
Arm64 har redan multiplikation med tvåpotenser; noll/ett finns i gemensam regel.
Faktorer 6/7/10/15 skjuts upp: referenserna använder två operationer där x86
har en omedelbar imul och arm64 två mov/madd; bredare målkostnad krävs.
Manifest med käll-/binärhashar, samtliga 36 auditkommandon och assembler ligger
lokalt i `work/opt-profile/multiply-audit/`. Basbinären har SHA256
`61249ca189d71743da9c506100c3d3dc8451cb3338e2898bc904b03d7cda87f4`.
Detta är kodformsbelägg, inte uppmätt körtidsvinst.

Verifierat före kostnadsmätning: unity och fil-för-fil med j2, 16 fullständigt
byteidentiska assemblerpar mellan byggena, 16 mål/nivå-kombinationer O0–O3
med bokstavliga körsvar och avsedda kodformer. Fallen omfattar signed/unsigned
32/64, negativa definierade produkter, wraparound, smalning med smutsiga höga
bitar, efterföljande utvidgning, *=, kommuterad konstant, en enda pekar-/värdefunktion
samt global volatile-laddning. 36 bytejämförelser mot basen bevarar O0,
`VCC_NOOPT` och overflow-builtins. Det redan ignorerade attributet optnone
utgör inget nytt löfte. ASan/UBSan för receptets modulära oracle och mutation,
samt x86-posternas användningar/definitioner/flaggor/nollutvidgning passerar.
CI kör den nya receptkontrollen och befintlig opt-rules tar automatiskt det nya
fallet. Slutlig integrations-CI återstår; avgränsad kostnadsmätning finns nedan.
Ingen SSA, generell InstCombine eller full O2-täckning är implementerad.


OPT1 matchad kostnad för 3/5/9: exakt ren kandidat `f92b67b` mot historisk
bas `69c1bd7`, båda värdbyggen `-O3 -march=x86-64-v3`, hela processens
Callgrind-Ir och 500 funktioner per prov, O1/O2 för alla fyra måltrippelar.
Skalär kontroll med faktor 37 (receptet avböjs) kostar +0,1256 % x86,
+0,1231 % arm64 och +0,0141–0,0142 % wasm. Pekarslingan kostar +0,0863 %,
+0,0041 % respektive +0,0080 %. Alla kontrollutdata är byteidentiska.
Positivt prov med faktorerna 3/5/9 kostar +0,3529 % x86, −2,1465 % arm64
samt +0,0160–0,0161 % wasm. Där blir 500 imul till 500 skalade LEA på
x86 och 500 materialiserade konstant+madd till 500 skiftade ADD på arm64;
wasm behåller 500 mul och dess assembler är byteidentisk. Detta mäter
kompileringsarbete, inte programmets körtid eller hela prestandagrinden.

Kandidatbinärens SHA256 är
`8492bdd0cb53f89d1ad3173ac48bcb837f668a20a4860b0b3fa64ee232ccf362`.
`work/opt-profile/mulconst-cost/result.json` bevarar samtliga 24 matchade par,
exakta kommandon, kompileringsflaggor, käll-/binärhashar och kodformsräkningar.
`work/opt-profile/mulconst-unity-parity/result.json` bevarar de 16
byteidentiska bygglägesparen. Bas 69 är avsiktligt historisk; en senare
integration får inte beskriva dessa tal som mätning mot dess nya förälder.
Slutlig integrations-CI och full G7 är fortsatt öppna.

OPT8-integrering: implementationen är 58263be; därefter har main54580f7
(PR48:s genererade minnesregression) mergats som 6b0e195. Konflikten gällde
bara denna plan; båda statusblocken är bevarade. Kompilatorkällorna är
oförändrade av denna merge. Unity, 180/180 riktade kör-/kodkrav och de
sanitiserade reciprok-/tryckkontrakten passerar på kombinationen, liksom
221/221 arm64-regressioner (ett befintligt xfail) och 216/216 x86-regressioner
(sex referensskips). Mätningarnas JSON anger exakt 781ca69→58263be och
`compiler-sources.json` binder de tio ändrade kompilatorfilerna med SHA256.

### Status OPT8 (2026-11-11): första begränsade native-tail-calls, integration pågår

`emit/opt/tail.c` bevisar samma enkla wrapperform för båda registermålen:
hela kroppen är en direkt return-call, identiska 32/64-bitars heltals-/pekartyper,
argument är egna formaler eller typade literaler, fasta prototyper och högst
målets registerkapacitet. Ingen ny hel AST-vandring eller SSA. Målens egna
`tail.c` parkerar argument före ABI-placering; en terminal markör håller alla
utgående argument levande till epilogen. X86 hoppar efter återställd ram/CFA;
arm64 skriver symbolgrenen som JUMP26 även direkt till objekt och återställer
CFI före grenen. Stackargument, lokaler/adressflykt, cleanup/VLA, konverteringar,
indirekta anrop, flyttal och allmänna svanspositioner återstår.

`-f[no-]optimize-sibling-calls` tolkas i ordning och finns i cacheidentiteten.
Attributen returns_twice, disable_tail_calls, not_tail_called och optnone
lagras/ärvs; den nya regeln respekterar dem. Övriga pass har ännu inte full
optnone-policy. Standard-wasm behåller vanligt call: clang 23 O2 gör samma sak;
return_call kräver explicit tail-call-feature och assemblerstöd, som inte ingår
här. Detta är ingen implementerad wasm-tail-transform.

Checkpoint på PR50-bas 10e2cb3: unity bygger; 16 fokuserade O0–O3-mål/nivåer
klarar kodkrav och körning, native även CFI. Verkligt kärnunderlag finns:
preprocessor-baslinje 881/881 och kandidatens sema 887/887 både per process och
batch. Detta är historisk evidens för den basen, inte bevis på senaste main.
Uppdaterad integration `2506518` inkluderar PR50 `0a3c988` och main `f97d16d`.
På den källan är sema grön 887/887 både per process och batch; verklig
kärnparitet är 881/881. Egna x86-prov klarar 223/223 med sex uttryckliga
referensundantag; egna arm64-prov klarar 228/228 samt ett befintligt xfail.
ABI är 32/32 på vardera x86 och arm64. Alla fyra måls
O0–O3-prov körs korrekt; native provar också CFI. Arm64:s direkta C-till-objekt
har rätt JUMP26/CALL26-relokeringar och körs korrekt under qemu. 48 blandade
flagg-/attributjobb med deklarationscache av/på är gröna. Ramregression,
verklig signal-unwind och registerhems-unwind (vanlig och 64-alignerad ram)
är gröna. Ändrade C-filer klarar separat syntaxkompilering; effektkontrakten
är gröna. De snabba cache- och direktobjektproven ingår nu i CI.

Två kalla 501-funktionsprofiler med exakt bas `0a3c988` och kandidat
`2506518`, båda byggda med `VCC_OPT='-O3 -march=x86-64-v3'`, visar:
enkla heltalswrappers med en formal och en literal går från 17 934 254 till
15 767 383 kompilerarinstruktioner (−12,082 %) och 32 064 till 24 038
`.text`-byte. Icke kvalificerade aritmetiska returer går från 17 211 605 till
17 269 849 (+0,338 %); assemblern och 24 039 textbyte är identiska.
`work/opt-profile/tail-cost/report.json` innehåller kommandon, alla spårade
kompilatorkällors hash, indatakällor och binärhashar; `measure-tail.py`
återskapar profilerna. Basbinärens SHA256 är
`d8e6b3b2bc693eb75374d41f4f7f3f871fba1aad78b1eb1a04bf5d65ba99d8cc`,
kandidatens `dcb4323027bc076c74904eb179d3a9575cb06224a114cf2f31e005fcbc7ad0c2`.
Detta är två avgränsade Ir-profiler, inte generell kompilerarfart eller
programkörtid; maskinen var delad och inga väggtidsanspråk görs. G5/G7 och
hela O2-målet är fortsatt öppna. Allmänna svanspositioner, stack-/FP-/aggregat-
ABI och uttryckliga wasm-tail-features återstår.

Tail-integrering efter main `103cdee` (#51/#60): båda planblocken och
harnessens tails/udivconst-regler är bevarade. Unity, fyra måls O0–O3-tailprov,
48 blandade cachekontroller, native-konstantkontroller, båda natives unsigned-
regler och reciprokontrakt passerar. Hela CI var grön på `8532f90` före denna
merge. Kärn-/svitbevis och Ir-profiler ovan gäller sina angivna källversioner;
main-integreringen är inte en ny kostnadsmätning.

### OPT8 status: unsigned64 reciprokdivision (2026-11-11)

Delsteget från exakt 046c6dc utökar `emit/opt/udivconst.c` med separat u64-
konstruktion i host-u128, kontrollerad mot libdivides u64-generator ovan.
U32 behåller sin egen billigare konstruktion. Native använder befintlig
UMULH/MULW via en separat walker-hook, med sparning av levande implicita
RAX/RDX. Budgeten räknar lat input, magic och sparat original/korrektion;
registertryck behåller vanlig division. Ingen SSA eller ny AST-genomgång.
Wasm avvisar vanliga u64-reciproker före konstruktionen och väljer bara
jämförelsekvot vid divisor>2^63, som clang23:s faktiska wasm-sänkning.
U64-rest och låga divisorer behåller wasm `div_u`/`rem_u`.

Sanitiserade kontrakt prövar 4096 fullbreddsdivisorer, 32 slumpdivisorer per
exakt bredd 2–63, tvåpotensgrannar, extremvärden och multipler mot oberoende
division/modulo; de gamla u32-kontrakten kvarstår. Den faktiska x86-hooken
prövas i 1536 register-/alias-/livemaskfall inklusive RAX/RDX. Körprovet har
18 divisorklasser, fullbreddsinput, multipler ±1, /= och %=, volatile,
sidoeffekter en gång, fasta RAX/RDX och nästlat adress-/registertryck.
Unity och fil-för-fil bygger. 196/196 regel-/nivå-/målkombinationer passerar
på x86, arm64, wasm32 och wasm64, även de 16 nya u64-fallen med fil-för-fil.
222 arm64-regressioner (ett befintligt xfail), 217 x86-regressioner (sex
referensskips), ABI 32/32 per native-mål, libgcc-unwind och x86-debug passerar.
Den oberoende läsgranskningen fann ingen kvarvarande blockerare.

Parade Callgrind-mätningar använder exakt 046c6dc mot e1fdb3d:s kompilatorkod,
clang23 `-O3 -march=x86-64-v3`. På fyra mål är 500-funktionskontroller och
loopkontroller högst +0,00021 % extra arbete. Befintlig mättad u32-korpus
kostar +0,052…+0,122 %, med identisk assembler. Inga av dessa eller wasm:s
avvisade u64-fall exekverar hostens `__udivti3`. En konfigurerad oförändrad
12-filers native-mix kostar −0,00303 % på x86 och +0,00082 % på arm64;
10 respektive 11 basstödda wasm-filer kostar −0,00794 %/−0,00462 %.
Wasm-mixen utesluter befintligt ej stödda atomics/BitInt-fall enligt JSON.
Detta är avgränsad kontroll-/mix-evidens, inte hela G7-grinden.

Den mättade nya u64-korpusen (`x/7ULL+x%10ULL`, 500 funktioner) kostar
**+14,10 % på x86 och +17,99 % på arm64 att kompilera**. Den avvägningen
är uttrycklig: reciproksekvenser och host-u128-konstruktion gör mer arbete.
På x86 kör en separat loop med tre miljoner seedade fullbredds-LCG-input
samma kvot-/restsumma: två uppvärmningar och nio alternerande barnprocesser
per binär ger median CPU 10,920→3,325 ms (−69,55 %), med identiskt
clang-verifierat svar. Detta gäller denna korpus/värd och inkluderar
processstart; det är inget generellt hastighetslöfte. Arm64 har statisk
UMULH-/restkod verifierad men ingen mätning på verklig arm64-hårdvara.
Wasm behåller sin dyrare klass enligt kostnadspolicyn.

Källor, exakta kommandon, binär-/källhashar och råa prov finns under
`work/opt-profile/udivconst64/`: `compiler-profile.json`, `mixed/result.json`
och `native-runtime-profile.json` med respektive `*-measure.py`.
Signed reciproker, billigare wasm-u64/rest/korrektion, bredare registerallokering,
fulla G5/G7-grindar och full O2-täckning återstår. U32-statusens äldre
+19,54 % mättade arm64-kostnad gäller fortfarande dess separata basmätning.

U64 efter integration med PR51 `54c8f3f` och main `f97d16d`: kompilatorkod
`d8957b3` bygger unity/fil-för-fil och passerar 196 regelprov, 16 separata
kontrollprov, 229 arm64-PASS (ett befintligt xfail), 224 x86-PASS (sex synliga
referensundantag), ABI 32 per native-mål, unwind/debug och sanitiserade
reciprok-/registerparskontrakt. Fil-för-fil kör de 16 u64-fallen på fyra mål;
O0/noopt-assemblern är byte-identisk mot den nya föräldern i åtta jämförelser.
Båda sidors planstatus och main:s x86-ändringar behålls.

Nya parade mätningar av exakt `54c8f3f`→`d8957b3`, samma clang23/v3-flaggor,
finns separat i `work/opt-profile/udivconst64-integrated/` med källhashmanifest.
Kontroll/loop är högst +0,000105 %; u32 +0,052…+0,121 % med identisk assembler.
Mättad u64-korpus kostar **+13,55 % x86/+17,97 % arm64** att kompilera.
Samma fixerade mix kostar +0,00429 % x86 (12 filer), +0,00065 % arm64 (12),
−0,00458 % wasm32 (10) och +0,01954 % wasm64 (11); samma basstödsundantag.
Det seedade x86-provet ger nu median 11,047→3,396 ms (−69,26 %) och identiska
körbinärhashar/svar som den historiska jämförelsen. De tidigare
+14,10 %/+17,99 % gäller fortsatt uttryckligen 046c6dc→e1fdb3d. Ingen av
mätningarna ersätter full G7 eller ger ett generellt körprestandalöfte.

U64 efter main2dc4ea3 via uppdaterad PR51head15551a7: unitybygge, samtliga
196 riktade kör-/kodkrav, sanitiserade matematik-/registerparskontrakt och
x86-bladramens policy/cache/debug/härdning/signal-unwind passerar igen.
Reciprok- och högproduktkällorna är oförändrade av integrationen.
Profilernas exakta 54c8f3f→d8957b3-avgränsning behålls; inga nya kostnads-
eller generella fartpåståenden görs för denna integration av bladramar.

### OPT8 status: signed32/64 reciprokdivision (2026-11-11)

Delsteget ovanpå exakt `f585591` använder `emit/opt/sdivconst.c` för en
gemensam reciprok för signed32 och signed64. Separata native-adaptrar
använder signed högprodukt, eventuell addition av täljaren, aritmetiskt
skift och en teckenbitskorrektion för C:s avrundning mot noll. Negativ
divisor negerar bara kvoten; resten beräknas med positiv divisormagnitud.
Noll, ±1 och tvåpotenser behåller befintliga regler. _BitInt, FP, pekare,
vektorer och andra bredder avvisas. Wasm behåller sin `div_s`/`rem_s`,
liksom clang23 för samma små prov.

Receptet byggs bara för konstant högeroperand vid O1+ utanför `noopt`.
Signed32 använder en host64-division; bara signed64 använder host-u128.
Tryckgränsen är högst två extra temporärer plus eventuell materialisering
av input; otillräckligt utrymme behåller vanlig division. x86:s separata
signed högprodukt återanvänder unsigned-variantens ägarskap och skydd för
RAX/RDX. Ingen SSA, ny AST-genomgång eller dynamisk allokering tillkommer.

Likhetsfallet i reciprokens felgräns tillåts utan extra täljaraddition.
Ett fullbreddsbevis i hjälparens kommentar omfattar även INT_MIN. Därmed
får `/3` samma korta magiska multiplikator som clang (`0x55555556` respektive
`0x5555555555555556`), utan att kräva samma register-/ramkod. Regressioner
täcker ±3/6/7/10/12/37, stora divisorer, extrema täljare, grannar till
multipler, signed64 ±9/129, lazy narrowing, compound-lvärden och effekter.

Aktuell integration bygger unity och de åtta ändrade/nya implementationerna
kompilerar var för sig. Fyra mål × O0–O3 passerar körning och kodkrav;
ytterligare fyra O2-`VCC_NOOPT`-kontroller passerar. Sanitiserade matematik-,
tryck- och signed/unsigned registerparskontrakt passerar. Native-sviterna ger
230 arm64-PASS (ett befintligt xfail) och 225 x86-PASS (sex uttryckliga
referensundantag). ABI ger 32 PASS per native-mål. x86:s debugrader O0–O3
och verklig unwind genom vanliga/överjusterade ramar vid O1/O2 passerar.
O0/noopt-assemblern är dessutom byte-identisk med `f585591` i åtta
mål-/policyjämförelser.

Parade Callgrind-mätningar använder nya binärer från exakt
`f585591`→`fbc7105`, båda med clang23 och `-O3 -march=x86-64-v3`.
500-funktionskontrollen har identisk assembler och kostar +0,0261 % x86,
+0,0270 % arm64. Loopkontrollen kostar +0,00795 % respektive +0,00091 %.
Wasm-kontrollerna och signed-korpusen har identisk assembler och ändras
högst 0,000214 % i instruktioner. Avsiktligt mättade native-korpusar med
500 funktioner (`x/7 + x%10`) visar kostnaden för extra kompilatorarbete:

| Mål | Signed32, kompilator-Ir | Signed64, kompilator-Ir |
| --- | ---: | ---: |
| x86-64 | +14,21 % | +17,79 % |
| arm64 | +30,03 % | +20,44 % |

Samma fixerade 12-filsmix som u64-steget kostar +0,00256 % x86 (12 filer),
+0,00572 % arm64 (12), +0,000141 % wasm32 (10) och −0,000034 % wasm64
(11). Basstödsundantagen är synliga: wasm32 atomic_ops/bitint_runtime,
wasm64 bitint_runtime; inga nya målundantag har lagts till.

Ett separat seedat x86-körprov summerar signed64 `/-37`, `%129` och
signed32 `/3`, `%-7` för tre miljoner fullbreddstäljare. Samtliga svar
matchar clang. Nio alternerade barn-CPU-prov efter två uppvärmningar ger
median **18,217→6,633 ms (−63,59 %)**, inklusive processstart. Det är ett
avgränsat prov, inte ett generellt fartlöfte eller mätning på arm64-hårdvara.
Käll-/binärhashar, exakta kommandon och rådata finns i
`work/opt-profile/sdivconst/`: `compiler-sources.json`,
`compiler-profile.json`, `mixed/result.json`, `native-runtime-profile.json`
och `noopt-identity.json` med respektive mätskript. Full G7 och full
O2-täckning återstår.

Bounded-flow efter stackintegration med signed/tail/u64 (`661d552`): unity,
sanitiserade flow-/dead-result-kontrakt, 16 mål-/nivåprov med körning och
x86-debug O0–O3 passerar. Tidigare hela sviter och Ir-mätningar ovan gäller
sina exakta angivna versioner; integrationen är ingen ny kostnadsmätning.


### OPT6: oanvända skalära lokala initierare (avgränsat delsteg)

Implementerat: `emit/opt/unusedinit.c` delar behörighet och
recept mellan a64, x64 och wasm32/64. Befintlig `SA_USED` från prepasset avvisar
refererade objekt direkt; ingen ny funktionsgenomgång, CFG eller SSA tillkommer.
Endast aldrig refererade automatiska heltalsobjekt på högst 64 bitar omfattas.
Volatile/atomic, cleanup, VLA/variabelt modifierade typer, statisk lagring,
adressflykt, `used`, asm-register och `_BitInt` behåller befintlig väg.
O0, `VCC_NOOPT` och uttryckligt känd `optnone`-funktion avvisas också.

En kandidatbunden heltalslista får besöka högst 64 AST-noder. Den utesluter
minnesindirektion, division/rest, skift, FP, anrop, builtins och okända noder.
Ett bevisat rent initialiseringsuttryck utelämnas. I annat fall evalueras
heltalsuttrycket som ett värde och frigörs/droppas; endast destinationens lagring
tas bort. Detta bevarar anrop, läsningar och interna konverteringar som den
befintliga värdeemittern utför. Budgetslut behåller evalueringen. Brace-/zero-
initialiseringsposter går kvar genom initialiseringsgången; de skickas aldrig
till värdeemittern. `side_effects()` används inte som bevis för frånvaro av traps.
Befintliga AST-/symboldata ändras inte; registerlokalvalet undviker att reservera
ett hem för dessa oanvända objekt.

Verifierat hittills: unity- och nonunitybygge samt fristående receptkontrakt normalt och med
ASan/UBSan. Kontrakten täcker budget 0/1/exakt gräns, 64/65 noder, behörighet,
attribut, okända noder, FP-/minnes-/divisionskontroller och initializer-fallback.
Små O2-assemblerprober visar att x64:s döda multiplikation/addition/store och
wasm32/64:s multiplikation/addition/lokal försvinner; a64:s befintliga efterpass
hade redan tagit bort aritmetiken i just denna prob. Detta är inte generell
DCE eller en fullständig lösning på OPT6.

Matchad kort Callgrind-mätning: exakt `f7327bd` mot `beb6e2b`, båda byggda
med Clang 23 `-O3 -march=x86-64-v3`, 16 seriella par utan exkluderingar, endast
O2. Kandidatfria 200-funktionskontroller/loopar gav x64 +0,0551/+0,2239 %,
a64 +0,0630/+0,1846 %, wasm32 −0,0001/+0,1325 % och wasm64
−0,0001/+0,1324 % exekverade kompilatorinstruktioner; all assembler var
byte-identisk. 200 positiva funktioner gav x64 −6,3725 %, a64 −5,2347 %,
wasm32 −4,1897 % och wasm64 −4,1878 %. a64 gav samma assembler men slapp
arbete som dess efterpass tidigare utförde. Fyra befintliga x64-regressionsfiler
(`deep_mixed`, `register_locals`, `atomic_ops`, `integer_identities`) gav
55 286 799 → 55 297 899 instruktioner (+0,02008 %), alla med samma assembler.
Manifest, kommandon, käll-/binärhashar och reproduktionsskript finns i
`work/opt-profile/unused-init-cost/{result.json,measure.py}`. Detta mäter hela
kompilatorprocessens användarinstruktioner, inte hårdvarucykler, väggtid eller
programmens körtid. Det är ett avgränsat exact-parent-prov; den kombinerade
optimeringsstackens separata mätning följer nedan.

Den kombinerade kostnadsgaten (`bench/vcc-opt-cost.py`) passerar: högst 1 %
ökning per mål, både mot `4a839ba` före stacken och mot `7be5f64` före detta
delsteg. Kandidaten var `59c686c`; alla tre binärer byggdes med Clang 23
`-O3 -march=x86-64-v3`. Serien mäter Callgrind-Ir för hela `vpp -O2 -S` på
ett fast korpus av befintliga regressionsfiler, med samma kandidatprofil i båda
jämförelserna:

| Mål | Filer | Mot `4a839ba` | Mot `7be5f64` |
|---|---:|---:|---:|
| x86-64 | 12 | +0,194974 % | +0,066731 % |
| arm64 | 12 | +0,113848 % | +0,062119 % |
| wasm32 | 10 | +0,248758 % | +0,057039 % |
| wasm64 | 11 | +0,233999 % | +0,057260 % |

Wasm32 saknar `atomic_ops` och `bitint_runtime`, wasm64 saknar
`bitint_runtime`; samma fall avvisas av alla tre kompilatorerna. Rapporten
innehåller kommandon, hashvärden, stödutfall och instruktionstal i
`work/opt-profile/stack-cost/final/result.json`; körloggen finns i
`work/opt-profile/stack-cost/final-run.log`. Detta är en begränsad gate för
kompilatorarbete, inte ett mått på körhastighet hos genererade program.

Efter integrering av postfix-föräldern (`e92a939`) passerar unity och nonunity
alla 16 target×O0–O3 runtime-/kodgenfall vardera, plus fyra `VCC_NOOPT`-fall
vardera. Kontrollerna omfattar rena uttryck, anrop en gång, short circuit,
volatile/atomic destination/läsning, division/minnesläsning/FP-konvertering,
cleanup/VLA/brace-init, `used`/`optnone`/asm-register och återanvänd inlinad AST.
Det exakta `f7327bd`-baslinjeverktyget underkänns av det nya x64-positivprovet,
så kodgenkravet skiljer faktisk ny förmåga från ett redan godkänt utfall.
ABI-korsanrop passerar 32/32 på x64 och 32/32 på a64. Debugprovet har en egen
oanvänd initierare före en överlevande radmarkör och passerar O0–O3, inklusive
DWARF-verifiering och byte-identisk `.text` med/utan `-g`.

Native-regressionskontrollen på samma produktionskälla gav 230 godkända x86-
fall och 235 arm64-fall, plus ett befintligt arm64-XFAIL. Den nya fixturen
avvisades först av clang: GNU:s `__atomic_fetch_add` tog en `_Atomic`-pekare.
Fixturen rättades till typade `__c11_atomic_fetch_add`, som clang godtar;
det berörda fallet kördes därefter separat och passerar på båda native-målen.
Sammanlagt verifieras därmed 231 x86-fall (sex referensundantag) och 236
arm64-fall samt ett befintligt XFAIL. Ingen produktionskod ändrades vid
rättningen. Alla fyra måls O0–O3-kör-/kodkrav och O2/noopt-prov kördes om
och passerar med den rättade fixturen. Loggar: `work/opt-pr-tmp/unused-stack-
{x86,a64}.log`; de två nya separata 1/1-resultaten är berörda-fixtur-körningar,
inte nya hela svitkörningar. Den kombinerade stackens kostnadsgrind återstår.

### Gemensam walker: en värdeläsning för skalär postfix (2026-11-11)

Status: implementerat och lokalt verifierat i `opt/postfix-eval`, före
integreringscommit. `wk_incdec` i `emit/walk/expr.c` läser ett ordinärt
registerrepresenterat värde en gång, behåller det gamla värdet och gör en
registerkopia i rätt registerklass före den destruktiva aritmetiken. Detta är
en evalueringsrättning vid O0–O3, utan optimeringspass, SSA eller kostsam analys.
Den gemensamma walkern används fortfarande bara av x86; arm64 och wasm behåller
sina befintliga vägar och valideras av samma semantiska test.

Avgränsning: `_Atomic` går oförändrat genom sin separata RMW/CAS-väg. Typer
som `wk_mem` håller via adress, exempelvis x86 `__int128` och `long double`,
behåller också sin befintliga snapshot-/läsningsväg; detta delsteg lovar inte en
enda originalläsning för dem. Postfix-bitfält använder den nya registerkopian:
två behållarläsningar återstår, en för fältvärdet och en för att bevara grannbitar,
som i de undersökta clang/GCC-fallen. Prefix-bitfältens extra resultatläsning och
fältbreddsnormalisering ändras inte här.

Validering: `postfix_eval.c` och `vcc-opt-rules.py --rule postfix --run`
kontrollerar exakt en ordinär skalär läsning och skrivning (eller en kombinerad
RMW), gamla/nya värden, bortkastat och castat resultat, smala heltal, bool,
FP/GPR-registertryck, fasta adressregister, pekarsteg, sidoeffekt i adressen,
bitfältsgrannar och separata atomiska/adressbaserade kontroller. Unity och
filvis bygge klarar samtliga 32 kombinationer av fyra mål och O0–O3. Den tidigare
oförändrade parent69-postfixvägen avvisas av läsningskontrollen: två läsningar i
stället för en. x86:s egna C-svit klarar 228/228, med sex befintliga överhoppade.
Loggar: `build/linux/postfix-evidence/`. Ingen ytterligare bred testkörning behövs
för detta delsteg. En liten matchad Ir-jämförelse efter checkpoint 6a726c0 finns i
`build/linux/postfix-evidence/cost/result.json`: samma clang23,
`-O3 -march=x86-64-v3`, exakt f7327bd-baslinje och en seriell Callgrind-körning
per binär/källa, inklusive processstart. För 64 funktioner utan postfix ändras
6 703 455 till 6 703 542 Ir (+87; +0,0013 %), med identisk assemblertext.
För 64 postfix-funktioner ändras 6 717 886 till 6 731 413 Ir (+13 527;
+0,2014 %), samtidigt som varje funktions ordinära objektläsningar minskar
från två till en. Detta mäter kompilatorarbete i två små prov, inte en full
1 %-grind eller körtid/hastighetsvinst hos det genererade programmet.


OPT1 integration efter wasm-flödesföräldern: på `60027c7` med förälder
`6d60e73` passerar unity och fil-för-fil (j2), 16 kompletta byteidentiska
assemblerpar mellan bygglägena, samtliga 16 multiplikationsfall O0–O3 på
x86/arm64/wasm32/wasm64, sanitiserat recept med mutation och posteffektskontrakt.
36 O0/VCC_NOOPT/overflow-jämförelser mot förälderns binär är byteidentiska
(byggd 1458a90, produktionskällor verifierat identiska med 6d60e73).
Native-körharnessen kontrollerar nu även bevarade register för detta fall.
CI-/harness-unionen behåller tails, deadresults, flödeskontrakt, wasm-flow och
mulconst. Aktuell unity-binär har SHA256
`398f139ca1489cd83a8822c0db8958f54df72cc73cc7fb252e5add4781a30a2e`;
kommandon/binärhashar och 16 par finns i
`work/opt-profile/mulconst-integration-parity/result.json`, kör-/kontraktsloggar
i `work/mulconst-integration-{contracts,policy,runtime}.log`.
Ingen historisk kostnadsprofil har upprepats: talen ovan gäller fortfarande
exakt 69c1bd7 till f92b67b, inte denna integrationsförälder. Integrations-CI
återstår; G5/G7 och hela O2 är fortsatt öppna.

Postfix-integrering efter multiplikationsföräldern: e63bf47 staplar rättningen
på PR71/f295999. Aktuellt unity-bygge är grönt. Samtliga 16 postfix- och 16
konstantmultiplikationsfall (fyra mål, O0–O3) klarar kodgenereringskontroller och
körning; multiplikationsrecept med mutationskontroll och registereffekter klarar
ASan/UBSan. `build/linux/postfix-evidence/integration-*.log` innehåller resultaten.
Den tidigare breda sviten och matchade Ir-proven har inte körts om; inga
integreringsfel motiverade det. Rättningen är fortfarande avgränsad till
registerrepresenterad postfix och ligger kvar ovanpå multiplikationsföräldern.

### Emitter: namn- och registerjämförelser (2026-11-11)

På bas `12957dc` används internade identifierares redan lagrade ord för korta
namn (`emit/name.h`), heltalstaggar från den befintliga builtintabellen för
särbehandlade bibliotekskall och direkt avkodning av x86:s asm-registernamn.
Den oanvända parallella registertabellsökningen har tagits bort. Antalet
källrader med `strcmp`/`strncmp`/`memcmp` i emitter och tre backender minskar
från 60 till 33; kvar finns bland annat faktiska C-strängjämförelser,
variabla asm-operandnamn, sektions-/synlighetsnamn och debugfiler. Dessa är
inte borttagna bara genom att flytta eller återskapa en jämförelseloop.

Matchad `perf stat`, fem växelvisa körningar på denna delade maskin med clang 23
`-O3 -march=x86-64-v3`, `12957dc` → `2727265`: 1 000 x86-funktioner med
inline-asm-klobber gav −7,586 % kompilatorinstruktioner, och 1 000 vanliga
anrop respektive `__builtin_memcpy` gav x86 −0,235 / −0,146 %, arm64
−0,202 / −0,103 %. Utdataassemblern var identisk. På wasm ökade samma
anropsprov +0,033–0,035 %; kontrollproven −0,027 till +0,012 % på fyra mål.
Dessa mättade prov mäter inte generell kompileringstid. De fem korta
cykelmätningarna varierar och bevisar ingen 1 %-tidsgräns. Kommandon,
binär-/källhashar och observationer: `work/opt-profile/emitname/final-result.json`.

Den fasta 1 %-instruktionsgrinden mot samma bas passerar: x86 −0,002 %, arm64
+0,020 %, wasm32 +0,042 %, wasm64 +0,039 %; 12/12 nativefiler, 10 wasm32 och
11 wasm64, med avvisade wasmfall uttryckligen redovisade. Underlag och exakt
byggproveniens: `work/opt-profile/emitname-final-gate/result.json`. Unity- och
filvis bygge passerar; före/efter-kärnans sema 887/887 i batch och individuellt;
248/248 arm64-vcc-regressioner, en befintlig xfail; 90 assemblerpar O0/O2,
187 x86-klobberstavningar, fyra måls O0–O3-kontrollflöde samt ASan/UBSan-
kontrakt. Vidare genomgång av de kvarvarande jämförelserna och realistisk
vägg-/CPU-tidsmätning återstår.

### Begränsade switch-värdetabeller (2026-11-11)

Status: lokalt implementerat för x86-64, arm64 och wasm32/64 vid O1–O3.
En gemensam budgeterad analys accepterar täta, högst 256 element stora
intervall med explicit default och konstanta heltalsresultat, antingen direkta
returer eller tilldelning till samma lokala variabel följd av break. Hål får
standardvärdet och väljaren evalueras en gång. Cleanup, inlining på native,
Darwin och osäkra kontrollflöden behåller ordinarie switchsänkning. Data läggs
efter funktionens storlek och debug-/CFI-avslut i en alignerad läsbar sektion.

`vcc-opt-rules.py --rule switchvalues --run` passerar O0–O3 på alla fyra mål
samt O2 med `VCC_NOOPT`; testet täcker gränsvärden, intervall, hål, sidoeffekter,
volatile, flera tabeller och avvisade kontrollflöden. Arm64-direktobjekt med
PIC, debug och CFI körs också under qemu och dess sektioner, symboler och
relokeringar överensstämmer med extern assemblering av vcc:s assembler.
Kostnadsproven nedan ger inte generell kompileringstid eller programhastighet.
### OPT1: två bitregler (2026-11-11)

Multiplikation med en tvåpotens sänks direkt till maskinskift för 32-/64-bitars
heltal; XOR med enbart ettor blir komplement. Recepten ligger i separata
målgeneriska filer, utan ny AST-vandring. O0 och VCC_NOOPT är oförändrade;
_BitInt och bredare typer ingår inte. O1–O3, bieffekter och operandkonvertering
kontrolleras på x86, arm64, wasm32 och wasm64. Unity och filvis bygge samt
36 O0/noopt/overflow-paritetsprov passerade.

Matchad seriell Callgrind-profil (`work/opt-profile/bit-rule-audit/result.json`,
kompilatorkällor och binärhashar i manifestet) mäter hela kompilatorprocessens
instruktioner, inte cykler eller programkörtid. Fyra blandade x86-prov gav
+0,0112 %; 200 regelfria funktioner +0,052–0,469 % beroende på mål; 200
regelmättade funktioner −1,39–1,79 %. Detta är små prov, inte en fullständig
prestandagrind. Jämförelseregler för heltalsgränser och bred O2-täckning återstår.

### OPT1: heltalsjämförelser vid gränsvärden (2026-11-11)

Ett gemensamt recept i `emit/opt/cmpbounds.c` känner igen relationer med
konstant resultat vid lägsta/högsta värdet för konverterade 32-/64-bitars
heltal. Operanden evalueras fortfarande: anrop och volatila/atomiska läsningar
bevaras. Endast numeriska AST-litteraler med identisk konverterad typ ingår;
pekare, flyttal, _BitInt, bredare typer och övriga konstantuttryck faller
igenom. Värde och gren testas på x86, arm64, wasm32 och wasm64 O0–O3;
assemblerkontroller verifierar även anrop och volatila/atomiska läsningar.
O0 och VCC_NOOPT är byteidentiska mot föräldern på testfilen för alla fyra mål.
Unity och filvis bygge passerar. Ytterligare gränsregler återstår.

Matchad seriell Callgrind-profil mot bitset-föräldern (`work/opt-profile/
cmpbounds-cost/result.json`, med käll- och binärhashar) mäter instruktioner för
hela kompilatorprocessen. Fyra blandade x86-filer gav +0,181 %; 200 funktioner
utan gränsjämförelse gav +0,252–0,295 % och mättade positiva prov −1,35–8,53 %
beroende på mål. Detta är små prov, inte en fullständig prestandagrind.

Värdetabellernas separata matchade Callgrind-prov mot basen före dessa senare
steg finns i `work/opt-profile/switch-values-cost/result.json`. Ett prov med
200 funktioner utan switch ändrade kompilatorarbetet +0,003–0,023 %; ett
avsiktligt mättat prov med 200 värdetabell-switchar ökade det +6,16–6,36 %
på native och +20,05–20,16 % på wasm. Detta är kompilatorinstruktioner, inte
cykler, och kräver en realistisk korpus före generell slutsats. Ett separat
syntetiskt x86-program med 100 000 anrop minskade instruktioner för hela
programmet från 4 544 608 till 4 217 359 (−7,2 %); det är inte ett generellt
körtidsmått. Den sammansatta stacken mäts separat nedan.

Hela stacken inklusive värdetabeller klarar den fasta 1 %-grinden mot
`4a839ba`: x86 +0,375 %, arm64 +0,292 %, wasm32 +0,556 %, wasm64 +0,515 %
kompilatorinstruktioner. Värdetabellsteget ensamt mot jämförelseföräldern
`dc3e54d` gav +0,032 %, +0,023 %, +0,045 % respektive +0,043 %. Det är
12/12 native-filer, 10 wasm32 och 11 wasm64; avvisade atomic_ops och/eller
bitint_runtime är redovisade, inte godkända. Exakt källträds- och binärproveniens,
kommandon och felkontroll finns i `work/opt-profile/full-stack-cost/result.json`.
Grinden är en liten fast korpus och upphäver inte det högre mättade switchprovet.

CI-uppföljning: wasm-tabellernas assemblertext får nu en explicit `.size`,
som LLVM:s assembler kräver. Det fokuserade tabellprovet assembleras även med
clang för wasm32/64; ett separat switchprov med variabla resultat bevarar
kontrollen av wasm `br_table`. Båda proven körs O0–O3 på fyra mål. Den
föregående kostnadsprofilen gäller före denna lilla metadatatilläggning.

### Switch-värdetabeller: snabbare utmatning (2026-11-11)

`perf record` och Callgrind på ett switchmättat wasm64-prov pekade på
`vsnprintf` via `tb_f` för varje tabellbyte. Byteutmatningen använder nu
`tb_s`/`tb_u`/`tb_c` i stället och producerar byteidentisk assembler. Matchad
Clang 23 `-O3 -march=x86-64-v3`, `096ead5` → `0a06d20`: fem växelvisa
`perf stat`-körningar per binär på 2 000 switchfunktioner gav medianer för
kompilatorprocessens user-instruktioner / user-cykler: x86 −17,91 / −13,28 %,
arm64 −17,02 / −12,85 %, wasm32 −18,67 / −14,13 %, wasm64 −18,65 / −13,64 %.
Regelfria x86/wasm64-kontroller ändrade instruktioner −0,0013 / −0,0017 %;
cykeltal på denna delade maskin varierar och är inte en generell hastighetsvinst.
En separat Callgrind-mätning med 200 switchfunktioner gav −12,1–13,1 % Ir.
Kommandon, binär-/indatahashar och alla mätvärden finns i
`work/opt-profile/switch-speed/{perf-stat.json,run.log}`. O0–O3-körning och
optimeringskontroller passerar på fyra mål; wasm assembleras även av clang.
Detta mäter kompilering, inte den genererade kodens körtid.

### Integrering på main: aritmetik och switch (2026-11-11)

De tidigare PR #77–#82 mergades in i featuregrenar efter att deras föräldrar
redan mergats. Integreringscommit `d24fb15` för in hela stacken på main
`ccbbc21`: bitregler, gränsjämförelser, switch-bitset, switch-värdetabeller,
wasm `.size` och snabbare tabellutmatning. Konflikter i planerna löstes genom
att behålla båda sidors historik; emitter- och CI-ändringarna från main finns
kvar.

Unity och filvis bygge passerar. I båda bygglägen körs bitregler,
gränsjämförelser, switchtabeller, bitset och konstantkontroll O0–O3 på x86,
arm64, wasm32 och wasm64; relevanta noopt-fall, extern wasm-assemblering och
arm64-direktobjekt med PIC/debug/CFI ingår. Emitter-namnkontrakten klarar
ASan/UBSan. Vcc-sviten: arm64 252/252 och en befintlig xfail; x86 247/247
körbara och sex referensundantag.

Matchad seriell Callgrind-grind för hela integreringen mot `ccbbc21`, båda
binärer byggda med clang 23 `-O3 -march=x86-64-v3`, gav compiler-Ir: x86
+0,169 %, arm64 +0,143 %, wasm32 +0,274 %, wasm64 +0,248 %. 12/12 nativefiler,
10 wasm32 och 11 wasm64 ingår; atomic_ops/bitint_runtime som avvisas av båda
kompilatorerna på respektive wasm-mål redovisas separat. Grinden på 1 %
passerar. Källträds-/binärhashar, exakta kommandon och fall finns i
`work/opt-profile/consolidated-main-cost/result.json`. Detta är en liten fast
korpus med kompilatorinstruktioner, inte en gräns för vägg- eller CPU-tid.

### Typgränser skrivna som konstantuttryck (2026-11-11)

`emit/opt/cmpbounds.c` känner nu även igen rena, begränsade heltalsuttryck
som typgränser, exempelvis `x < (-2147483647-1)` och `x <= (unsigned)-1`.
Ett högst 16 noder stort uttryck kontrolleras före den befintliga
konstantevaluatorn; anrop, variabelläsningar och okända uttryck faller tillbaka
till vanlig kodgenerering. Den andra operandens sidoeffekter bevaras.
Fokuserade körnings- och kodgenereringstester passerar O0–O3 på x86, arm64,
wasm32 och wasm64 med både unity- och filvis bygge. O0 och `VCC_NOOPT=1`
gav byteidentisk assembler mot basen på alla fyra mål. Vcc-sviten passerar
252/252 med en befintlig xfail, och x86-regressionen passerar.

Matchad seriell Callgrind-profil mot `90b5cf8` gav förändringar i
kompilatorinstruktioner på −0,017 % för x86, −0,014 % för arm64, −0,031 %
för wasm32 och −0,026 % för wasm64. Den fasta 1 %-grinden passerar; 12/12
nativefiler, 10 wasm32 och 11 wasm64 ingår, medan atomic_ops/bitint_runtime
som avvisas av båda på respektive wasm-mål redovisas separat. Proveniens,
kommandon och fall finns i `work/opt-profile/bounds-expressions-cost/result.json`.
Detta är en liten korpus och instruktionstal, inte ett mått på CPU-tid.
Generell O2-täckning och G5/G7 återstår.

### Identiska heltalsoperander (2026-11-11)

Ett gemensamt recept i `emit/opt/repeated.c` förenklar `x-x` och `x^x` till
noll och `x&x`/`x|x` till en läsning för samma ordinära automatiska
heltalsvariabel. Adresstagna, volatile, atomiska, statiska och externa värden
avvisas; inga godtyckliga uttryck jämförs. Alla tre emitterfamiljer använder
beslutet. Fokuserade O0–O3-prov passerar på x86, arm64, wasm32 och wasm64
med både unity och filvis bygge; två volatile-läsningar bevaras. O0 och
`VCC_NOOPT=1` ger byteidentisk assembler mot basen. Vcc-sviten passerar
253/253 med en befintlig xfail.

Matchad seriell Callgrind-grind mot `22a91b8` gav förändringar i
kompilatorinstruktioner +0,099 % x86, +0,121 % arm64, +0,115 % wasm32 och
+0,123 % wasm64; 1 %-grinden passerar. 12 nativefiler, 10 wasm32 och 11
wasm64 ingår; wasm-fall som båda avvisar och mätproveniens finns i
`work/opt-profile/repeated-integer-cost/result.json`. Det är en liten korpus
med instruktionstal, inte CPU-tid; generell O2-täckning och G5/G7 återstår.

### Wasm: heltalsjämförelse med noll (2026-11-11)

Ett gemensamt, begränsat recept i `emit/opt/cmpzero.c` känner igen
heltalslikhet med noll och osignerad `x <= 0` (även med omvända operander).
Wasm sänker dessa direkt till `i32.eqz`/`i64.eqz` i stället för en konstant
och en jämförelse. Konstantuttryck kontrolleras med den befintliga begränsade
konstantevaluatorn; den andra operanden evalueras en gång. Signerad relation,
dynamisk andra operand och sidoeffekter omfattas av regressionstesten.
Båda bygglägenas fokuserade O0–O3-prov passerar på fyra mål; O0 och
`VCC_NOOPT=1` ger byteidentisk assembler mot basen. Vcc-sviten passerar
253/253 med en befintlig xfail. Receptet används endast där ett enskilt
målkommando ger konkret effekt; native-sänkningen är oförändrad.

Matchad seriell Callgrind-grind mot `22a91b8` gav förändringar i
kompilatorinstruktioner −0,028 % x86, −0,024 % arm64, +0,137 % wasm32 och
+0,139 % wasm64; 1 %-grinden passerar. 12 nativefiler, 10 wasm32 och 11
wasm64 ingår; fallen atomic_ops/bitint_runtime som båda avvisar på respektive
wasm-mål är redovisade i `work/opt-profile/zero-comparisons-cost/result.json`.
Det är en liten korpus och instruktionstal, inte CPU-tid. G5/G7 och generell
O2-täckning är fortsatt öppna.

Samma recept omfattar nu också heltalsjämförelser mellan två läsningar av
samma ordinära lokala variabel, även direkt i x86:s villkorssänkning.
Flyttalsjämförelser behålls på grund av NaN. Utökade fokuserade O0–O3-prov
passerar på fyra mål i båda bygglägen; vcc-sviten 254/254 med en befintlig
xfail och O0/noopt-assembler identisk med den stackade föräldern. Matchad
Callgrind mot PR #89:s `16d5feb` för hela den utökade regeln: x86 +0,133 %,
arm64 +0,123 %, wasm32 +0,139 %, wasm64 +0,109 % kompilatorinstruktioner;
1 %-grinden passerar på samma begränsade korpus. Exakta fall och proveniens:
`work/opt-profile/repeated-expanded-cost/result.json`. Detta mäter inte CPU-tid.

Kompletterande kort CPU-tidsprofil mot PR #89:s binär: fem växelvisa
`perf stat task-clock:u`-körningar per binär på syntetiska filer med 12 000
funktioner. Median bas → kandidat, x86: identitetsmättad 25,21 → 23,18 ms,
kontroll utan träff 28,80 → 28,21 ms; wasm64: mättad 22,83 → 22,32 ms,
kontroll 25,19 → 25,87 ms. Kontrollernas assembler är identisk. Den lilla,
delade värdens korta sampel varierar och visar inte att CPU-tidspåslaget på
verkliga projekt är under 1 %. Mätkommandon, binär-/indatahashar och alla
sampel finns i `work/opt-profile/repeated-cputime/{measure.py,result.json}`.

Kompletterande verkliga indata: två förprocessade Linux-TU:er bland de första
30 tillgängliga accepterades av den fristående `-O2 -ffreestanding`-proben.
Fem växelvisa `task-clock:u`-sampel per binär gav median bas → kandidat
23,03 → 23,02 ms för `kernel__printk__sysctl.o.i` och 15,89 → 15,59 ms för
`fs__fs_dirent.o.i`; assemblern var identisk i båda fallen. De övriga 28
misslyckades i denna förenklade körning och ingår inte: detta selektiva,
lilla urval bevisar inte kompilatorns kostnad på ett helt bygge. Kommandon,
hashar och sampel finns i `work/opt-profile/repeated-cputime/kernel*.{py,json}`.

Bredare CPU-tidsprov för samma binärer: efter att ha återställt relevanta
flaggor från kärnans `.cmd` accepterades 28 av de första 30 förprocessade
Linux-TU:erna av båda. Tre växelvisa seriella rundor av samtliga 28 gav
median användar-CPU 0,78945 → 0,79150 s (+0,26 %); system-CPU och väggtid
finns också i rådata. Två TU:er exkluderades efter kandidatens preflight.
Urvalet är litet, värden är delad och mätningen avser arm64-målet, inte ett
helt kärnbygge eller wasm. Exakta flaggor, käll-/binärhashar och sampel finns i
`work/opt-profile/repeated-cputime/kernel-batch{.py,-result.json}`.

### Dubbla unära heltalsoperationer (2026-11-11)

`emit/opt/double-unary.c` identifierar `-(-x)` och `~(~x)` för likatypade
32-/64-bitars heltal; varje emitter evaluerar då `x` en gång. Flyttal,
BitInt och mellanliggande typkonverteringar lämnas till vanlig sänkning.
Fokuserade körnings-/kodgenereringsprov O0–O3 passerar på fyra mål i båda
bygglägen, inklusive anrop, volatile och smal konvertering. O0/noopt ger
byteidentisk assembler mot den stackade föräldern; vcc-sviten passerar
255/255 med en befintlig xfail.

Matchad Callgrind mot `e1391de` gav compiler-Ir +0,045 % x86, +0,049 %
arm64, +0,106 % wasm32 och +0,119 % wasm64 på den fasta lilla korpusen;
1 %-grinden passerar, med samma explicit redovisade wasm-uteslutningar.
Tre växelvisa seriella rundor på 28 förprocessade Linux-TU:er med arm64-flaggor
gav median användar-CPU 0,79154 → 0,78655 s. Två TU:er är exkluderade och
den delade värdens lilla urval bevisar ingen generell tidsvinst. Proveniens,
flaggor och sampel: `work/opt-profile/double-unary-cost/result.json` och
`work/opt-profile/double-unary-cputime/kernel-batch-result.json`. G5/G7 och
generell O2-täckning återstår.

### Nästa steg: begränsad lokal återanvändning av värden (2026-11-11)

En konkret lucka är `(a+b)*(a+b)`: aktuell x86, arm64 och wasm emitterar
två additioner, medan Clang 23 `-O2` beräknar summan en gång. Även när
summan tilldelas en lokal variabel beräknas motsvarande uttryck om. Det här
är lokal CSE, inte löst av identitetsreglerna ovan.

Första kontrollunderlaget är `tests/vcc/local_value_reuse.c`, som körs O0–O3
på fyra mål via `--rule localvalues`: vanlig återanvändning och kommutering,
ändrad operand, anrop som ändrar global data, volatile, möjlig pekaralias och
kontrollflödesjoin. Dessa är endast semantiska regressioner; de bevisar inte
att någon optimering är implementerad. Basen passerar alla 16 kombinationer.

Genomför i små granskbara steg: (1) beskriv rena heltalsuttryck med typ,
operation och stabila värdeidentiteter i en begränsad tabell per grundblock;
kräv budget och billig kandidatkontroll så vanlig kod slipper allokering och
fullständiga extra vandringar; (2) invalidera vid definitioner, okända effekter,
kontrollflödesgränser och aliaserande minne; volatile/atomic, trappande och
flyttalsmiljöberoende operationer får aldrig återanvändas utan separat bevis;
(3) ge x86/arm64-adaptrarna korrekt registerlivstid, implicit register-/flaggeffekt
och debugremappning, och wasm en typad lokal/stackhantering snarare än
assemblertextomskrivning; (4) lägg till kodformskrav först när respektive
backend implementerats, samt budget-/barriär-/alias-/joinnegativfall och
O0/noopt-paritet. Mät faktiskt kompilerings-CPU och instruktioner för både
kontroller och uttrycksmättade filer samt den sammansatta stacken. Ingen av
punkterna är implementerad eller godkänd ännu; hela O2 och G5/G7 återstår.

Första byggstenen finns lokalt i `emit/flow/values.{h,c}`: en fast 32-platsers
tabell utan allokering med explicita budgetsteg. Kollision kan bara kasta bort
möjligheter; vid budgetslut publiceras inga fler fakta före reset. Adaptern
måste själv skapa nya värde-ID vid definition, nollställa vid barriär och
bevisa att ett resultat fortfarande finns i en användbar plats. ASan/UBSan-
kontrakt testar olika nyckeldelar, kollision, blockgräns och budgetslut;
unity- och filvis bygge samt de 16 semantiska mål-/nivåproven passerar.
Ingen backend använder tabellen ännu; den optimerar alltså ingen kod och är
inte en genomförd CSE-lösning. Nästa arbete är en faktisk konsument med säker
platshantering och dokumenterad kandidat-/kostnadsgräns.

Första konsumenten finns nu i `emit/opt/sibling-values.c`: när båda
operanderna till en heltalsoperation är rena, strukturellt identiska uttryck
av ordinära lokala värden, beräknas uttrycket en gång. Begränsningen är 32
AST-noder och 64 tabelloperationer per kandidat; billig rotkontroll föregår
allokeringen av den lilla tabellen på stacken. Kollision och budgetslut ger
konservativ fallback. Kommutativa inre operationer kan ha omvända operander.
Flyttal, BitInt, indirekta läsningar, volatile, atomic, adresstagna lokaler,
anrop och obevisade operationer avvisas. Native behåller värdet i separata
register; wasm använder en typad temporär lokal. Detta är endast återanvändning
mellan syskon i ett uttryck, inte generell CSE över satser eller block.

O0–O3 körnings-/kodgenereringsprov passerar på x86, arm64, wasm32 och wasm64
i båda bygglägen, inklusive nästlade uttryck, alias, volatile, atomic, anrop
och joins. ASan/UBSan-tabellkontrakt passerar; O0/noopt-assembler är identisk
mot `b261315`. Vcc-sviten passerar 256/256 med en befintlig xfail. Matchad
Callgrind-korpus mot samma bas gav compiler-Ir +0,085 % x86, +0,088 % arm64,
+0,100 % wasm32, +0,087 % wasm64; 1 %-grinden passerar. Tre växelvisa
seriella rundor av 28 förprocessade arm64-kärn-TU:er gav median användar-CPU
0,79110 → 0,79335 s (+0,28 %). Två TU:er är exkluderade och det begränsade
urvalet på delad värd bevisar inte helt bygges eller wasm-kostnad. Proveniens
finns i `work/opt-profile/local-values-cost/result.json` och
`work/opt-profile/local-values-cputime/kernel-batch-result.json`. Återbruk över
satser/block, generell CSE och G5/G7 återstår.

Efter att #93 mergats till main och arm64:s ABI-ändringar tillkommit byggdes
båda sidor om mot exakt main `87975fa`. Fyrmålsproven passerar i båda
bygglägen och arm64:s korskompilator-ABI passerar 34/34. Den matchade fasta
Callgrind-grinden ger nu compiler-Ir +0,112 % x86, +0,113 % arm64, +0,119 %
wasm32 och +0,117 % wasm64; 1 %-grinden passerar med samma redovisade
wasm-uteslutningar. Denna mätning ersätter inte CPU-tid och underlag finns i
`work/opt-profile/local-values-main-cost/result.json`.

### Lokal initiering följd av ren retur (2026-11-11)

Det begränsade värdenumreringsreceptet återanvänder nu även ett ordinärt lokalt
heltalsvärde i en direkt efterföljande ren retur: `unsigned x=a+b; return
x*(a+b);` beräknar summan en gång på x86, arm64 och wasm. Returnoden måste
redan läsa `x`, så dess initiering inte kan försvinna som oanvänd. Samma
32-nods-/64-tabellbudget och rena heltalskrav används; annan sats, skrivning,
anrop, indirekt minne, volatile, atomic, adresstagna variabler och obevisade
uttryck avstår. Substitutionen gäller endast under den retursatsen och sparas/
återställs per emitterkontext. Detta är ännu inte generell blocklokal CSE.

O0–O3 körnings-/kodgenereringsprov passerar på fyra mål i båda bygglägen,
inklusive skrivning och volatile-lokal som negativa fall. O0/noopt-assembler
är identisk mot `597c521`; vcc-sviten passerar 256/256 med en befintlig xfail.
Matchad Callgrind mot den föräldern gav compiler-Ir +0,102 % x86, +0,088 %
arm64, −0,003 % wasm32 och +0,004 % wasm64 på den fasta lilla korpusen; 1 %-
grinden passerar. Tre växelvisa seriella rundor på 28 förprocessade arm64-
kärn-TU:er gav median användar-CPU 0,79289 → 0,78492 s; två TU:er är
uteslutna och det lilla urvalet på delad värd bevisar ingen generell tidsvinst.
Detaljer och hashad proveniens: `work/opt-profile/decl-return-cost/result.json`
och `work/opt-profile/decl-return-cputime/kernel-batch-result.json`.
Generell lokal CSE, G5/G7 och full O2-täckning återstår.
Samma 28-TU-prov vid O0 gav median användar-CPU 0,76171 → 0,76136 s över
tre växelvisa rundor. Detta korta sampel på delad värd är inte en generell
CPU-tidsgrind; rådata finns i `work/opt-profile/decl-return-o0-cputime/`.

Utökning: returregeln söker nu bakåt över högst åtta rena lokala
heltalsdeklarationer i samma block. Skrivningar, anrop, volatile/atomic,
cleanup, kontrollflöde och obevisade initierare stoppar sökningen. En fast
kandidats nyckel bevaras under sökningen även om den direktmappade tabellen
får en kollision; budgetslut avstår fortfarande. Fokuserade kodforms- och
körningsprov täcker ren mellanliggande deklaration samt skrivnings-, anrops-
och volatilebarriärer på alla fyra mål O0–O3 och båda bygglägen. O0/noopt är
byteidentiskt mot föräldern; vcc-sviten 256/256 med en befintlig xfail och
x86-debugrader O0–O3 passerar.

Ny matchad Callgrind-profil mot `597c521`: compiler-Ir +0,114 % x86, +0,094 %
arm64, +0,010 % wasm32, +0,015 % wasm64; 1 %-grinden passerar på den fasta
begränsade korpusen med tidigare redovisade wasm-uteslutningar. Sju växelvisa
rundor på 28 förprocessade arm64-kärn-TU:er gav median användar-CPU O2
0,78091 → 0,78181 s och O0 0,76889 → 0,76712 s. Ett tidigare tresampelsprov
varierade mer; dessa små, selektiva prover på delad värd ger inte en generell
CPU-tidsgräns. Rådata och hashad proveniens finns i
`work/opt-profile/decl-return-expanded-cost/`, `decl-return-expanded-cputime/`,
`decl-return-expanded-cputime7/` och `decl-return-expanded-o0-7/`. Sökning över
andra satser, joinpunkter och generell CSE återstår.

Samma begränsade blockskanning återanvänder nu värden även i en senare ren
lokal heltalsinitiering, inte enbart i retur. Mottagaren måste redan läsa den
tidigare lokalen; skrivning, anrop, volatile/atomic och övriga barriärer
avvisas som tidigare. Nya körnings- och kodformskrav passerar O0–O3 på fyra
mål i båda bygglägen, inklusive en skrivande initierare som negativfall.
O0/noopt-assembler är byteidentisk mot `597c521`; vcc-sviten är 256/256 med
en befintlig xfail och x86:s debugprov passerar.

Den uppdaterade fasta Callgrind-korpusen mot samma bas ger compiler-Ir
+0,151 % x86, +0,130 % arm64, +0,036 % wasm32 och +0,031 % wasm64; 1 %-
grinden passerar med tidigare redovisade uteslutningar. Sju växelvisa O2-
rundor på 28 förprocessade arm64-kärn-TU:er gav median användar-CPU 0,78387 →
0,78438 s; tre O0-rundor gav 0,76742 → 0,75814 s. Två TU:er exkluderades och
korta prover på delad värd ger ingen generell tidsgräns. Proveniens och sampel:
`work/opt-profile/local-values-decl-cost/`, `local-values-decl-cputime/` och
`local-values-decl-o0/`. Generell blocklokal CSE och reuse över joins återstår.

Den begränsade blockskanningen kan nu passera ordinära lokala tilldelningar,
inkrement och enkla sammansatta tilldelningar. Skrivna symboler samlas upp;
kandidaten avvisas om den cachade lokalen eller någon indata har skrivits.
Pekare, volatile/atomic, anrop, dolda skrivningar, division/shift och andra
obevisade effekter är barriärer. Renhetskontrollen av en tilldelnings högerled
skjuts upp tills en kandidat överlevt invalidationskontrollen; enkla skrivna
indata avvisas innan tabellen skapas. Testerna täcker oberoende skrivning,
skriven indata, skrivet cachat värde, sammansatt tilldelning, inkrement och
dold skrivning/anrop/volatile; fyra mål O0–O3 och båda bygglägen passerar.
O0/noopt ger byteidentisk assembler mot `597c521`; vcc 256/256 med en
befintlig xfail, x86-debug och ASan/UBSan-tabellkontrakt passerar.

Matchad Callgrind mot samma förälder: compiler-Ir +0,156 % x86, +0,147 %
arm64, +0,044 % wasm32 och +0,046 % wasm64; 1 %-grinden passerar med samma
redovisade uteslutningar. Sju växelvisa O2-rundor på 28 förprocessade
arm64-kärn-TU:er gav median användar-CPU 0,78569 → 0,78586 s; två TU:er är
uteslutna och det korta urvalet på delad värd visar ingen generell tidsgräns.
Ett avsiktligt mättat kontrollprov med 2 000 ogiltigförklarade kandidater gav
+2,32 % compiler-Ir på x86 och +2,52 % på wasm64, med identisk assembler.
Före den uppskjutna kontrollen och snabbavvisningen var motsvarande +3,77 %
och +4,26 %. Det är ett verkligt avgränsat kostnadsfall, inte hela projektets
kostnad. Exakta kommandon, binär-/källhashar och sampel finns i
`work/opt-profile/local-values-writes-fast-cost/`,
`local-values-writes-fast-cputime/`, `local-values-writes-saturated/` och
`local-values-writes-fast-saturated/`. Generell CSE och joins återstår.
