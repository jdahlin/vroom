| Command | Mean Wall Time [ms] | Change | Factor |
|:---|---:|---:|:---|
| `/opt/homebrew/opt/llvm/bin/clang -O0 -c f0.c -o /tmp/claude-a.o` | 185.8 ± 9.7 |  |  |
| `/opt/homebrew/opt/llvm/bin/clang -O2 -c f0.c -o /tmp/claude-a.o` | 1364.1 ± 15.5 | +634.0% | (7.3x slower) |
| `/opt/homebrew/opt/llvm/bin/clang -fsyntax-only f0.c` | 105.2 ± 0.8 | -43.4% | (1.8x faster) |
| `/opt/homebrew/opt/llvm/bin/clang -E f0.c -o /tmp/claude-a.i` | 57.2 ± 0.6 | -69.2% | (3.2x faster) |
| `/opt/homebrew/opt/llvm/bin/clang -O0 -c empty.c -o /tmp/claude-a.o` | 109.5 ± 4.2 | -41.1% | (1.7x faster) |
| `/opt/homebrew/opt/llvm/bin/clang -O0 -c nothing.c -o /tmp/claude-a.o` | 47.7 ± 0.5 | -74.3% | (3.9x faster) |
| `/Users/johandahlin/dev/vroom/third_party/tcc-install/bin/tcc -c f0.c -o /tmp/claude-b.o` | 11.9 ± 0.2 | -93.6% | (15.6x faster) |
| `/Users/johandahlin/dev/vroom/third_party/tcc-install/bin/tcc -c empty.c -o /tmp/claude-b.o` | 9.6 ± 0.2 | -94.8% | (19.4x faster) |
| `/Users/johandahlin/dev/vroom/third_party/tcc-install/bin/tcc -c nothing.c -o /tmp/claude-b.o` | 2.6 ± 0.1 | -98.6% | (72.7x faster) |
