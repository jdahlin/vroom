/*
 * rsh: kbuild's recipe subset of sh, run in vccd (see shell.h).
 *
 * Two passes: shell_tokenize and parse the whole script into a command list,
 * declining on anything outside the subset, then run it.  printf's output is
 * computed while parsing (its arguments are literals), which is also what
 * lets `printf ... | xargs TOOL` become one TOOL call with the words
 * appended.
 */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "utils/shell.h"
#include "common/buffer.h"

/* ---------------- growable buffers ---------------- */

/* ---------------- tokens ---------------- */

enum { SHELL_TOKEN_WORD, SHELL_TOKEN_SEMICOLON, SHELL_TOKEN_PIPE, SHELL_TOKEN_REDIRECT };

struct shell_token {
	int kind;
	bool assign;	/* NAME= before any quote */
	char *s;	/* SHELL_TOKEN_WORD: the word, quotes removed (malloc'ed) */
};

struct shell_token_list {
	struct shell_token *t;
	int n, cap;
};

static bool shell_token_push(struct shell_token_list *v, int kind, bool assign, char *s)
{
	if (v->n == v->cap) {
		int cap = v->cap ? 2 * v->cap : 64;
		struct shell_token *t = realloc(v->t, cap * sizeof(*t));

		if (!t)
			return false;
		v->t = t;
		v->cap = cap;
	}
	v->t[v->n++] = (struct shell_token){ kind, assign, s };
	return true;
}

static bool shell_char_is_name(char c, bool first)
{
	return c == '_' || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
	       (!first && c >= '0' && c <= '9');
}

/* characters that mean more shell than rsh knows, outside quotes */
static bool shell_char_is_unsupported(char c)
{
	return strchr("$`&()<{}*?[]~#!\n\r", c) != NULL;
}

static bool shell_tokenize(const char *s, struct shell_token_list *v)
{
	vbuffer w = VBUFFER_STICKY_INIT;

	for (;;) {
		bool quoted, assign, in_word;
		size_t name_end;

		while (*s == ' ' || *s == '\t')
			s++;
		if (!*s)
			break;
		if (*s == ';' || *s == '|' || *s == '>') {
			int k = *s == ';' ? SHELL_TOKEN_SEMICOLON : *s == '|' ? SHELL_TOKEN_PIPE : SHELL_TOKEN_REDIRECT;

			/* ;; || >> >| are other operators */
			if (s[1] == ';' || s[1] == '|' || s[1] == '>' || s[1] == '&')
				return false;
			if (!shell_token_push(v, k, false, NULL))
				return false;
			s++;
			continue;
		}
		/* a word: unquoted, '...' and "..." parts up to a blank or operator */
		w.n = 0;
		if (!vbuffer_append(&w, "", 0))
			return false;
		quoted = false;
		name_end = 0;
		assign = false;
		in_word = true;
		while (in_word && *s) {
			char c = *s;

			if (c == '\'') {
				const char *e = strchr(s + 1, '\'');

				if (!e || !vbuffer_append(&w, s + 1, e - s - 1))
					goto fail;
				quoted = true;
				s = e + 1;
			} else if (c == '"') {
				const char *e = s + 1;

				while (*e && *e != '"') {
					if (*e == '$' || *e == '`' || *e == '\\')
						goto fail;
					e++;
				}
				if (!*e || !vbuffer_append(&w, s + 1, e - s - 1))
					goto fail;
				quoted = true;
				s = e + 1;
			} else if (c == '\\') {
				/* \X is X ('\'' in kbuild's saved commands); \newline continues a line */
				if (!s[1] || s[1] == '\n' || !vbuffer_append(&w, s + 1, 1))
					goto fail;
				quoted = true;
				s += 2;
			} else if (c == ' ' || c == '\t' || c == ';' || c == '|') {
				in_word = false;
			} else if (c == '>') {
				/* 2>file and the like: an fd number glued to the redirection */
				goto fail;
			} else if (shell_char_is_unsupported(c)) {
				goto fail;
			} else {
				if (!quoted && !assign) {
					if (c == '=' && name_end == w.n && w.n > 0)
						assign = true;
					else if (name_end == w.n && shell_char_is_name(c, w.n == 0))
						name_end++;
				}
				if (!vbuffer_append(&w, s, 1))
					goto fail;
				s++;
			}
		}
		{
			char *word = strdup(w.p);

			if (!word || !shell_token_push(v, SHELL_TOKEN_WORD, assign, word)) {
				free(word);
				goto fail;
			}
		}
	}
	free(w.p);
	return true;
fail:
	free(w.p);
	return false;
}

/* ---------------- commands ---------------- */

enum { SHELL_COMMAND_NOP, SHELL_COMMAND_SET_E, SHELL_COMMAND_TEXT, SHELL_COMMAND_RM, SHELL_COMMAND_MKDIR, SHELL_COMMAND_TOOL, SHELL_COMMAND_FIXDEP };

struct shell_command {
	int kind;
	char **argv;		/* SHELL_COMMAND_RM, SHELL_COMMAND_MKDIR: the paths; SHELL_COMMAND_TOOL: the tool's argv (owned array) */
	char **assign;		/* SHELL_COMMAND_TOOL: NAME=VALUE (owned array) */
	const char *redir;	/* SHELL_COMMAND_TEXT: > FILE */
	char *text;		/* SHELL_COMMAND_TEXT: what echo/printf write (malloc'ed) */
	size_t textlen;
	bool xargs;		/* SHELL_COMMAND_TOOL run as printf | xargs: map the status */
};

struct shell_script {
	struct shell_command *c;
	int n, cap;
};

static struct shell_command *shell_script_command_add(struct shell_script *sc)
{
	if (sc->n == sc->cap) {
		int cap = sc->cap ? 2 * sc->cap : 16;
		struct shell_command *c = realloc(sc->c, cap * sizeof(*c));

		if (!c)
			return NULL;
		sc->c = c;
		sc->cap = cap;
	}
	memset(&sc->c[sc->n], 0, sizeof(sc->c[0]));
	return &sc->c[sc->n++];
}

/* NULL-terminated copy of n token words (the strings stay owned by the tokens) */
static char **shell_words_to_argv(struct shell_token *t, int n, vbuffer *extra)
{
	int nx = 0, i;
	char **v, *p;

	if (extra)
		for (p = extra->p; p && *p;) {
			while (*p == ' ' || *p == '\t' || *p == '\n')
				p++;
			if (!*p)
				break;
			nx++;
			while (*p && *p != ' ' && *p != '\t' && *p != '\n')
				p++;
		}
	v = malloc((n + nx + 1) * sizeof(*v));
	if (!v)
		return NULL;
	for (i = 0; i < n; i++)
		v[i] = t[i].s;
	/* xargs: split printf's output in place (it is ours) */
	if (extra)
		for (p = extra->p; p && *p;) {
			while (*p == ' ' || *p == '\t' || *p == '\n')
				*p++ = 0;
			if (!*p)
				break;
			v[i++] = p;
			while (*p && *p != ' ' && *p != '\t' && *p != '\n')
				p++;
		}
	v[i] = NULL;
	return v;
}

/* POSIX printf with %s and %% and \n \t \\: the format is reused while arguments remain */
static bool shell_printf_format(struct shell_token *a, int n, vbuffer *out)
{
	const char *fmt = a[0].s;
	int ai = 1;

	if (!vbuffer_append(out, "", 0))
		return false;
	do {
		bool used = false;
		const char *f;

		for (f = fmt; *f; f++) {
			if (*f == '\\') {
				char c = f[1] == 'n' ? '\n' : f[1] == 't' ? '\t' : f[1] == '\\' ? '\\' : 0;

				if (!c || !vbuffer_append(out, &c, 1))
					return false;
				f++;
			} else if (*f == '%') {
				if (f[1] == '%') {
					if (!vbuffer_append(out, "%", 1))
						return false;
				} else if (f[1] == 's') {
					const char *s = ai < n ? a[ai++].s : "";

					used = true;
					if (!vbuffer_append(out, s, strlen(s)))
						return false;
				} else {
					return false;
				}
				f++;
			} else if (!vbuffer_append(out, f, 1)) {
				return false;
			}
		}
		if (!used)
			break;
	} while (ai < n);
	return ai >= n;
}

/*
 * One simple command, tokens t[0..n): leading assignments, words, at most
 * one > FILE.  pipe_in: printf's output for the xargs side of a pipe.
 */
static bool shell_command_parse(struct shell_token *t, int n, struct shell_script *sc, const struct shell_callbacks *ops,
			 vbuffer *pipe_in, vbuffer *pipe_out)
{
	struct shell_token w[n > 0 ? n : 1];
	const char *redir = NULL, *name;
	int nw = 0, nasg = 0, i;
	struct shell_command *c;

	for (i = 0; i < n; i++) {
		if (t[i].kind == SHELL_TOKEN_REDIRECT) {
			if (redir || i + 1 >= n || t[i + 1].kind != SHELL_TOKEN_WORD)
				return false;
			redir = t[++i].s;
		} else if (t[i].kind != SHELL_TOKEN_WORD) {
			return false;
		} else if (t[i].assign && nw == nasg && !redir) {
			w[nw++] = t[i];
			nasg++;
		} else {
			w[nw++] = t[i];
		}
	}
	if (nw == nasg)
		return false;	/* assignments alone, or nothing */
	name = w[nasg].s;
	if (pipe_in) {
		/* the right side of printf | xargs TOOL ... */
		if (nasg || redir || strcmp(name, "xargs") || nw < 2 || w[1].s[0] == '-' ||
		    !ops->is_tool(ops->ctx, w[1].s) || strpbrk(pipe_in->p, "'\"\\"))
			return false;
		for (i = 0; pipe_in->p[i] == ' ' || pipe_in->p[i] == '\t' || pipe_in->p[i] == '\n'; i++)
			;
		if (!pipe_in->p[i])
			return false;	/* empty input: GNU and BSD xargs differ */
		if (!(c = shell_script_command_add(sc)))
			return false;
		c->kind = SHELL_COMMAND_TOOL;
		c->xargs = true;
		c->argv = shell_words_to_argv(w + 1, nw - 1, pipe_in);
		c->assign = shell_words_to_argv(w, 0, NULL);
		c->text = pipe_in->p;	/* the argv strings point into it */
		pipe_in->p = NULL;
		return c->argv && c->assign;
	}
	if (pipe_out && (strcmp(name, "printf") || nasg || redir))
		return false;
	if (nasg && !ops->is_tool(ops->ctx, name))
		return false;	/* VAR=x builtin: rsh only passes env to tools */
	if (!(c = shell_script_command_add(sc)))
		return false;
	if (!strcmp(name, "set")) {
		if (nw != 2 || strcmp(w[1].s, "-e") || redir)
			return false;
		c->kind = SHELL_COMMAND_SET_E;
	} else if (!strcmp(name, ":") || !strcmp(name, "true") || !strcmp(name, "trap")) {
		if (name[0] == 't' && name[1] == 'r')	/* trap: signal handlers only; EXIT/0 must run */
			for (int i = 2; i < nw; i++)
				if (!strcmp(w[i].s, "EXIT") || !strcmp(w[i].s, "0"))
					return false;
		if (!strcmp(name, "trap") && nw < 3)
			return false;
		if (redir)
			return false;	/* : > file creates it */
		c->kind = SHELL_COMMAND_NOP;
	} else if (!strcmp(name, "echo")) {
		vbuffer b = VBUFFER_STICKY_INIT;

		if (!vbuffer_append(&b, "", 0))
			return false;
		for (i = 1; i < nw; i++) {
			/* dash's echo takes -n and expands backslashes */
			if ((i == 1 && w[i].s[0] == '-') || strchr(w[i].s, '\\') ||
			    (i > 1 && !vbuffer_append(&b, " ", 1)) || !vbuffer_append(&b, w[i].s, strlen(w[i].s))) {
				free(b.p);
				return false;
			}
		}
		if (!vbuffer_append(&b, "\n", 1)) {
			free(b.p);
			return false;
		}
		c->kind = SHELL_COMMAND_TEXT;
		c->text = b.p;
		c->textlen = b.n;
		c->redir = redir;
	} else if (!strcmp(name, "printf")) {
		vbuffer b = VBUFFER_STICKY_INIT;

		if (nw < 2 || !shell_printf_format(w + 1, nw - 1, &b)) {
			free(b.p);
			return false;
		}
		if (pipe_out) {
			sc->n--;	/* no command: its output becomes xargs' words */
			*pipe_out = b;
			return true;
		}
		c->kind = SHELL_COMMAND_TEXT;
		c->text = b.p;
		c->textlen = b.n;
		c->redir = redir;
	} else if (!strcmp(name, "rm")) {
		if (nw < 2 || strcmp(w[1].s, "-f") || redir)
			return false;
		for (i = 2; i < nw; i++)
			if (w[i].s[0] == '-')
				return false;
		c->kind = SHELL_COMMAND_RM;
		c->argv = shell_words_to_argv(w + 2, nw - 2, NULL);
		return c->argv != NULL;
	} else if (!strcmp(name, "mkdir") || !strcmp(name, "/bin/mkdir")) {
		/* mkdir -p: $(shell mkdir -p $(obj)) in every submake */
		if (nw < 3 || strcmp(w[1].s, "-p") || redir)
			return false;
		for (i = 2; i < nw; i++)
			if (w[i].s[0] == '-' || !w[i].s[0])
				return false;
		c->kind = SHELL_COMMAND_MKDIR;
		c->argv = shell_words_to_argv(w + 2, nw - 2, NULL);
		return c->argv != NULL;
	} else if (ops->fixdep && strlen(name) >= 20 && !strcmp(name + strlen(name) - 20, "scripts/basic/fixdep")
		   && (strlen(name) == 20 || name[strlen(name) - 21] == '/') && !access(name, X_OK)) {
		/* fixdep D T CMD > FILE */
		if (nw != 4 || !redir)
			return false;
		c->kind = SHELL_COMMAND_FIXDEP;
		c->argv = shell_words_to_argv(w + 1, 3, NULL);
		c->redir = redir;
		return c->argv != NULL;
	} else if (ops->is_tool(ops->ctx, name)) {
		if (redir)
			return false;
		c->kind = SHELL_COMMAND_TOOL;
		c->argv = shell_words_to_argv(w + nasg, nw - nasg, NULL);
		c->assign = shell_words_to_argv(w, nasg, NULL);
		return c->argv && c->assign;
	} else {
		return false;
	}
	return true;
}

static bool shell_script_parse(struct shell_token_list *v, struct shell_script *sc, const struct shell_callbacks *ops)
{
	int i = 0;

	while (i < v->n) {
		int a = i, p = -1;

		while (i < v->n && v->t[i].kind != SHELL_TOKEN_SEMICOLON) {
			if (v->t[i].kind == SHELL_TOKEN_PIPE) {
				if (p >= 0)
					return false;	/* only two-stage pipes */
				p = i;
			}
			i++;
		}
		if (i == a)
			return false;	/* empty command: a syntax error for sh */
		if (p >= 0) {
			vbuffer out = VBUFFER_STICKY_INIT;
			bool ok = shell_command_parse(v->t + a, p - a, sc, ops, NULL, &out) &&
				  shell_command_parse(v->t + p + 1, i - p - 1, sc, ops, &out, NULL);

			free(out.p);
			if (!ok)
				return false;
		} else if (!shell_command_parse(v->t + a, i - a, sc, ops, NULL, NULL)) {
			return false;
		}
		if (i < v->n)
			i++;	/* the ; (a trailing one is fine) */
	}
	return sc->n > 0;
}

/* ---------------- running ---------------- */

static bool shell_fd_write_all(int fd, const char *p, size_t n)
{
	while (n) {
		ssize_t r = write(fd, p, n);

		if (r < 0 && errno == EINTR)
			continue;
		if (r <= 0)
			return false;
		p += r;
		n -= r;
	}
	return true;
}

static void shell_error_report(int fd, const char *a, const char *b, const char *c)
{
	char line[1024];
	int n = snprintf(line, sizeof(line), "%s%s%s\n", a, b, c);

	if (n > 0)
		(void)shell_fd_write_all(fd, line, n < (int)sizeof(line) ? n : (int)sizeof(line) - 1);
}

/* "<a><b>': <strerror>" as coreutils (EPERM on a directory: unlink's answer on macOS) */
static void shell_rm_error_report(int fd, const char *a, const char *b, int e)
{
	char m[128] = "': ";

	if (e == EPERM && a[0] == 'r')
		e = EISDIR;
	strlcat(m, strerror(e), sizeof m);
	shell_error_report(fd, a, b, m);
}

static bool shell_path_is_directory(int dir, const char *p)
{
	struct stat st;

	return fstatat(dir, p, &st, 0) == 0 && S_ISDIR(st.st_mode);
}

/* mkdir -p: the whole path, then each missing parent from the top (mode 0777 & ~umask) */
static bool shell_directories_create(int dir, const char *path)
{
	char buf[4096];
	size_t n = strlen(path), i;

	if (mkdirat(dir, path, 0777) == 0 || (errno == EEXIST && shell_path_is_directory(dir, path)))
		return true;
	if (errno != ENOENT || n >= sizeof(buf))
		return false;
	memcpy(buf, path, n + 1);
	for (i = 1; i <= n; i++) {
		if (buf[i] != '/' && buf[i])
			continue;
		if (buf[i - 1] == '/')
			continue;	/* a//b */
		buf[i] = 0;
		if (mkdirat(dir, buf, 0777) < 0 && !(errno == EEXIST && shell_path_is_directory(dir, buf)))
			return false;
		buf[i] = path[i];
	}
	return true;
}

static int shell_script_run(const char *cwd, struct shell_script *sc, bool errexit, const int fds[3],
	       const struct shell_callbacks *ops)
{
	int dir = -1, st = 0, i;

	for (i = 0; i < sc->n; i++) {
		struct shell_command *c = &sc->c[i];

		if ((c->kind == SHELL_COMMAND_RM || c->kind == SHELL_COMMAND_MKDIR || (c->redir && c->kind != SHELL_COMMAND_FIXDEP)) && dir < 0 &&
		    (dir = open(cwd, O_RDONLY | O_DIRECTORY | O_CLOEXEC)) < 0) {
			shell_error_report(fds[2], "sh: cannot open ", cwd, "");
			return 2;
		}
		switch (c->kind) {
		case SHELL_COMMAND_NOP:
			st = 0;
			break;
		case SHELL_COMMAND_SET_E:
			errexit = true;
			st = 0;
			break;
		case SHELL_COMMAND_TEXT:
			if (c->redir) {
				int fd = openat(dir, c->redir, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0666);

				if (fd < 0) {
					shell_error_report(fds[2], "sh: cannot create ", c->redir, errno == ENOENT ? ": Directory nonexistent" : ": Permission denied");
					st = 2;
					break;
				}
				st = shell_fd_write_all(fd, c->text, c->textlen) ? 0 : 1;
				if (close(fd) < 0)
					st = 1;
			} else {
				st = shell_fd_write_all(fds[1], c->text, c->textlen) ? 0 : 1;
			}
			break;
		case SHELL_COMMAND_RM:
			st = 0;
			for (char **p = c->argv; *p; p++)
				if (unlinkat(dir, *p, 0) < 0 && errno != ENOENT) {
					shell_rm_error_report(fds[2], "rm: cannot remove '", *p, errno);
					st = 1;
				}
			break;
		case SHELL_COMMAND_MKDIR:
			st = 0;
			for (char **p = c->argv; *p; p++)
				if (!shell_directories_create(dir, *p)) {
					shell_rm_error_report(fds[2], "mkdir: cannot create directory '", *p, errno == EEXIST ? ENOTDIR : errno);
					st = 1;
				}
			break;
		case SHELL_COMMAND_FIXDEP:
			st = ops->fixdep(ops->ctx, c->argv[0], c->argv[1], c->argv[2], c->redir, fds[2]);
			break;
		case SHELL_COMMAND_TOOL:
			st = ops->run_tool(ops->ctx, c->argv, c->assign, fds);
			if (c->xargs && st)
				st = st == 255 ? 124 : st >= 128 ? 125 : st == 127 ? 127 : st == 126 ? 126 : 123;
			break;
		}
		if (st && errexit)
			break;
	}
	if (dir >= 0)
		close(dir);
	return st;
}

int shell_run(const char *cwd, const char *script, bool errexit, const int fds[3],
	    const struct shell_callbacks *ops)
{
	struct shell_token_list v = { 0 };
	struct shell_script sc = { 0 };
	int st = SHELL_DECLINE, i;

	if (shell_tokenize(script, &v) && shell_script_parse(&v, &sc, ops))
		st = shell_script_run(cwd, &sc, errexit, fds, ops);
	for (i = 0; i < sc.n; i++) {
		free(sc.c[i].argv);
		free(sc.c[i].assign);
		free(sc.c[i].text);
	}
	free(sc.c);
	for (i = 0; i < v.n; i++)
		free(v.t[i].s);
	free(v.t);
	return st;
}
