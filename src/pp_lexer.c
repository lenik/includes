#define _POSIX_C_SOURCE 200809L
#include "pp_lexer.h"
#include <ctype.h>
#include <string.h>
#include <stdlib.h>

static void set_tok(pp_lexer_t *lx, pp_tok_type_t type, const char *start, size_t len) {
	lx->cur.type = type;
	lx->cur.start = start;
	lx->cur.len = len;
	lx->cur.line = lx->line;
	lx->cur_valid = true;
}

static bool at_eof(pp_lexer_t *lx) {
	return lx->pos >= lx->len;
}

static char peek(pp_lexer_t *lx) {
	return at_eof(lx) ? '\0' : lx->buf[lx->pos];
}

static char advance(pp_lexer_t *lx) {
	if (at_eof(lx)) return '\0';
	char c = lx->buf[lx->pos++];
	if (c == '\n') lx->line++;
	return c;
}

/* Skip line continuation: \ followed by optional whitespace and newline. */
static void skip_line_continuation(pp_lexer_t *lx) {
	if (peek(lx) != '\\') return;
	lx->pos++;
	while (!at_eof(lx)) {
		char c = lx->buf[lx->pos];
		if (c == '\n') {
			lx->pos++;
			lx->line++;
			return;
		}
		if (c != ' ' && c != '\t') return;
		lx->pos++;
	}
}

/* Skip // comment to end of line. */
static void skip_line_comment(pp_lexer_t *lx) {
	while (!at_eof(lx) && lx->buf[lx->pos] != '\n')
		lx->pos++;
}

/* Skip block comment; return true if skipped. */
static bool skip_block_comment(pp_lexer_t *lx) {
	if (lx->pos + 1 < lx->len && lx->buf[lx->pos] == '/' && lx->buf[lx->pos + 1] == '*') {
		lx->pos += 2;
		while (lx->pos + 1 < lx->len) {
			if (lx->buf[lx->pos] == '*' && lx->buf[lx->pos + 1] == '/') {
				lx->pos += 2;
				return true;
			}
			if (lx->buf[lx->pos] == '\n') lx->line++;
			lx->pos++;
		}
		lx->pos = lx->len;
		return true;
	}
	return false;
}

static bool is_ident_start(char c) {
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static bool is_ident_char(char c) {
	return is_ident_start(c) || (c >= '0' && c <= '9');
}

static void lex_ident_or_keyword(pp_lexer_t *lx) {
	const char *start = lx->buf + lx->pos;
	advance(lx);
	while (!at_eof(lx) && is_ident_char(peek(lx)))
		advance(lx);
	size_t len = (size_t)(lx->buf + lx->pos - start);

	static const struct { const char *s; pp_tok_type_t t; } kw[] = {
		{"defined", PP_TOK_DEFINED},
		{"include", PP_TOK_INCLUDE},
		{"if", PP_TOK_IF},
		{"ifdef", PP_TOK_IFDEF},
		{"ifndef", PP_TOK_IFNDEF},
		{"elif", PP_TOK_ELIF},
		{"else", PP_TOK_ELSE},
		{"endif", PP_TOK_ENDIF},
		{"define", PP_TOK_DEFINE},
		{"undef", PP_TOK_UNDEF},
	};
	for (size_t i = 0; i < sizeof(kw)/sizeof(kw[0]); i++) {
		if (len == strlen(kw[i].s) && memcmp(start, kw[i].s, len) == 0) {
			set_tok(lx, kw[i].t, start, len);
			return;
		}
	}
	set_tok(lx, PP_TOK_IDENT, start, len);
}

static void lex_number(pp_lexer_t *lx) {
	const char *start = lx->buf + lx->pos;
	while (!at_eof(lx) && isdigit((unsigned char)peek(lx)))
		advance(lx);
	set_tok(lx, PP_TOK_NUMBER, start, (size_t)(lx->buf + lx->pos - start));
}

static void lex_string_quoted(pp_lexer_t *lx) {
	const char *start = lx->buf + lx->pos;
	advance(lx); /* " */
	while (!at_eof(lx)) {
		char c = advance(lx);
		if (c == '"') break;
		if (c == '\\' && !at_eof(lx)) advance(lx);
		if (c == '\n') break; /* unterminated */
	}
	set_tok(lx, PP_TOK_STRING_QUOTED, start, (size_t)(lx->buf + lx->pos - start));
}

static void lex_string_angled(pp_lexer_t *lx) {
	const char *start = lx->buf + lx->pos;
	advance(lx); /* < */
	while (!at_eof(lx) && peek(lx) != '>')
		advance(lx);
	if (!at_eof(lx)) advance(lx); /* > */
	set_tok(lx, PP_TOK_STRING_ANGLED, start, (size_t)(lx->buf + lx->pos - start));
}

void pp_lexer_init(pp_lexer_t *lx, const char *buf, size_t len) {
	lx->buf = buf;
	lx->len = len;
	lx->pos = 0;
	lx->line = 1;
	lx->cur_valid = false;
}

void pp_lexer_next(pp_lexer_t *lx) {
	for (;;) {
		while (!at_eof(lx)) {
			skip_line_continuation(lx);
			if (at_eof(lx)) break;
			char c = peek(lx);
			if (c == ' ' || c == '\t') { advance(lx); continue; }
			if (c == '/' && lx->pos + 1 < lx->len) {
				if (lx->buf[lx->pos + 1] == '/') {
					skip_line_comment(lx);
					continue;
				}
				if (lx->buf[lx->pos + 1] == '*') {
					skip_block_comment(lx);
					continue;
				}
			}
			break;
		}
		if (at_eof(lx)) {
			set_tok(lx, PP_TOK_EOF, lx->buf + lx->len, 0);
			return;
		}

		const char *start = lx->buf + lx->pos;
		char c = advance(lx);

		if (c == '\n') {
			set_tok(lx, PP_TOK_NEWLINE, start, 1);
			return;
		}
		if (c == '#') {
			set_tok(lx, PP_TOK_HASH, start, 1);
			return;
		}
		if (is_ident_start(c)) {
			lx->pos--; lx->line -= (c == '\n' ? 1 : 0);
			lex_ident_or_keyword(lx);
			return;
		}
		if (isdigit((unsigned char)c)) {
			lx->pos--;
			lex_number(lx);
			return;
		}
		if (c == '"') {
			lx->pos--;
			lex_string_quoted(lx);
			return;
		}
		if (c == '<') {
			if (!at_eof(lx) && peek(lx) == '=') { advance(lx); set_tok(lx, PP_TOK_LE, start, 2); return; }
			if (lx->pos < lx->len && (is_ident_char(peek(lx)) || peek(lx) == '/' || peek(lx) == '.')) {
				lx->pos--;
				lex_string_angled(lx);
			} else {
				set_tok(lx, PP_TOK_LT, start, 1);
			}
			return;
		}
		if (c == '>') {
			if (!at_eof(lx) && peek(lx) == '=') { advance(lx); set_tok(lx, PP_TOK_GE, start, 2); return; }
			set_tok(lx, PP_TOK_GT, start, 1);
			return;
		}
		if (c == '(') { set_tok(lx, PP_TOK_LPAREN, start, 1); return; }
		if (c == ')') { set_tok(lx, PP_TOK_RPAREN, start, 1); return; }
		if (c == '+') { set_tok(lx, PP_TOK_PLUS, start, 1); return; }
		if (c == '-') { set_tok(lx, PP_TOK_MINUS, start, 1); return; }
		if (c == '*') { set_tok(lx, PP_TOK_STAR, start, 1); return; }
		if (c == '/') { set_tok(lx, PP_TOK_SLASH, start, 1); return; }
		if (c == '%') { set_tok(lx, PP_TOK_PERCENT, start, 1); return; }
		if (c == '!') {
			if (!at_eof(lx) && peek(lx) == '=') { advance(lx); set_tok(lx, PP_TOK_NE, start, 2); }
			else set_tok(lx, PP_TOK_NOT, start, 1);
			return;
		}
		if (c == '=') {
			/* Support '==' in expressions; ignore a lone '=' as normal
			 * source text so it does not terminate the token stream. */
			if (!at_eof(lx) && peek(lx) == '=') {
				advance(lx);
				set_tok(lx, PP_TOK_EQ, start, 2);
				return;
			}
			continue;
		}
		if (c == '&' && !at_eof(lx) && peek(lx) == '&') { advance(lx); set_tok(lx, PP_TOK_AND, start, 2); return; }
		if (c == '|' && !at_eof(lx) && peek(lx) == '|') { advance(lx); set_tok(lx, PP_TOK_OR, start, 2); return; }

		/* Unknown character: ignore it and continue scanning. */
		continue;
	}
}

const pp_token_t *pp_lexer_current(pp_lexer_t *lx) {
	if (!lx->cur_valid)
		pp_lexer_next(lx);
	return lx->cur_valid ? &lx->cur : NULL;
}

pp_tok_type_t pp_lexer_type(pp_lexer_t *lx) {
	const pp_token_t *t = pp_lexer_current(lx);
	return t ? t->type : PP_TOK_EOF;
}

size_t pp_token_str(const pp_token_t *t, char *buf, size_t buf_size) {
	if (!t || t->len == 0) { if (buf && buf_size) buf[0] = '\0'; return 0; }
	if (buf && buf_size > 0) {
		size_t n = t->len < buf_size - 1 ? t->len : buf_size - 1;
		memcpy(buf, t->start, n);
		buf[n] = '\0';
	}
	return t->len;
}
