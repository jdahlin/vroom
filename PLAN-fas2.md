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
| | **2.8 klar** (utom vdso-länken): `vlink` byte-identisk med lld (30 ms mot 340 ms), `vobjcopy`/`vnm` identiska med llvm-objcopy/llvm-nm, Image bootar | |
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
  - minnet: 2,8 GB lagrat vid `-j 6`, taket är `VPP_HC_MAX_MB`;
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
- **Prestanda (2026-11-08, laddad maskin, load ~4):** emittern på alla 840 TU:er: 459 ms CPU (0,55 ms/TU, varav utskrift 122 ms), 4,48 M instruktioner = 17,9 MB maskinkod, 39 MB/s, 103 ns/instruktion, en tråd per TU (`bench/vcc-emit-stats.py`). `kernel/sched/core.c` ensam: 2,9 ms med 1 tråd, 1,26 ms med 2, 1,04 ms med 4. Roofline (`bench/emit-roofline.c`, 29 755 instruktioner): maskinkod 0,10 ns/instruktion (~40 GB/s), text 0,74 ns/instruktion; vi ligger två storleksordningar över, det mesta i generering och utskrift, inte i minnesbandbredd.
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
  - vdso-länken (`-shared` med `.dynamic`/`.dynsym`/hash/versioner) går fortfarande via lld.
  - Koppla in `vlink`/`vobjcopy`/`vnm` i bygget: shimmen i stället för vroom-ld och llvm-objcopy/llvm-nm.
  - Kör länken i vccd (ingen process, indata redan i minnet).
  - `Image` kan skrivas direkt från vmlinux.unstripped parallellt med strip.

### Status 2.9 (2026-11-08): vccd kompilerar kärnans `.c` och `.S` själv, objekt utan asm-text
- `ppdriver.c`: `-c` av en `.c` kör pp (header-cachen delas mellan jobben) + cc.c + emit.c och ger ett ELF-objekt; `-c` av en `.S` kör pp-utdata genom as.c i minnet (`as_set_dir` för relativa `.incbin`). Ett `As`/`ElfObj`/utbuffert (4 GB NORESERVE) per tråd, objektet skrivs med en `write`, `-MD`/`-MMD`-depfilen som clangs. `cc_run`:s tabeller byggs en gång (`pthread_once`).
- `vccd.c`: `-c` av namngiven `.c`/`.S` med `--target=aarch64-*-linux*` körs i arbetartråden (jobbtyp `cc-own`); värdprogram (Mach-O), vDSO (`-mcmodel=tiny`, kräver PIC), `-fpic`/`-shared`/stdin går till vroomd som förut, och likaså varje TU vcc fallerar på (räknas, loggas med `VCC_LOG`). `VCC_CC=0` vidarebefordrar allt (A/B). Räknare vid SIGUSR1/SIGTERM. Trådstackar 16 MB.
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

Kvar (18): **fp 8** = 128-bitars long double (clang viker aritmetiken, vcc
behöver mjukflyttal: `__addtf3`/`__multf3`/`__eqtf2`… i runtimen och 16-byte
flyttalsvärden i q-register i emittern; egen uppgift), **link 6** = `link_error`-
test som kräver en optimerare (värdeintervall, DCE), **run 3** = bcp-1
(`__builtin_constant_p` efter inlining), eeprof-1 (`-finstrument-functions`),
20021127-1 (clang behandlar en egen `llabs` som builtin), **emit 1** = pr70460
(etikettdifferenser `&&a - &&b` i statiska initierare).

## Risker
- **GNU C-ytan och inline asm** är det stora jobbet. Uppskattat: lexer + pp ~2 kloc,
  parser + sema ~10–15 kloc, emitter ~5 kloc, assembler ~4 kloc, länkare ~3 kloc.
- **Korrekthet vid -O0-liknande kodgenerering.** Kärnan förutsätter inlining och DCE.
  Det löses i sema (steg 2.5–2.6), inte genom kärnpatchar.
- **make självt.** Även med v5 tar make ~0,3–0,5 s att läsa alla Makefiles och starta
  ~1 000 jobb. Shimsens latens och make `-j` blir golvet.
- **Mätbrus.** Maskinen används till annat. Bara A/B-mätningar körda direkt efter
  varandra jämförs.

### Felmeddelanden (clang-format)

Status: `bench/vcc-diag.py` jämför vccs stderr med clangs (-fsyntax-only) på
`ccs/vcc/tests/diag/*.c`: 61/61 identiska (från 4/48).  `bench/vcc-sema.py
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

Kvar: "expanded from macro"-noter (kräver makropositioner i pp:s
platsrymd), varningar som clang avgör efter nåbarhet (-Wdivision-by-zero,
-Wshift-count-overflow, -Warray-bounds), fler -Wall-varningar.
