#ifndef INCLUDES_PP_PARSE_H
#define INCLUDES_PP_PARSE_H

#include "pp_lexer.h"
#include "macro.h"
#include "pp_cond.h"
#include "pp_expr.h"
#include <stddef.h>
#include <stdbool.h>

struct includes_config;

typedef void (*pp_include_cb)(void *ctx, const char *path, bool is_angled);

/**
 * Parse buffer as preprocessor input; evaluate conditionals and macros.
 * For each #include in an active branch, calls cb(ctx, path, is_angled).
 * path is the raw include name (e.g. "foo.h" or <stdio.h>); resolver runs elsewhere.
 * current_file is used in error messages (e.g. missing #endif).
 * On parse error (unmatched #if/#endif), returns false and optionally sets *error_line.
 */
bool pp_parse(const char *buf, size_t len, const char *current_file,
              const struct includes_config *config,
              macro_table_t *macros, pp_include_cb cb, void *ctx,
              unsigned *error_line);

#endif /* INCLUDES_PP_PARSE_H */
