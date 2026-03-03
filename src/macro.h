#ifndef INCLUDES_MACRO_H
#define INCLUDES_MACRO_H

#include <stddef.h>
#include <stdbool.h>

typedef struct macro {
	char *name;
	char *value;
} macro_t;

typedef struct macro_table macro_table_t;

macro_table_t *macro_table_create(void);
macro_table_t *macro_table_clone(const macro_table_t *src);
void macro_table_destroy(macro_table_t *t);

void macro_define(macro_table_t *t, const char *name, const char *value);
void macro_undef(macro_table_t *t, const char *name);
const char *macro_lookup(macro_table_t *t, const char *name);
bool macro_defined(macro_table_t *t, const char *name);

#endif /* INCLUDES_MACRO_H */
