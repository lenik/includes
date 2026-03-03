#ifndef INCLUDES_PP_LEXER_H
#define INCLUDES_PP_LEXER_H

#include <stddef.h>
#include <stdbool.h>

typedef enum pp_tok_type {
	PP_TOK_EOF,
	PP_TOK_NEWLINE,
	PP_TOK_HASH,
	PP_TOK_IDENT,
	PP_TOK_NUMBER,
	PP_TOK_STRING_QUOTED,
	PP_TOK_STRING_ANGLED,
	PP_TOK_LPAREN,
	PP_TOK_RPAREN,
	PP_TOK_PLUS,
	PP_TOK_MINUS,
	PP_TOK_STAR,
	PP_TOK_SLASH,
	PP_TOK_PERCENT,
	PP_TOK_EQ,      /* == */
	PP_TOK_NE,      /* != */
	PP_TOK_LT,
	PP_TOK_GT,
	PP_TOK_LE,      /* <= */
	PP_TOK_GE,      /* >= */
	PP_TOK_AND,     /* && */
	PP_TOK_OR,      /* || */
	PP_TOK_NOT,     /* ! */
	PP_TOK_DEFINED,
	PP_TOK_INCLUDE,
	PP_TOK_IF,
	PP_TOK_IFDEF,
	PP_TOK_IFNDEF,
	PP_TOK_ELIF,
	PP_TOK_ELSE,
	PP_TOK_ENDIF,
	PP_TOK_DEFINE,
	PP_TOK_UNDEF,
} pp_tok_type_t;

typedef struct pp_token {
	pp_tok_type_t type;
	const char *start;  /* points into lexer buffer */
	size_t len;
	unsigned line;
} pp_token_t;

typedef struct pp_lexer {
	const char *buf;
	size_t len;
	size_t pos;
	unsigned line;
	pp_token_t cur;
	bool cur_valid;
} pp_lexer_t;

void pp_lexer_init(pp_lexer_t *lx, const char *buf, size_t len);
void pp_lexer_next(pp_lexer_t *lx);
const pp_token_t *pp_lexer_current(pp_lexer_t *lx);
pp_tok_type_t pp_lexer_type(pp_lexer_t *lx);

/* Copy token text to a NUL-terminated buffer; returns required length if buf is NULL. */
size_t pp_token_str(const pp_token_t *t, char *buf, size_t buf_size);

#endif /* INCLUDES_PP_LEXER_H */
