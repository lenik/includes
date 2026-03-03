#define _POSIX_C_SOURCE 200809L
#include "pp_cond.h"
#include <stdlib.h>
#include <string.h>

void pp_cond_init(pp_cond_stack_t *s) {
	size_t i;
	s->depth = 0;
	s->overflow = 0;
	for (i = 0; i < PP_COND_MAX_DEPTH; i++)
		s->directive[i] = NULL;
}

bool pp_cond_active(pp_cond_stack_t *s) {
	size_t i;
	for (i = 0; i < s->depth; i++)
		if (!s->active[i]) return false;
	return true;
}

void pp_cond_push(pp_cond_stack_t *s, bool taken, unsigned line_no, const char *directive_line) {
	if (s->depth >= PP_COND_MAX_DEPTH) {
		s->overflow++;
		return;
	}
	s->active[s->depth] = taken;
	s->seen_else[s->depth] = false;
	s->line[s->depth] = line_no;
	s->directive[s->depth] = (directive_line && *directive_line) ? strdup(directive_line) : NULL;
	s->depth++;
}

bool pp_cond_pop(pp_cond_stack_t *s) {
	if (s->overflow > 0) {
		s->overflow--;
		return true;
	}
	if (s->depth == 0) return false;
	s->depth--;
	free(s->directive[s->depth]);
	s->directive[s->depth] = NULL;
	return true;
}

bool pp_cond_else(pp_cond_stack_t *s) {
	if (s->depth == 0) return false;
	if (s->seen_else[s->depth - 1]) return false;
	s->seen_else[s->depth - 1] = true;
	s->active[s->depth - 1] = !s->active[s->depth - 1];
	return true;
}

bool pp_cond_elif(pp_cond_stack_t *s, bool expr_true) {
	if (s->depth == 0) return false;
	if (s->seen_else[s->depth - 1]) return false;
	if (s->active[s->depth - 1]) {
		s->active[s->depth - 1] = false;
		return true;
	}
	s->active[s->depth - 1] = expr_true;
	return true;
}
