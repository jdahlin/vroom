# Diagnostik i världsklass för vcc

**Mål:** vcc:s felmeddelanden ska vara bättre än clangs för all C (inte bara kärnan):
lika exakta som clang när man vill det, och med det bästa från rustc, Elm, Zig, Swift och
GCC 15/16 när man ber om det.  Bakgrund och källor: `RESEARCH-diagnostics.md`.

**Regler:**
- Standardutdata förblir **byte-identisk med clang** (`bench/vcc-diag.py`).  Allt nytt är
  opt-in (`-fdiagnostics-style=`, `-fdiagnostics-add-output=`) eller syns bara med färg,
  som GCC gör.  Första raden är alltid `fil:rad:kol: error: text` så verktyg kan tolka den.
- Ingen kostnad utan fel: `bench/vbench.py gate` (1 %) på varje steg.  Poster byggs bara på
  felvägen.
- Data från clang hämtas en gång och ligger i genererade filer (som `ppdata*.inc`).
- Varje steg har en valideringsrad; ett steg är klart först när den är grön.

## Utgångsläge (2026-11-09)

| | |
|---|---|
| clang 23 diagnos-id totalt | ~7 300 (3 916 err, 1 770 warn, 1 120 note, 461 ext/compat) |
| varav möjliga i C (uppskattning) | 1 500–2 000 |
| vcc distinkta meddelanden | ~200–400; 7 `-W`-flaggor |
| `tests/diag/` | 66 fall, exakt lika med clang |
| felåterhämtning | varje `expect()`-miss och 46 `fatal()` avslutar TU:n |
| utskrift | `pp/diag.c` formaterar med printf och skriver direkt till fd; ingen post sparas |

## Teststrategin (gäller alla steg)

Tio lager; varje steg nedan säger vilka det berör.

1. **Paritet** — `vcc-diag.py`: standardläget exakt lika med clang (stderr och exit-status).
2. **Täckning** — `bench/vcc-diagcov.py` (D0.1): clangs C-relevanta id:n mot vcc:s; listar
   vad som saknas, per område, och vilka vi har som clang aldrig ger på samma fall.
3. **Mutationskorpus** — `bench/vcc-mutate.py` (D0.2): ta giltiga TU:er (`tests/vcc`,
   PostgreSQL, kärnan), injicera ett fel (ta bort en token, byt `;`/`,`, döp om en
   identifierare, ändra en typ, ta bort `}`), kör vcc och clang.  Mäter per mutant:
   - ingen krasch/timeout, exit 1, minst ett fel;
   - **första felet** lika med clangs (plats + text);
   - **följdfel**: antal fel utöver det första, vcc mot clang;
   - helt identisk stderr.
   Fast frö, ~2 000 mutanter, < 30 s med `-j 6`.  Siffrorna förs in i planen per steg.
4. **Snapshots** — `tests/diag-rich/*.c` med `*.stderr`, `*.json`, `*.sarif` per renderare,
   `--bless` för att skriva om; normaliserade sökvägar.  Inline-förväntningar
   (`// expect-error: REGEX`, `@+N`, rustc/Swift-stil) för fall där bara innehållet spelar
   roll — samma format som T4 `reject` i TODO.md, så de två slås ihop.
5. **Rundresa** — (a) en Python-renderare i `bench/` som bygger clang-texten ur JSON måste ge
   exakt samma text som vcc (GCC:s `sarif-replay`-idé: posten innehåller allt); (b) fix-its:
   applicera, jämför med `*.fixed`, kompilera om utan det felet.
6. **Inga falska positiva** — `vcc-sema.py warn` 879/879 på kärnan, PostgreSQL-bygget
   (`bench/pg/`) utan nya varningar mot clang; senare SQLite och Lua som tredje/fjärde
   projekt så att varningarna inte blir kärnformade.
7. **Prestanda** — `vbench.py gate`; plus ett felrikt fall (10 000 fel i en TU med
   `-ferror-limit=0`) som inte får bli kvadratiskt.
8. **Fuzz** — nytt mål `fuzz-diag`: slumpade spann, etiketter och fix-its över slumpad
   källtext (tabbar, UTF-8, CRLF, långa rader) genom alla renderare: ingen krasch, inga
   läsningar utanför, bredder rätt.  `fuzz-pp` med `FUZZ_PP_PRINT=1` täcker pp-felen.
9. **Sanitizers** — diag-, mutations- och snapshot-sviterna även med `--bin-dir build/ccs-asan`.
10. **Mänsklig granskning** — `bench/vcc-diag-catalog.py`: en HTML-sida med varje meddelande i
    clang-, rich- och JSON-form sida vid sida (Elms error-message-catalog).  Ändrade
    meddelanden granskas där innan commit.

## Steg

### D0. Mätning och korpus (inget synligt ändras)

- **D0.1 clangs tabell som data.**  `ccs/vcc/diag/gen-diagids.sh` läser clangs
  `Diagnostic*Kinds.td` en gång och skriver `diagids.inc`: namn, nivå, standard-flagga,
  grupper, meddelandemall, språk (C / C++ / ObjC / OpenMP ...).  Kräver att .td-filerna
  hämtas från llvm-project (motsvarande tagg för Homebrew clang 23.1.4) — beslut av
  användaren.  `bench/vcc-diagcov.py` ger täckningstabellen.
  *Validera:* tabellen innehåller alla ~7 300 id:n som `diagtool` känner; C-andelen räknas
  exakt och förs in här.
- **D0.2 mutationskorpus** enligt lager 3, med dagens vcc som baslinje.
  *Validera:* körningen är deterministisk; baslinjesiffror (krascher, första-fel-paritet,
  följdfel) i planen.

### D1. Strukturerade diagnoser (grunden)

- **D1.1 posten.**  `vdiag`: id (clangs namn, t.ex. `err_typecheck_convert_incompatible`;
  egna med `vcc_`-prefix), nivå, flagga, primärt spann + etikett, sekundära spann +
  etiketter, barn (note/help), förslag (spann, text, *tillämplighet*: maskin / kanske /
  platshållare), makro- och include-kontext.  Allokeras i en arena per TU.  Byggs med
  `d = diag_begin(c, ID, loc); diag_label(d, ...); diag_fix(d, ...); diag_emit(d);`
  `report()`, `cc_error()`, `note()` och `pp_diagx()` blir tunna omslag, så de ~200
  anropsställena kan flyttas över ett i taget.
  *Validera:* lager 1 (66/66), 6, 7.
- **D1.2 renderare `clang`.**  Dagens `vdiagx_`/`snippet` flyttas till en renderare som tar
  en post.  Ordning och sammanslagning av noter som i clang.
  *Validera:* lager 1 + 5a (JSON-rundresan bevisar att posten räcker).
- **D1.3 id i alla anropsställen** och `-fdiagnostics-show-id` (id efter flaggan) samt
  `VCC_TRACK_DIAG=1` (vcc:s egen källplats per diagnos, som rustcs `-Z track-diagnostics`).
  *Validera:* lager 2: varje vcc-diagnos har ett id som finns i `diagids.inc` eller är `vcc_*`.

### D2. Felåterhämtning utan kaskader

- **D2.1 feltyp och garanti.**  En feltyp/feldeklaration som alla kontroller tyst godtar
  (rustcs `TyKind::Error`); den kan bara skapas med en token som bara fås genom att ett fel
  sänts (`ErrorGuaranteed`).  En `diag_delayed_bug()` som blir intern fel om TU:n slutar
  utan fel.
- **D2.2 `expect()` blir icke-fatal.**  Synkronisering på `;`, `}` och början av nästa
  deklaration; saknat `;` efter `struct`; obalanserade klamrar; de 46 `fatal()` gås
  igenom ett och ett.  Riktmärket i standardläget är **clangs felmängd**, inte färre fel.
- **D2.3 dubbletter** (`void v;`, sammansatt literal i parentes, typförslag som kaskaderar:
  TODO.md "Error recovery").
  *Validera:* lager 3: inga krascher, första-fel-paritet ≥ 95 %, följdfel inte fler än
  clangs i ≥ 95 % av mutanterna; nya `tests/diag/`-fall med flera oberoende fel; T5 stress
  grön; lager 9.

### D3. Maskinläsbar utdata

- **D3.1 JSON-rader** (`-fdiagnostics-format=json`): en post per rad, med `rendered`
  (exakt text, så verktyg aldrig renderar om).
- **D3.2 SARIF 2.1** (`-fdiagnostics-format=sarif`, `-fdiagnostics-add-output=sarif:file=`):
  sekundära platser, etiketter, include-kedjan som `locationRelationship`, fix-its som
  `fixes`, argv och cwd.  Text på stderr och SARIF samtidigt.
- **D3.3 OSC 8-länkar** på `[-Wflagga]` och id till dokumentationen (D6) när färg är på.
  *Validera:* lager 4 (snapshots), 5a; SARIF valideras mot det officiella schemat
  (vendrat i `bench/`); `vcc-driver.py` för de nya flaggorna.

### D4. Rich-renderaren (`-fdiagnostics-style=vcc`)

- Etiketterade multispann i källordning (rustc/ariadne), flerradsspann, tabbar och breda
  tecken rätt.
- **Nästling** i stället för platta noter (GCC 16): makrokedjor, include-stack, "previous
  declaration", parametrar som inte stämmer.
- **Kontrastfärg** per sida i jämförelser (GCC 15) och **typdiff**: bara den del som skiljer
  (en `const`, en `*`, en struct-tagg, ett fält i en funktionspekartyp) markeras.
- **Rollnamn** (Elm): "argument 2 till 'memcpy'", "returvärdet från 'f'".
- `help:`-rader med en åtgärd; teckenuppsättning `unicode|ascii`; läsbart läge (miette:s
  "narratable") under `NO_COLOR` utan tty.
  *Validera:* lager 4, 8, 10; standardläget orört (lager 1).

### D5. Innehåll (ett delsteg per område, i värde-per-insats-ordning)

Varje delsteg: nya fall i `tests/diag/` (clang-texten) och `tests/diag-rich/`, lager 6 grönt.

1. **Saknade varningar** från TODO.md (`-Wunused-variable`, `-Wuninitialized`,
   `-Wformat`, `-Wconstant-conversion`, ...) — prioriteras med lager 2:s tabell efter hur
   ofta clang ger dem på PostgreSQL och kärnan.
2. **Makron**: expanderad text med caret i (rich), ramar från systemheaders vikta,
   flernivå-"expanded from" (TODO.md "Diagnostic details").
3. **`static_assert`**: operanderna utvärderade (`48 == 56`), och för `sizeof` en
   layoutnot med var utfyllnaden kom ifrån.
4. **Include-tips**: `memcpy` → `<string.h>` som fix-it, från en genererad libc/POSIX-tabell.
5. **Standardversion**: C23-nyckelord, `-std=c11` mot C23-funktioner, GNU-utvidgningar
   under `-pedantic`.
6. **Portabilitet**: Linux mot Apple (`long double`, variadiska argument, `char`-tecken).
7. **UB vid konstantvikning**: skift ≥ bredd, signerat överspill, med en rad om varför.
8. **Attribut**: `format`, `nonnull`, `warn_unused_result`, `deprecated("...")`.

### D6. Fix-its, `--fix` och `--explain`

- **Swifts regel**: en fix-it på själva felet bara om den är den enda uppenbara; andra som
  noter, säkrast först.  Tillämplighet på varje förslag.
- `-fdiagnostics-generate-patch` (alla fix-its som unified diff) och `vcc --fix` som bara
  tillämpar maskin-säkra.
- `vcc --explain ID`: `ccs/vcc/docs/diag/ID.md` med ett felaktigt och ett rättat exempel,
  inkompilerade i en genererad tabell (inga filer vid körning).  Kod bara när
  förklaringen tillför något (rustcs regel).
  *Validera:* lager 5b; varje dok-exempel testas i CI: det felaktiga ger exakt det id:t, det
  rättade inget; `--fix` på PostgreSQL:s varningar ger ett bygge som klarar `meson test`.

### D7. Hela bygget i vccd

- Samma varning från en header skrivs en gång med "(också i N andra TU:er)"
  (`-fdiagnostics-dedup`, opt-in; standard är clangs beteende).
- Poster sparas per TU, så en omkörning ersätter exakt den TU:ns mängd; sammanfattning vid
  byggets slut (per flagga, per fil).
- Samma poster ut över vccd:s socket för en LSP/editor.
  *Validera:* `vccd-test.py`-fall (dedup, ersättning, socket); kärnbygget med `-Wall`:
  antal utskrivna dubbletter före/efter i planen; lager 7 på hela bygget.

### D8. Senare

Flödesvägar för `-Wuninitialized`, returnerad stackadress och saknad `return` (numrerade
steg, GCC-analysatorn); minnesdiagram för indexering utanför fält; HTML-utdata.

## Ordning och milstolpar

| milstolpe | steg | synligt för användaren |
|---|---|---|
| M1 | D0, D1 | inget (samma text), men mätbart: täckning och mutationsbaslinje |
| M2 | D2 | färre avbrutna TU:er, flera fel per körning |
| M3 | D3, D4 | JSON/SARIF, rich-läget |
| M4 | D5.1–D5.4, D6 | varningar, makrovy, fix-its, `--explain` |
| M5 | D5.5–D5.8, D7 | resten av innehållet, vccd-dedup |

D1 och D2 hänger ihop (feltypen behöver posten); D0.2 först så att D2 har en baslinje.

## Status

- [ ] D0.1 — väntar på beslut om att hämta clangs `.td`-filer
- [ ] D0.2
- [ ] D1.1 – D1.3
- [ ] D2.1 – D2.3
- [ ] D3.1 – D3.3
- [ ] D4
- [ ] D5.1 – D5.8
- [ ] D6
- [ ] D7
- [ ] D8
