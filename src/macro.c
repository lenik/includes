#define _POSIX_C_SOURCE 200809L
#include "macro.h"
#include <stdlib.h>
#include <string.h>

#define HASH_INIT 64

struct macro_table {
	macro_t *entries;
	size_t count;
	size_t cap;
};

macro_table_t *macro_table_create(void) {
	macro_table_t *t = (macro_table_t *)malloc(sizeof(*t));
	if (!t) return NULL;
	t->entries = NULL;
	t->count = 0;
	t->cap = 0;
	return t;
}

macro_table_t *macro_table_clone(const macro_table_t *src) {
	macro_table_t *t;
	size_t i;
	if (!src) return NULL;
	t = macro_table_create();
	if (!t) return NULL;
	if (src->count == 0) return t;
	t->entries = (macro_t *)malloc(src->cap * sizeof(macro_t));
	if (!t->entries) { free(t); return NULL; }
	t->cap = src->cap;
	t->count = src->count;
	for (i = 0; i < src->count; i++) {
		t->entries[i].name = strdup(src->entries[i].name);
		t->entries[i].value = src->entries[i].value ? strdup(src->entries[i].value) : strdup("");
		if (!t->entries[i].name || !t->entries[i].value) {
			size_t j;
			free(t->entries[i].name);
			free(t->entries[i].value);
			for (j = 0; j < i; j++) { free(t->entries[j].name); free(t->entries[j].value); }
			free(t->entries);
			free(t);
			return NULL;
		}
	}
	return t;
}

void macro_table_destroy(macro_table_t *t) {
	size_t i;
	if (!t) return;
	for (i = 0; i < t->count; i++) {
		free(t->entries[i].name);
		free(t->entries[i].value);
	}
	free(t->entries);
	free(t);
}

static macro_t *find_slot(macro_table_t *t, const char *name) {
	size_t i;
	for (i = 0; i < t->count; i++) {
		if (strcmp(t->entries[i].name, name) == 0)
			return &t->entries[i];
	}
	return NULL;
}

void macro_define(macro_table_t *t, const char *name, const char *value) {
	macro_t *m = find_slot(t, name);
	if (m) {
		free(m->value);
		m->value = value ? strdup(value) : strdup("");
		return;
	}
	if (t->count >= t->cap) {
		size_t new_cap = t->cap ? t->cap * 2 : HASH_INIT;
		macro_t *n = (macro_t *)realloc(t->entries, new_cap * sizeof(macro_t));
		if (!n) return;
		t->entries = n;
		t->cap = new_cap;
	}
	t->entries[t->count].name = strdup(name);
	t->entries[t->count].value = value ? strdup(value) : strdup("");
	t->count++;
}

void macro_undef(macro_table_t *t, const char *name) {
	size_t i;
	for (i = 0; i < t->count; i++) {
		if (strcmp(t->entries[i].name, name) == 0) {
			free(t->entries[i].name);
			free(t->entries[i].value);
			t->entries[i] = t->entries[t->count - 1];
			t->count--;
			return;
		}
	}
}

const char *macro_lookup(macro_table_t *t, const char *name) {
	macro_t *m = find_slot(t, name);
	return m ? m->value : NULL;
}

bool macro_defined(macro_table_t *t, const char *name) {
	return find_slot(t, name) != NULL;
}
