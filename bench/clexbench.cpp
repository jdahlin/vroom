// clexbench LIST [REPS]: clang's own lexer (libclang-cpp, raw mode, gnu11) over
// every file in LIST, files pre-read, one thread, best of REPS.  Compared with
// `lextest core`: the same corpus, the same token definition (no comments or
// whitespace).  "+ident lookup" also looks every identifier up in an
// IdentifierTable, as the preprocessor does (our interning); "warm" keeps
// the table across reps, as vcc's shared table is.
#include <chrono>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

#include <pthread/qos.h>

#include "clang/Basic/IdentifierTable.h"
#include "clang/Basic/LangOptions.h"
#include "clang/Lex/Lexer.h"

using namespace clang;

int main(int argc, char **argv)
{
	pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
	std::ifstream list(argv[1]);
	int reps = argc > 2 ? atoi(argv[2]) : 5;
	std::vector<std::string> bufs;
	std::string path;
	size_t bytes = 0;

	while (std::getline(list, path)) {
		std::ifstream f(path, std::ios::binary);
		std::string s((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());

		bytes += s.size();
		s.push_back('\0');	// clang wants a NUL-terminated buffer
		bufs.push_back(std::move(s));
	}
	LangOptions lo;
	std::vector<std::string> includes;
	LangOptions::setLangDefaults(lo, Language::C, llvm::Triple("aarch64-linux-gnu"), includes,
				     LangStandard::lang_gnu11);
	IdentifierTable warm(lo);	/* mode 2: kept across reps, like vcc's table */

	for (int mode = 0; mode < 3; mode++) {
		double best = 1e9;
		size_t toks = 0, ids = 0;

		for (int r = 0; r < reps; r++) {
			IdentifierTable cold(lo);
			IdentifierTable &it = mode == 2 ? warm : cold;
			auto t0 = std::chrono::steady_clock::now();

			toks = 0;
			for (auto &b : bufs) {
				const char *s = b.data(), *e = s + b.size() - 1;
				Lexer lex(SourceLocation(), lo, s, s, e);
				Token t;

				do {
					lex.LexFromRawLexer(t);
					toks++;
					if (mode && t.is(tok::raw_identifier))
						it.get(t.getRawIdentifier());
				} while (!t.is(tok::eof));
				toks--;	// eof
			}
			double d = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
			best = d < best ? d : best;
			ids = it.size();
		}
		printf("clang raw lexer%s: %.1f MB, %.2f M tokens: best of %d %.1f ms = %.2f GB/s, %.1f ns/token%s\n",
		       mode == 2 ? " + ident lookup (warm table)" : mode ? " + ident lookup (cold table)" : "", bytes / 1e6, toks / 1e6, reps, best * 1e3,
		       bytes / best / 1e9, best * 1e9 / toks,
		       mode ? (" (" + std::to_string(ids) + " identifiers)").c_str() : "");
	}
}
