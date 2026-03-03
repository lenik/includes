#define _POSIX_C_SOURCE 200809L
#include "pp_expr.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static long eval_or(pp_lexer_t *lx, macro_table_t *macros);
static long eval_and(pp_lexer_t *lx, macro_table_t *macros);
static long eval_eq(pp_lexer_t *lx, macro_table_t *macros);
static long eval_rel(pp_lexer_t *lx, macro_table_t *macros);
static long eval_add(pp_lexer_t *lx, macro_table_t *macros);
static long eval_mul(pp_lexer_t *lx, macro_table_t *macros);
static long eval_unary(pp_lexer_t *lx, macro_table_t *macros);
static long eval_primary(pp_lexer_t *lx, macro_table_t *macros);

static long parse_number(const pp_token_t *t) {
	char buf[32];
	size_t n = pp_token_str(t, buf, sizeof(buf));
	if (n == 0) return 0;
	return strtol(buf, NULL, 0);
}

static long eval_primary(pp_lexer_t *lx, macro_table_t *macros) {
	const pp_token_t *t = pp_lexer_current(lx);
	pp_tok_type_t ty = pp_lexer_type(lx);
	if (ty == PP_TOK_NUMBER) {
		long v = parse_number(t);
		pp_lexer_next(lx);
		return v;
	}
	if (ty == PP_TOK_LPAREN) {
		pp_lexer_next(lx);
		long v = eval_or(lx, macros);
		if (pp_lexer_type(lx) == PP_TOK_RPAREN)
			pp_lexer_next(lx);
		return v;
	}
	if (ty == PP_TOK_DEFINED) {
		pp_lexer_next(lx);
		if (pp_lexer_type(lx) == PP_TOK_LPAREN)
			pp_lexer_next(lx);
		if (pp_lexer_type(lx) != PP_TOK_IDENT) return 0;
		t = pp_lexer_current(lx);
		char name[256];
		pp_token_str(t, name, sizeof(name));
		pp_lexer_next(lx);
		if (pp_lexer_type(lx) == PP_TOK_RPAREN)
			pp_lexer_next(lx);
		return macro_defined(macros, name) ? 1L : 0L;
	}
	if (ty == PP_TOK_IDENT) {
		char name[256];
		pp_token_str(t, name, sizeof(name));
		pp_lexer_next(lx);
		const char *val = macro_lookup(macros, name);
		if (val && val[0]) return strtol(val, NULL, 0);
		return 0;
	}
	pp_lexer_next(lx);
	return 0;
}

static long eval_unary(pp_lexer_t *lx, macro_table_t *macros) {
	if (pp_lexer_type(lx) == PP_TOK_NOT) {
		pp_lexer_next(lx);
		return eval_unary(lx, macros) ? 0 : 1;
	}
	if (pp_lexer_type(lx) == PP_TOK_MINUS) {
		pp_lexer_next(lx);
		return -eval_unary(lx, macros);
	}
	if (pp_lexer_type(lx) == PP_TOK_PLUS) {
		pp_lexer_next(lx);
		return eval_unary(lx, macros);
	}
	return eval_primary(lx, macros);
}

static long eval_mul(pp_lexer_t *lx, macro_table_t *macros) {
	long v = eval_unary(lx, macros);
	for (;;) {
		pp_tok_type_t ty = pp_lexer_type(lx);
		if (ty == PP_TOK_STAR) {
			pp_lexer_next(lx);
			v *= eval_unary(lx, macros);
		} else if (ty == PP_TOK_SLASH) {
			pp_lexer_next(lx);
			long r = eval_unary(lx, macros);
			if (r != 0) v /= r;
		} else if (ty == PP_TOK_PERCENT) {
			pp_lexer_next(lx);
			long r = eval_unary(lx, macros);
			if (r != 0) v %= r;
		} else break;
	}
	return v;
}

static long eval_add(pp_lexer_t *lx, macro_table_t *macros) {
	long v = eval_mul(lx, macros);
	for (;;) {
		pp_tok_type_t ty = pp_lexer_type(lx);
		if (ty == PP_TOK_PLUS) {
			pp_lexer_next(lx);
			v += eval_mul(lx, macros);
		} else if (ty == PP_TOK_MINUS) {
			pp_lexer_next(lx);
			v -= eval_mul(lx, macros);
		} else break;
	}
	return v;
}

static long eval_rel(pp_lexer_t *lx, macro_table_t *macros) {
	long v = eval_add(lx, macros);
	pp_tok_type_t ty = pp_lexer_type(lx);
	if (ty == PP_TOK_LT) { pp_lexer_next(lx); return (v < eval_add(lx, macros)) ? 1 : 0; }
	if (ty == PP_TOK_GT) { pp_lexer_next(lx); return (v > eval_add(lx, macros)) ? 1 : 0; }
	if (ty == PP_TOK_LE) { pp_lexer_next(lx); return (v <= eval_add(lx, macros)) ? 1 : 0; }
	if (ty == PP_TOK_GE) { pp_lexer_next(lx); return (v >= eval_add(lx, macros)) ? 1 : 0; }
	return v;
}

static long eval_eq(pp_lexer_t *lx, macro_table_t *macros) {
	long v = eval_rel(lx, macros);
	pp_tok_type_t ty = pp_lexer_type(lx);
	if (ty == PP_TOK_EQ) { pp_lexer_next(lx); return (v == eval_rel(lx, macros)) ? 1 : 0; }
	if (ty == PP_TOK_NE) { pp_lexer_next(lx); return (v != eval_rel(lx, macros)) ? 1 : 0; }
	return v;
}

static long eval_and(pp_lexer_t *lx, macro_table_t *macros) {
	long v = eval_eq(lx, macros);
	while (pp_lexer_type(lx) == PP_TOK_AND) {
		pp_lexer_next(lx);
		v = (v && eval_eq(lx, macros)) ? 1 : 0;
	}
	return v;
}

static long eval_or(pp_lexer_t *lx, macro_table_t *macros) {
	long v = eval_and(lx, macros);
	while (pp_lexer_type(lx) == PP_TOK_OR) {
		pp_lexer_next(lx);
		v = (v || eval_and(lx, macros)) ? 1 : 0;
	}
	return v;
}

struct pp_expr_result pp_expr_eval(pp_lexer_t *lx, macro_table_t *macros) {
	struct pp_expr_result r = { 0, true };
	r.value = eval_or(lx, macros);
	return r;
}
