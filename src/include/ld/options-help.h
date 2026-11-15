#ifndef VCC_LINKER_OPTIONS_HELP_H
#define VCC_LINKER_OPTIONS_HELP_H

#include <stdio.h>

static inline void linker_options_print(void)
{
    puts(
        "ELF options\n"
        "  -o -r --relocatable -i -shared --shared -Bshareable -Bsymbolic "
        "--Bsymbolic\n"
        "  --whole-archive -whole-archive --no-whole-archive "
        "-no-whole-archive\n"
        "  --start-group --end-group -( -) --pic-veneer --eh-frame-hdr\n"
        "  --no-eh-frame-hdr -EL --no-undefined-version --no-undefined -zdefs "
        "-X\n"
        "  --discard-locals -x --discard-all -S --strip-debug -n --nmagic\n"
        "  --no-apply-dynamic-relocs --apply-dynamic-relocs --no-relax "
        "--gc-sections\n"
        "  --no-gc-sections --print-gc-sections --no-print-gc-sections "
        "--vroom-stubs\n"
        "  --emit-relocs -q --version -v -V -static -Bstatic --Bstatic -dn "
        "-non_shared\n"
        "  -Bdynamic --Bdynamic -dy -call_shared -no-pie --no-pie -pie --pie\n"
        "  --no-dynamic-linker --as-needed -as-needed --no-as-needed "
        "-no-as-needed -E\n"
        "  --export-dynamic -export-dynamic --no-export-dynamic --build-id\n"
        "  --build-id=STYLE @FILE -m VALUE --dynamic-linker VALUE "
        "-dynamic-linker VALUE\n"
        "  -I VALUE --hash-style VALUE --library-path VALUE -L VALUE --library "
        "VALUE -l\n"
        "  VALUE --script VALUE -T VALUE --entry VALUE -e VALUE -soname VALUE "
        "--soname\n"
        "  VALUE -h VALUE --orphan-handling VALUE --error-limit VALUE "
        "--threads VALUE\n"
        "  -Map VALUE --Map VALUE --vroom-comment VALUE -z VALUE\n"
        "PE/COFF options (one or two leading dashes)\n"
        "  --Bstatic --static --dn --non_shared --Bdynamic --dy --call_shared\n"
        "  --whole-archive --no-whole-archive --shared --dll --strip-all "
        "--strip-debug\n"
        "  --version --export-all-symbols --build-id --exclude-libs "
        "--no-guard-cf\n"
        "  --no-guard-longjmp --gc-sections --no-gc-sections --verbose "
        "--dynamicbase\n"
        "  --disable-dynamicbase --no-dynamicbase --high-entropy-va\n"
        "  --disable-high-entropy-va --disable-nxcompat --tsaware "
        "--disable-tsaware\n"
        "  --large-address-aware --appcontainer --disable-appcontainer\n"
        "  --disable-reloc-section --enable-reloc-section --insert-timestamp\n"
        "  --no-insert-timestamp --enable-auto-import --disable-auto-import\n"
        "  --enable-runtime-pseudo-reloc --disable-runtime-pseudo-reloc\n"
        "  --exclude-all-symbols --allow-multiple-definition\n"
        "  --no-allow-multiple-definition --kill-at --demangle --no-demangle\n"
        "  --fatal-warnings --no-fatal-warnings --build-id VALUE --entry "
        "VALUE\n"
        "  --subsystem VALUE --image-base VALUE --stack VALUE --heap VALUE\n"
        "  --file-alignment VALUE --section-alignment VALUE --major-os-version "
        "VALUE\n"
        "  --minor-os-version VALUE --major-image-version VALUE "
        "--minor-image-version\n"
        "  VALUE --major-subsystem-version VALUE --minor-subsystem-version "
        "VALUE\n"
        "  --out-implib VALUE --output-def VALUE --Map VALUE --require-defined "
        "VALUE\n"
        "  --undefined VALUE --sysroot VALUE --output VALUE --library-path "
        "VALUE\n"
        "  --library VALUE --exclude-libs VALUE --exclude-symbols VALUE --icf "
        "VALUE -o\n"
        "  FILE -e SYMBOL -u SYMBOL -L DIR -l NAME -m MACHINE -s -S\n"
        "Mach-O options\n"
        "  -o FILE -arch arm64 -L DIR -lNAME -dead_strip -no_dead_strip -pie "
        "-dynamic\n"
        "  -execute -demangle -platform_version PLATFORM MIN SDK -syslibroot "
        "DIR\n"
        "  --vroom-stubs\n"
        "WebAssembly selection\n"
        "  -mwasm64 (requires WebAssembly support)");
}

static inline void wasm_linker_options_print(void)
{
    puts(
        "WebAssembly linker options\n"
        "  -o FILE -L DIR -lNAME -z stack-size=N -m MACHINE -mllvm OPTION\n"
        "  --export-table --stack-first --no-growable-memory --growable-table\n"
        "  --allow-undefined --no-entry --gc-sections --no-gc-sections "
        "-mwasm64\n"
        "  -Bstatic --strip-debug --strip-all -Bdynamic --export VALUE\n"
        "  --export-if-defined VALUE --initial-heap VALUE --initial-memory "
        "VALUE\n"
        "  --max-memory VALUE --global-base VALUE --table-base VALUE "
        "--extra-features\n"
        "  VALUE --entry VALUE");
}

static inline void objcopy_options_print(void)
{
    puts("Object copy options\n"
         "  -S --strip-all --only-keep-debug -g --strip-debug -w --wildcard -O "
         "VALUE\n"
         "  --output-target VALUE -I VALUE --input-target VALUE -B VALUE\n"
         "  --binary-architecture VALUE --remove-section VALUE -R VALUE "
         "--only-section\n"
         "  VALUE -j VALUE --strip-unneeded-symbol VALUE --dump-section VALUE\n"
         "  --set-section-flags VALUE --rename-section VALUE "
         "--add-gnu-debuglink VALUE\n"
         "  --add-section VALUE --redefine-sym VALUE --prefix-symbols VALUE\n"
         "  --prefix-alloc-sections VALUE");
}

static inline void nm_options_print(void)
{
    puts("Symbol options\n"
         "  -p --no-sort -n -v --numeric-sort -S --print-size -u "
         "--undefined-only -U\n"
         "  --defined-only -g --extern-only -D --dynamic -A -o "
         "--print-file-name");
}

#endif
