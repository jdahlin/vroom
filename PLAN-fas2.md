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
| 2.2 | SIMD-lexer + tokencache | lexar alla 3 114 kärnfiler; > 1 GB/s/kärna; tokens identiska med clang `-dump-tokens` |
| 2.3 | Preprocessor | `vcc -E` ger samma tokenström som `clang -E` för alla 840 TU:er (normaliserad jämförelse) |
| 2.4 | Header-cache/prefixträd | `-E` för alla TU:er < 0,5 s väggtid totalt |
| 2.5 | Parser + sema | `-fsyntax-only` för alla TU:er utan fel; konstantuttryck = clang |
| 2.6 | Emitter arm64 | kompilerar alla TU:er; per-fil-diff av symboler och sektioner mot clang -O0; kärnan bootar med vcc-objekt + lld |
| 2.7 | Assembler | alla `.S` och all inline asm; byte-identiskt mot clang `-cc1as` för `.S` |
| 2.8 | Länkare + objcopy | `vmlinux` och `Image` utan lld; bootar |
| 2.9 | Allt i minnet, profilering, finjustering | kallt bygge < 2 s, bootar Debian till `running` |

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
## Risker
- **GNU C-ytan och inline asm** är det stora jobbet. Uppskattat: lexer + pp ~2 kloc,
  parser + sema ~10–15 kloc, emitter ~5 kloc, assembler ~4 kloc, länkare ~3 kloc.
- **Korrekthet vid -O0-liknande kodgenerering.** Kärnan förutsätter inlining och DCE.
  Det löses i sema (steg 2.5–2.6), inte genom kärnpatchar.
- **make självt.** Även med v5 tar make ~0,3–0,5 s att läsa alla Makefiles och starta
  ~1 000 jobb. Shimsens latens och make `-j` blir golvet.
- **Mätbrus.** Maskinen används till annat. Bara A/B-mätningar körda direkt efter
  varandra jämförs.
