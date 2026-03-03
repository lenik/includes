#define _POSIX_C_SOURCE 200809L
#include "pp_parse.h"
#include "config.h"
#include "util/logging.h"
#include <stdlib.h>
#include <string.h>

/** Copy the buffer line containing ptr into out (max out_size-1 chars), trim \\r. */
static void get_directive_line(const char *buf, size_t len, const char *ptr,
                               char *out, size_t out_size) {
	size_t line_start, line_end;
	if (!buf || !ptr || ptr < buf || out_size == 0) { if (out_size) out[0] = '\0'; return; }
	line_start = (size_t)(ptr - buf);
	while (line_start > 0 && buf[line_start - 1] != '\n')
		line_start--;
	line_end = line_start;
	while (line_end < len && buf[line_end] != '\n')
		line_end++;
	if (line_end - line_start >= out_size)
		line_end = line_start + out_size - 1;
	memcpy(out, buf + line_start, line_end - line_start);
	out[line_end - line_start] = '\0';
	if (line_end > line_start && out[line_end - line_start - 1] == '\r')
		out[line_end - line_start - 1] = '\0';
}

static void skip_to_newline(pp_lexer_t *lx) {
	while (pp_lexer_type(lx) != PP_TOK_NEWLINE && pp_lexer_type(lx) != PP_TOK_EOF)
		pp_lexer_next(lx);
	if (pp_lexer_type(lx) == PP_TOK_NEWLINE)
		pp_lexer_next(lx);
}

static void parse_define(pp_lexer_t *lx, macro_table_t *macros) {
	char name[256], value[1024];
	value[0] = '\0';
	if (pp_lexer_type(lx) != PP_TOK_IDENT) { skip_to_newline(lx); return; }
	pp_token_str(pp_lexer_current(lx), name, sizeof(name));
	pp_lexer_next(lx);
	size_t vlen = 0;
	while (pp_lexer_type(lx) != PP_TOK_NEWLINE && pp_lexer_type(lx) != PP_TOK_EOF) {
		const pp_token_t *t = pp_lexer_current(lx);
		size_t n = t->len;
		if (vlen + n + 1 < sizeof(value)) {
			memcpy(value + vlen, t->start, n);
			vlen += n;
			value[vlen] = ' ';
			vlen++;
		}
		pp_lexer_next(lx);
	}
	if (vlen > 0 && value[vlen - 1] == ' ') vlen--;
	value[vlen] = '\0';
	/* trim trailing space */
	while (vlen > 0 && value[vlen - 1] == ' ') value[--vlen] = '\0';
	macro_define(macros, name, value[0] ? value : NULL);
	if (pp_lexer_type(lx) == PP_TOK_NEWLINE) pp_lexer_next(lx);
}

static void parse_undef(pp_lexer_t *lx, macro_table_t *macros) {
	char name[256];
	if (pp_lexer_type(lx) != PP_TOK_IDENT) { skip_to_newline(lx); return; }
	pp_token_str(pp_lexer_current(lx), name, sizeof(name));
	pp_lexer_next(lx);
	macro_undef(macros, name);
	skip_to_newline(lx);
}

static void parse_include(pp_lexer_t *lx, bool active, pp_include_cb cb, void *ctx) {
	char path[1024];
	bool is_angled = false;
	if (pp_lexer_type(lx) == PP_TOK_STRING_QUOTED) {
		const pp_token_t *t = pp_lexer_current(lx);
		/* strip quotes */
		if (t->len >= 2) {
			size_t n = t->len - 2;
			if (n >= sizeof(path)) n = sizeof(path) - 1;
			memcpy(path, t->start + 1, n);
			path[n] = '\0';
		} else path[0] = '\0';
		pp_lexer_next(lx);
	} else if (pp_lexer_type(lx) == PP_TOK_STRING_ANGLED) {
		const pp_token_t *t = pp_lexer_current(lx);
		if (t->len >= 2) {
			size_t n = t->len - 2;
			if (n >= sizeof(path)) n = sizeof(path) - 1;
			memcpy(path, t->start + 1, n);
			path[n] = '\0';
		} else path[0] = '\0';
		is_angled = true;
		pp_lexer_next(lx);
	} else {
		skip_to_newline(lx);
		return;
	}
	if (active && cb) cb(ctx, path, is_angled);
	skip_to_newline(lx);
}

bool pp_parse(const char *buf, size_t len, const char *current_file,
              const struct includes_config *config,
              macro_table_t *macros, pp_include_cb cb, void *ctx,
              unsigned *error_line) {
	pp_cond_stack_t cond;
	char directive_buf[256];
	const char *p = buf;
	const char *end = buf + len;
	unsigned line = 1;
	if (error_line) *error_line = 0;

	pp_cond_init(&cond);

	while (p < end) {
		const char *line_start = p;
		const char *line_end = memchr(p, '\n', (size_t)(end - p));
		if (!line_end) line_end = end;
		else line_end++; /* include newline */

		/* Find first non-space/tab character on this line. */
		const char *s = line_start;
		while (s < line_end && (*s == ' ' || *s == '\t')) s++;
		if (s >= line_end || *s != '#') {
			/* Not a preprocessor directive; ignore entire line as body text. */
			p = line_end;
			line++;
			continue;
		}

		/* Initialize a lexer over just this directive line, starting at '#'. */
		pp_lexer_t lx;
		pp_lexer_init(&lx, s, (size_t)(line_end - s));

		/* First token must be '#'. */
		if (pp_lexer_type(&lx) != PP_TOK_HASH) {
			p = line_end;
			line++;
			continue;
		}
		pp_lexer_next(&lx); /* consume # */

		pp_tok_type_t dir = pp_lexer_type(&lx);
		if (dir == PP_TOK_EOF || dir == PP_TOK_NEWLINE) {
			p = line_end;
			line++;
			continue;
		}
		/* Capture line/directive for #if/#ifdef/#ifndef before advancing.
		 * Token pointers still refer into the original buffer. */
		const pp_token_t *dir_tok = pp_lexer_current(&lx);
		unsigned dir_line = line;
		get_directive_line(buf, len, dir_tok->start, directive_buf, sizeof(directive_buf));
		pp_lexer_next(&lx);

		switch (dir) {
		case PP_TOK_INCLUDE:
			parse_include(&lx, pp_cond_active(&cond), cb, ctx);
			break;
		case PP_TOK_DEFINE:
			parse_define(&lx, macros);
			break;
		case PP_TOK_UNDEF:
			parse_undef(&lx, macros);
			break;
		case PP_TOK_IF: {
			struct pp_expr_result r = pp_expr_eval(&lx, macros);
			pp_cond_push(&cond, r.ok && r.value != 0, dir_line, directive_buf);
			skip_to_newline(&lx);
			break;
		}
		case PP_TOK_IFDEF: {
			char name[256];
			bool def = false;
			if (pp_lexer_type(&lx) == PP_TOK_IDENT) {
				pp_token_str(pp_lexer_current(&lx), name, sizeof(name));
				def = macro_defined(macros, name);
			}
			pp_cond_push(&cond, def, dir_line, directive_buf);
			skip_to_newline(&lx);
			break;
		}
		case PP_TOK_IFNDEF: {
			char name[256];
			bool def = false;
			if (pp_lexer_type(&lx) == PP_TOK_IDENT) {
				pp_token_str(pp_lexer_current(&lx), name, sizeof(name));
				def = macro_defined(macros, name);
			}
			pp_cond_push(&cond, !def, dir_line, directive_buf);
			skip_to_newline(&lx);
			break;
		}
		case PP_TOK_ELIF: {
			struct pp_expr_result r = pp_expr_eval(&lx, macros);
			pp_cond_elif(&cond, r.ok && r.value != 0);
			skip_to_newline(&lx);
			break;
		}
		case PP_TOK_ELSE:
			pp_cond_else(&cond);
			skip_to_newline(&lx);
			break;
		case PP_TOK_ENDIF: {
			if (!pp_cond_pop(&cond)) {
				if (error_line) *error_line = line;
				if (config && config->warnings)
					logwarn_fmt("Unmatched #endif at line %u", line);
				return false;
			}
			skip_to_newline(&lx);
			break;
		}
		default:
			skip_to_newline(&lx);
			break;
		}

		/* Move to next physical line in the original buffer. */
		p = line_end;
		line++;
	}
	if (cond.depth != 0 || cond.overflow != 0) {
		if (cond.depth != 0) {
			size_t top = cond.depth - 1;
			if (error_line) *error_line = cond.line[top];
			if (config && config->warnings) {
				logwarn_fmt("Missing #endif for %s:%u: %s",
					current_file && current_file[0] ? current_file : "(unknown)",
					cond.line[top],
					cond.directive[top] && cond.directive[top][0] ? cond.directive[top] : "#if / #ifdef / #ifndef");
			}
			for (; cond.depth > 0; cond.depth--) {
				free(cond.directive[cond.depth - 1]);
				cond.directive[cond.depth - 1] = NULL;
			}
		} else {
			if (config && config->warnings) {
				logwarn_fmt("Missing #endif (nesting exceeded %u levels)",
					(unsigned)PP_COND_MAX_DEPTH);
			}
		}
		return false;
	}
	return true;
}
