# Diagnostik i världsklass för vcc

**Mål:** vcc:s felmeddelanden ska vara bättre än clangs för all C (inte bara kärnan), med
det bästa från rustc, Elm, Zig, Swift och GCC 15/16.  Clang är golvet, inte facit: vi
behöver inte vara byte-identiska, och där vi kan göra bättre gör vi det.  Bakgrund och
källor: `RESEARCH-diagnostics.md`.

**Regler:**
- **Det som verktyg tolkar är fast:** första raden `fil:rad:kol: nivå: text [-Wflagga]`,
  clangs `-W`-namn och grupper (`-Wno-x`, `-Werror=x` i byggsystem), exit-status,
  `-fno-color-diagnostics`/`-fdiagnostics-*`-flaggorna clang har.  Text, snippet,
  noter, fix-its och följdfel får vi göra bättre än clang, även i standardläget.
- **Clang som mätpunkt:** samma fel ska hittas på samma plats (eller en bättre, med
  motivering); varje avvikelse i text är ett medvetet val som syns i en snapshot.
- Ingen kostnad utan fel: `bench/vbench.py gate` (1 %) på varje steg.  Poster byggs bara på
  felvägen.
- Data från clang hämtas en gång och ligger i genererade filer (som `ppdata*.inc`).
- Varje steg har en valideringsrad; ett steg är klart först när den är grön.

## Utgångsläge (2026-11-09)

| | |
|---|---|
| clang 23 diagnos-id totalt | ~7 300 (3 916 err, 1 770 warn, 1 120 note, 461 ext/compat) |
| varav C (heuristik, `bench/clangdiag.py stats`) | 2 985 (1 468 err, 876 warn, 245 ext, 385 note); ~15 % är C++ som läcker in, så ~2 500 |
| vcc | 295 diagnosanrop, 245 distinkta format, 7 `-W`-flaggor |
| täckning (`bench/vcc-diagcov.py`) | 105 clang-diagnoser (70 err, 11 warn, 5 ext, 16 note i C); 47 vcc-format säger mindre än clangs (typen eller detaljen saknas); 78 finns bara i vcc (gränser, "unsupported", interna fel) |
| `tests/diag/` | 66 fall, exakt lika med clang (blir snapshots av vcc:s egen text, D1.2) |
| felåterhämtning | varje `expect()`-miss och 46 `fatal()` avslutar TU:n |
| utskrift | `pp/diag.c` formaterar med printf och skriver direkt till fd; ingen post sparas |

## Teststrategin (gäller alla steg)

Tio lager; varje steg nedan säger vilka det berör.

1. **Clang som mätpunkt** — `vcc-diag.py`: för varje fall samma exit-status och samma
   mängd (fil, rad, kolumn, nivå, flagga) i första raderna som clang; texten jämförs inte.
   Medvetna avvikelser (bättre plats, färre följdfel) listas i fallet med en motivering
   (`/* diag-differs: ... */`).
2. **Täckning** — `bench/vcc-diagcov.py` (D0.1): clangs C-relevanta id:n mot vcc:s; listar
   vad som saknas, per område, och vilka vi har som clang aldrig ger på samma fall.
3. **Mutationskorpus** — `bench/vcc-mutate.py` (D0.2): ta giltiga TU:er (`tests/vcc`,
   PostgreSQL, kärnan), injicera ett fel (ta bort en token, byt `;`/`,`, döp om en
   identifierare, ändra en typ, ta bort `}`), kör vcc och clang.  Mäter per mutant:
   - ingen krasch/timeout, exit 1, minst ett fel;
   - **första felet** på samma plats som clangs (rad, kolumn ±token);
   - **följdfel**: antal fel utöver det första, vcc mot clang — målet är färre;
   - ett fel som clang hittar men vcc inte (och tvärtom), per mutationstyp.
   Fast frö, ~2 000 mutanter, < 30 s med `-j 6`.  Siffrorna förs in i planen per steg.
4. **Snapshots** — `tests/diag/*.c` med `*.stderr`, `*.json`, `*.sarif` per renderare,
   `--bless` för att skriva om; normaliserade sökvägar.  Inline-förväntningar
   (`// expect-error: REGEX`, `@+N`, rustc/Swift-stil) för fall där bara innehållet spelar
   roll — samma format som T4 `reject` i TODO.md, så de två slås ihop.
5. **Rundresa** — (a) en Python-renderare i `bench/` som bygger textutdata ur JSON måste ge
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
10. **Mänsklig granskning** — `bench/vcc-diag-catalog.py`: en HTML-sida med varje fall i
    clangs och vcc:s text och JSON sida vid sida (Elms error-message-catalog).  Ändrade
    meddelanden granskas där innan commit; vcc ska vinna eller vara lika i varje rad.

## Steg

### D0. Mätning och korpus (inget synligt ändras)

- **D0.1 clangs tabell som data.**  *Klart 2026-11-09.*  `bench/clangdiag.py fetch` hämtar
  `Diagnostic*.td` för Homebrew-clangs version till `work/clang-td/23.1.4/` (17 filer,
  1,2 MB); `bench/clangdiag.py` tolkar dem (namn, klass, standardnivå, `-W`-grupp,
  komponent, kategori, mall, språkhink) på 0,5 s.  `bench/vcc-diagcov.py` (1,2 s) matchar
  vcc:s format mot mallarna åt båda hållen och ger täckning, vcc-only och "säger mindre
  än clang"; `--missing [REGEX]` listar det som saknas per `-W`-grupp, `-v` allt.
  Siffrorna står i Utgångsläge.  Språkhinken är en heuristik (namn-token, kategori,
  grupp, första alternativet i meddelandet): stickprov visar ~15 % C++ i C-hinken.
  Textmatchningen är en startpunkt; när diagnoserna har id (D1.3) läser täckningen dem.
  `diagids.inc` för vcc självt genereras i D1.1.
  *Validerat:* 7 297 diagnoser (diagtool: 7 298), 830 grupper, 20 substitutioner.
- **D0.2 mutationskorpus** enligt lager 3.  *Klart 2026-11-09.*  `bench/vcc-mutate.py`:
  TU:er som både clang och vcc godtar (gcc-torture, tcc-tests2, `tests/vcc` med
  vcc-ctests flaggor, och kärn-TU:er), ett fel injicerat i huvudfilen av elva mutatorer
  (`del-semi`, `del-close`, `del-open`, `del-token`, `dup-token`, `semi-comma`,
  `typo-ident`, `typo-keyword`, `change-type`, `insert`, `operator`), `-fsyntax-only
  -ferror-limit=0` med båda.  Clangs svar cachas, så en omkörning kör bara vcc (22 s
  för 1 970 mutanter med `-j 6`; kallt 112 s).  `show KATEGORI [MUTATOR]` visar fallen
  med båda kompilatorernas fel; allt i `build/ccs/mutate/last.jsonl`.
  **Baslinje** (frö 1, 1 970 mutanter ur 1 800 torture- och 145 kärn-TU:er):

  | | |
  |---|---|
  | krascher / timeouts | 0 / 0 |
  | missade (clang avvisar, vcc godtar) | 34 |
  | falska (clang godtar, vcc avvisar) | 20 |
  | båda godtar (mutationen blev giltig C) | 304 |
  | båda avvisar | 1 612 |
  | första felet på clangs plats / samma rad / annanstans | 82,6 % / 8,2 % / 9,2 % |
  | följdfel jämfört med clang: färre / lika / fler | 21,3 % / 60,2 % / 18,5 % |
  | fel totalt | vcc 2 715, clang 3 505 |

  Vad siffrorna säger (grupperat per meddelande):
  - **Första felet annanstans** (148): nästan alltid ett `,` i stället för `;` i radslut.
    Clang säger "expected ';' at end of declaration / after expression / after return
    statement" på raden (92 fall); vcc säger "expected expression" eller "expected
    identifier" på nästa rad.  Clangs heuristik: ett `,` följt av radbrytning och en
    token som inte kan fortsätta uttrycket.  Även `unsigned double` (7) diagnostiseras inte.
  - **Färre fel** (343) är oftast att vcc slutar efter första parsningsfelet där clang
    återhämtar sig och hittar fler (D2.2): "expected ')'", odeklarerade namn,
    "conflicting types", "function definition is not allowed here".
  - **Fler fel** (298): följdfel efter ett felstavat nyckelord (`cas 61:`, `struc S {`,
    `cha str[]`): clang föreslår nyckelordet ("did you mean 'struct'?") och går vidare;
    vcc ger "expected ';' after expression" och odeklarerade namn efteråt.
  - **Missade** (34): parameterlista utan typer i en deklaration (8), `main` med fel
    parametertyper (4), fältelement av ofullständig typ (4), anrop/definition med
    ofullständig returtyp (7), returtyp som inte stämmer (2).  I TODO.md.
  - **Falska** (20): `-Wno-error=int-conversion` / `incompatible-pointer-types` styr inte
    vcc (TODO.md "Warnings as errors not controllable"), `-Wreturn-mismatch` hör inte
    till gruppen `return-type`, och ett saknat `;` före `}` i en struct är i clang en
    varning ("expected ';' at end of declaration list"), i vcc ett fel.
  *Validerat:* tre körningar i rad ger identiska siffror (efter en rättning: cachade
  clang-svar kom som listor, vcc:s som tupler).
  **Efter `,` för `;` (2026-11-09):** clangs två heuristiker: ett `,` före något som inte
  kan börja ett uttryck (`{`, `}`, `if`, `for`, `while`, `else`, `goto`, en
  deklarationsspecificerare) avslutar uttrycket, och `expect_semi` tar `,` eller `:` som
  ett felskrivet `;` (fix-it, parsningen går vidare); i en deklaration är ett `,` sist på
  raden före något som inte kan börja en deklarator ett `;` ("expected ';' at end of
  declaration").  Första felet på clangs plats 82,6 → 88,7 %, annanstans 9,2 → 3,5 %;
  `semi-comma` 154 av 155 på clangs plats (den sista pekar vcc på själva `,`, clang på
  följdfelet).  Fler följdfel än clang 18,5 → 17,4 %.  Test: `tests/diag/comma_semi.c`.
  **Efter felstavade nyckelord (2026-11-09):** typo-rättningen erbjuder nyckelord som
  clang (Semas CTypeSpecs där en typ kan stå, satsnyckelorden i början av en sats;
  `break`/`continue`/`case`/`default` bara där de är tillåtna, `bool` bara i C23) och
  förkastar, som clang, en rättning när namnet är kortare än tre gånger avståndet
  (`in` blir inte `int`, `i` inte `u`).  Ett valt nyckelord parsas *som* nyckelordet
  (token-id skrivs om): `stati int x;`, `struc S s;`, `retur x;`, `(doubl)x`.  Dessutom
  clangs ParseImplicitInt: efter specificerare utan typ (`static in f(void)`, `const
  unsigne long *p`) är ett okänt namn som inte följs av `[ ( ) ; , = { :`/`asm` ett
  okänt typnamn, och en identifierarlista utan typer utanför en definition (`void
  exit(in);`) är clangs fel.  `typo-keyword`: först på clangs plats 145 → 163 av 165,
  fler följdfel 74 → 7, missade 7 → 1; totalt första felet på clangs plats 88,7 →
  89,7 %, fler följdfel 17,4 → 13,3 %, missade 34 → 27.  Kvar i kategorin: kaskader på felnoder (`in
  y[8];` ger "subscripted value is not an array" på `y`), `as volatile (...)`; den
  missade är en K&R-definition mot en tidigare prototyp (TODO.md).
  Test: `tests/diag/keyword_typo.c` (med `.expect`: vcc visar fix-it även för
  `(doubl)x`, där clang tappar den), `tests/diag/kr_decl.c`.  `vcc-diag.py` tar nu
  `X.c.expect` där vcc medvetet säger mer än clang: vcc:s text måste stämma med filen
  och diagnosraderna (`fil:rad:kol: nivå:`) med clangs.
  **Efter felnoder (2026-11-09):** clangs ogiltiga uttryck och deklarationer.  Ett uttryck
  som redan fått sitt fel blir en felnod (`err_node`, `NF_ERR`: int 0, lvalue), och
  operatorerna (binära, tilldelning, `?:`, unära, cast, index, medlem, anrop) ger en
  felnod utan nytt fel när en operand är det; postfix-operatorer efter en felnod parsas
  bara (`postfix_err`).  En deklaration vars typ var fel (okänt typnamn, `void`-variabel,
  ofullständig typ, ett `void`-fält eller -parameter) märks `SF_INVALID` / `field.invalid`,
  och varje användning blir en felnod.  Samtidigt clangs första fel som saknades:
  `'void' cannot be signed or unsigned`, `'long struct' is invalid`, `'short double' is
  invalid`, `cannot combine with previous 'X' declaration specifier` (med det tidigare
  ordet; `char int` är nu ett fel som i clang), `'void' must be the first and only
  parameter`, `argument may not have 'void' type`, ofullständig parameter- och
  resultattyp i en definition.  Första felet på clangs plats 89,7 → 91,0 % (annanstans
  3,5 → 2,4 %), fler följdfel 13,3 → 11,2 %, missade 27 → 23; `change-type` först på
  clangs plats 54 → 81.  Kvar bland följdfelen är mest parserns återhämtning ("expected
  X", D2.2).  Prestanda: `decl_specs` körs för varje deklaration, medlem och parameter
  (~100 000 per kernel-TU), så kontrollerna ligger på den felaktiga token i kalla
  funktioner utan pekare till loopens variabler (`&sgn` tvingade dem till minnet: +0,4 %);
  parametrarnas typ läses en gång (`KIND`).  fe +0,27 % mot förra steget; grenen ligger på
  +0,91 % mot merge-basen, nära budgeten på 1 %.  Test: `tests/diag/error_nodes.c`,
  `tests/diag/spec_combine.c`.

### D1. Strukturerade diagnoser (grunden)

- **D1.0 snabba vinster.**  *Klart 2026-11-09.*  De 47 vcc-format som sade mindre än
  clangs: nu med typen, fältet, förväntat/givet antal argument, indexet och gränsen, och
  clangs intervall (`~~~`) och "forward declaration"-noter där clang har dem.  Dessutom:
  - `-Wexcess-initializers` är en varning som i clang (var ett fel för första
    elementet och tyst för resten), en gång per lista;
  - `_Generic` pekar på det styrande uttrycket; `'int[2]'` utan mellanslag som clang;
  - följdfel borta efter ogiltig unär operand och ogiltig vektor/skalär-konvertering;
  - attributnamnet i "expected string literal as argument of 'section' attribute",
    utan efterföljande "expected ')'";
  - `invalid operand number 3 in inline asm string (2 operands, 0 labels)`: mer än clang.
  Kvar i listan (13): falska träffar och medvetna val (vcc:s `impossible constraint`
  och `unknown asm operand name` namnger operanden, clangs gör det inte).  Fynd som
  hör till D2 eller är egna buggar står i TODO.md ("Designated initializers found by
  PLAN-diag D1.0", "Invalid code accepted").
  *Validerat:* `vcc-diag.py` 71/71 (5 nya fall: `detail_types`, `detail_fields`,
  `detail_builtins`, `excess_init`, `attr_string`); `vcc-sema.py` batch/run 878/879,
  warn 879/879; `vcc-ctests.py` 1740/1740; `vcc-stress.py` 22/22; `vcc-driver.py`
  41/41; `vbench.py gate` ok (fe +0,29 %, cc +0,24 %).  Täckning: 105 → 136 clang-
  diagnoser, "säger mindre" 47 → 13.
- **D1.1 posten.**  `vdiag`: id (clangs namn, t.ex. `err_typecheck_convert_incompatible`;
  egna med `vcc_`-prefix), nivå, flagga, primärt spann + etikett, sekundära spann +
  etiketter, barn (note/help), förslag (spann, text, *tillämplighet*: maskin / kanske /
  platshållare), makro- och include-kontext.  Allokeras i en arena per TU.  Byggs med
  `d = diag_begin(c, ID, loc); diag_label(d, ...); diag_fix(d, ...); diag_emit(d);`
  `report()`, `cc_error()`, `note()` och `pp_diagx()` blir tunna omslag, så de ~200
  anropsställena kan flyttas över ett i taget.  `diagids.inc` genereras ur `.td`-filerna
  (bara C-hinken och de id vcc använder), som `ppdata*.inc`.
  *Validera:* lager 1 (66/66), 6, 7.
  *Klart 2026-11-09.*  `vdiag` i `pp/pp.h`: id, format, flagga, meddelande, primär plats och
  etikett, spann med etiketter, fix-its med tillämplighet (`VDA_MACHINE/MAYBE/PLACEHOLDER`),
  makrots token, barn (noter) och `track` (för D1.3).  Byggs i TU:ns `perm`-arena med
  `pp_diag_begin/label/fix/note/emit`; `pp_diag_records()` ger TU:ns poster i ordning.  En
  not som rapporteras efter sin diagnos blir dess barn; "expanded from macro" läggs direkt
  efter den diagnos den hör till.  Textutskriften läser bara posten (`render_text`), så
  JSON/SARIF (D3) får samma innehåll.  `report`, `note`, `cc_warnx`, `pp_warnx`,
  `pp_diagx` och `pp_vdiag` är tunna omslag; flaggan ligger i posten, inte i texten.
  Konverteringsfelen som clang har som varningar med fel som standard (`-Wint-conversion`,
  `-Wincompatible-pointer-types`) och `#error`/`#warning` sätter id och flagga själva.
  `diagids.inc` (`bench/gen-diagids.py`, incheckad): varje format vcc rapporterar får
  clangs namn när texten matchar (som `vcc-diagcov.py`, även `?:`-format) och annars
  `vcc_` + formatets ord; posten slår upp formatet (binärsökning, bara på felvägen).  270
  format, 240 id, 72 `vcc_*`.  `--check` säger om tabellen är inaktuell.
  Lager 7 hittade en kvadratisk väg som fanns före posten: varje odeklarerat namn sökte
  typo-rättning över alla symboler (40 000 fel: 7,3 s).  Clangs `-fspell-checking-limit`
  (50 försök per TU, misslyckade räknas; `-fno-spell-checking`) införd, samma antal
  rättningar som clang; nu 0,19 s.  `vcc-stress.py` har fallet (`errors-40k`, budget 3 s).
  *Validerat:* `vcc-diag.py` 78/78; `vcc-sema.py` batch 878/879, warn 879/879;
  `vcc-ctests.py` 1740/1740; `vcc-stress.py` 23/23; `vcc-driver.py` 41/41; `vccd-test.py`
  19/19; PostgreSQL med vcc: 1 504 filer, `meson test` 81 ok, 0 fel; `vbench.py gate` ok
  (fe +0,94 %).
- **D1.2 textrenderaren.**  Dagens `vdiagx_`/`snippet` flyttas till en renderare som tar
  en post; i början samma utseende som clang.  `tests/diag/` får vcc:s egen förväntade
  text (`*.stderr`, `--bless`) och `vcc-diag.py` byter till lager 1:s jämförelse, så
  att texten sedan får bli bättre.
  *Validera:* lager 1, 4 + 5a (JSON-rundresan bevisar att posten räcker).
  *Klart 2026-11-09 (lager 5a med D3.1).*  `render_text` tar en post (D1.1).  `vcc-diag.py`
  jämför nu två saker: vcc:s egen text mot `X.c.stderr` (lager 4, `--bless` skriver om), och
  (fil, rad, kolumn, nivå, `-W`-flagga) för varje diagnosrad samt exit-status mot clang
  (lager 1).  En medveten skillnad i lager 1 deklareras i fallet med `/* diag-differs: ... */`.
  `.expect` blev `.stderr`.  Sammanfattningen räknar också fall vars text fortfarande är
  identisk med clangs (77 av 78), så det syns när texten börjar bli bättre.
  *Validerat:* 78/78; en ändrad snapshot ger FAIL med diff.
- **D1.3 id i alla anropsställen** och `-fdiagnostics-show-id` (id efter flaggan) samt
  `VCC_TRACK_DIAG=1` (vcc:s egen källplats per diagnos, som rustcs `-Z track-diagnostics`).
  *Validera:* lager 2: varje vcc-diagnos har ett id som finns i `diagids.inc` eller är `vcc_*`.
  *Klart 2026-11-09.*  Id:t sitter i anropsställets format (posten slår upp det, D1.1); de
  format som delas av flera clang-diagnoser (konverteringsfelen med `%s`-prefix, `#error`)
  sätter id:t själva, och `#define`-felen fick ett format var.  `gen-diagids.py` matchar nu
  även pp:s `error()`/`warn()` och har en lista med fästa id där textmatchningen tar fel
  (`err_typecheck_convert_incompatible`, `fatal_too_many_errors`, pp:s `missing ')' after`):
  303 format, 268 id, 75 `vcc_*`.  `-fdiagnostics-show-id` skriver ` [id]` efter flaggan.
  `VCC_TRACK_DIAG=1` skriver ` {vcc fil.c:rad funktion}` (med `-g`, t.ex. ASan-bygget) eller
  ` {vcc funktion+offset}`: rapportfunktionerna sparar sin returadress i posten, `atos`
  översätter bara när det skrivs ut.  Två första försök (makron som skickade
  `__FILE_NAME__:__LINE__` som argument, eller lagrade det i en trådlokal vid anropet)
  kostade pp +0,6 % resp. +1,6 % (en TLV-åtkomst lyfts in i heta funktioner på Darwin).
  *Validerat:* lager 2 i `vcc-diag.py` (varje diagnosrad med `-fdiagnostics-show-id` har ett
  id) 78/78; mutationskorpusen kör vcc med id: 81 olika id över 3 158 fel, 0 utan id;
  `gen-diagids.py --check`; `vbench.py gate` ok (pp +0,07 %, fe +0,89 %).

### D2. Felåterhämtning utan kaskader

- **D2.1 feltyp och garanti.**  En feltyp/feldeklaration som alla kontroller tyst godtar
  (rustcs `TyKind::Error`); den kan bara skapas med en token som bara fås genom att ett fel
  sänts (`ErrorGuaranteed`).  En `diag_delayed_bug()` som blir intern fel om TU:n slutar
  utan fel.
- **D2.2 `expect()` blir icke-fatal.**  Synkronisering på `;`, `}` och början av nästa
  deklaration; saknat `;` efter `struct`; obalanserade klamrar; de 46 `fatal()` gås
  igenom ett och ett.  Målet är **färre följdfel än clang**, med samma första fel.

  *Status (steg 1 klart):* ett parsningsfel (`fatal`, saknad stängare) hoppar till närmaste
  återhämtningspunkt: en `_setjmp` per block, struct-kropp, for-huvud och TU (aldrig per
  sats; macOS `setjmp` gör ett systemanrop).  Där återställs räknare och scopes, och resten
  av satsen hoppas över som GCC:s `skip_to_end_of_block_or_statement` (balanserat, `;` i
  parenteser hoppas över, initierarnas öppna `{` stängs först, ett `else` efter en
  överhoppad if går vidare med else-grenen; en `}` direkt efter `(` eller ett operatortecken
  stänger inget och hoppas över).  Saknad `)`, `]` eller `}` är clangs
  BalancedDelimiterTracker: felet (en gång per plats) och "to match this", sedan fram till
  stängaren men inte förbi `;`, och vidare.  Efter ett ogiltigt uttryck tiger vcc som clang
  (anropet blir ogiltigt, ingen antalskontroll; satsen hoppas över till `;`).  Clangs egna
  former: "extraneous closing brace ('}')", "extraneous ')' before ';'", "expected
  identifier or '('", "expected function body after function declarator", "expected ';'
  after top level declarator" (resten av deklarationen hoppas över om ingen ny börjar),
  "expected member name or ';' after declaration specifiers", "type name requires a
  specifier or qualifier" (`struct { a, b; }`), "cannot combine with previous 'struct'
  declaration specifier" (`int struct S x`), saknat `;` före `}` i en struct är en varning,
  for-huvudets "expected ';'" hoppar fram till nästa `;` eller `)`.  Hittat på vägen:
  `skip_balanced` hoppade bara över parenteser (en initierare av ofullständig typ åt bara
  sin `{`).  Mutationskorpusen (mot 76c0909): första felet på clangs plats 91,0 → 91,7 %,
  fler följdfel än clang 11,2 → 6,7 %, samma 68,0 → 79,4 %, färre 20,8 → 13,9 %; fel totalt
  2323 → 3006 (clang 3518); spurious 20 → 17; inga krascher.  `tests/diag/recovery.c`: tolv
  oberoende fel, exakt clangs utdata.  Prestanda: fe +0,62 % mot merge-basen (nyckelords-
  korrigeringens ParseImplicitInt-test flyttat till loopens utgång: −0,22 %).

  *Status (steg 2, målet nått):* fler följdfel än clang 6,7 → 4,8 %, samma 79,4 → 82,3 %,
  första felet på clangs plats 91,7 → 93,2 % (annanstans 3,2 → 1,9 %).  Det som tog dit:
  en deklaration som felar efter sitt namn deklarerar det ändå, ogiltigt (en platshållare
  som en senare deklaration ersätter tyst); en parameterlista utan `)` lämnar funktionen
  deklarerad men ogiltig, och anrop av en ogiltig deklaration är tysta; `'void' must be the
  first and only parameter` först när listan är parsad (som clang); ogiltiga specificerare
  (`signed double`, `void long`, `'long struct' is invalid` efter en tagg) gör deklarationen
  ogiltig, och ett bitfält eller en cast av ogiltig typ säger inget mer; för många/få
  argument avbryter anropet (argumenten konverteras inte), liksom en tilldelning till ett
  icke-lvärde (clangs "non-object type 'T' is not assignable" för funktioner); kommat
  sprider felnoder; en ogiltig `return`-operand hoppas tyst till `;`; en saknad `;` efter en
  prototyp före en ny deklaration är "after top level declarator", inte "expected function
  body"; ett makroanrop som når filens slut tas bort med sitt namn, som i clang (`clang -E`
  likadan).  `tests/diag/recovery_decl.c` nytt.  Prestanda: fe +0,89 % mot merge-basen
  (preprocessorns felväg i en kall funktion: utan den kostade sex rader +0,27 % pp).
  Kvar: `f(a; )` (clangs "unexpected ';' before ')'"), avbrutna attribut, clangs
  parentes-socker i typnamn (`double (*())`), "expected ';' after struct" när en
  typspecificerare följer en definition, de 46 `fatal()` ett och ett.
- **D2.3 dubbletter** (`void v;`, sammansatt literal i parentes, typförslag som kaskaderar:
  TODO.md "Error recovery").
  *Validera:* lager 3: inga krascher, första felet på clangs plats i ≥ 95 %, följdfel
  inte fler än clangs i ≥ 95 % av mutanterna och färre totalt; nya `tests/diag/`-fall med flera oberoende fel; T5 stress
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

### D4. Bättre textutdata (standard)

Det här blir standardutseendet, inte ett eget läge; första raden behåller formatet.
`-fdiagnostics-style=clang` finns kvar så länge det är billigt, för den som vill jämföra.

- Etiketterade multispann i källordning (rustc/ariadne), flerradsspann, tabbar och breda
  tecken rätt.
- **Nästling** i stället för platta noter (GCC 16): makrokedjor, include-stack, "previous
  declaration", parametrar som inte stämmer.
- **Kontrastfärg** per sida i jämförelser (GCC 15) och **typdiff**: bara den del som skiljer
  (en `const`, en `*`, en struct-tagg, ett fält i en funktionspekartyp) markeras.
- **Rollnamn** (Elm): "argument 2 till 'memcpy'", "returvärdet från 'f'".
- `help:`-rader med en åtgärd; teckenuppsättning `unicode|ascii`; läsbart läge (miette:s
  "narratable") under `NO_COLOR` utan tty.
  *Validera:* lager 1 (samma platser som clang), 4, 8, 10.

### D5. Innehåll (ett delsteg per område, i värde-per-insats-ordning)

Varje delsteg: nya fall i `tests/diag/` (snapshots, och clang som mätpunkt), lager 6 grönt.

1. **Saknade varningar** från TODO.md (`-Wunused-variable`, `-Wuninitialized`,
   `-Wformat`, `-Wconstant-conversion`, ...) — prioriteras med lager 2:s tabell efter hur
   ofta clang ger dem på PostgreSQL och kärnan.
2. **Makron**: expanderad text med caret i, ramar från systemheaders vikta,
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
| M1 | D0, D1 | typen/detaljen i 47 meddelanden (D1.0); mätbart: täckning och mutationsbaslinje |
| M2 | D2 | färre avbrutna TU:er, flera fel per körning |
| M3 | D3, D4 | JSON/SARIF, ny standardtext |
| M4 | D5.1–D5.4, D6 | varningar, makrovy, fix-its, `--explain` |
| M5 | D5.5–D5.8, D7 | resten av innehållet, vccd-dedup |

D1 och D2 hänger ihop (feltypen behöver posten); D0.2 först så att D2 har en baslinje.

## Status

- [x] D0.1 — 2026-11-09
- [x] D0.2 — 2026-11-09
- [x] D1.0 — 2026-11-09
- [x] D1.1 – D1.3 — 2026-11-09
- [ ] D2.1 – D2.3
- [ ] D3.1 – D3.3
- [ ] D4
- [ ] D5.1 – D5.8
- [ ] D6
- [ ] D7
- [ ] D8
