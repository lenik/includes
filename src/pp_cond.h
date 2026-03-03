#ifndef INCLUDES_PP_COND_H
#define INCLUDES_PP_COND_H

#include <stdbool.h>
#include <stddef.h>

#define PP_COND_MAX_DEPTH 256

typedef struct pp_cond_stack {
	bool active[PP_COND_MAX_DEPTH];  /* current branch taken */
	bool seen_else[PP_COND_MAX_DEPTH];
	unsigned line[PP_COND_MAX_DEPTH];   /* line number of #if/#ifdef/#ifndef */
	char *directive[PP_COND_MAX_DEPTH]; /* full directive line (owned, freed on pop) */
	size_t depth;
	size_t overflow;  /* when depth >= MAX, count extra pushes so #endif still balances */
} pp_cond_stack_t;

void pp_cond_init(pp_cond_stack_t *s);
bool pp_cond_active(pp_cond_stack_t *s);
/** Push with optional location for error reporting; directive is strdup'd. */
void pp_cond_push(pp_cond_stack_t *s, bool taken, unsigned line_no, const char *directive_line);
bool pp_cond_pop(pp_cond_stack_t *s);
bool pp_cond_else(pp_cond_stack_t *s);
bool pp_cond_elif(pp_cond_stack_t *s, bool expr_true);

#endif /* INCLUDES_PP_COND_H */
