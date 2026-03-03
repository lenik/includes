#ifndef INCLUDES_PP_EXPR_H
#define INCLUDES_PP_EXPR_H

#include "pp_lexer.h"
#include "macro.h"
#include <stdbool.h>

struct pp_expr_result {
	long value;
	bool ok;
};

/**
 * Evaluate preprocessor expression from current lexer position.
 * Consumes tokens until the expression ends (e.g. newline).
 * identifiers are looked up in macro table; undefined = 0.
 */
struct pp_expr_result pp_expr_eval(pp_lexer_t *lx, macro_table_t *macros);

#endif /* INCLUDES_PP_EXPR_H */
