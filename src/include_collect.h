#ifndef INCLUDES_INCLUDE_COLLECT_H
#define INCLUDES_INCLUDE_COLLECT_H

#include "config.h"
#include "file_loader.h"
#include "macro.h"
#include "pp_parse.h"
#include <stdbool.h>
#include <stddef.h>

typedef struct include_collect {
	const includes_config_t *config;
	macro_table_t *macros;
	char **visited;      /* canonical paths we've parsed */
	size_t visited_count;
	size_t visited_cap;
	char **printed;      /* paths we've already output (for dedup) */
	size_t printed_count;
	size_t printed_cap;
	char *root_dir;      /* directory of first root source (for -c join names) */
	int depth;
	const char *current_path;  /* file being parsed (for resolve) */
	int parse_error;           /* 1 if pp_parse failed (unmatched #if/#endif) */
	char **output_lines;       /* when sort_output/json/graphviz: buffer until finish */
	size_t output_count;
	size_t output_cap;
	char **edge_from;          /* for graphviz */
	char **edge_to;
	size_t edge_count;
	size_t edge_cap;
	void (*output_cb)(void *ctx, const char *path);  /* library: called for each path */
	void *output_ctx;
} include_collect_t;

void include_collect_init(include_collect_t *c, const includes_config_t *config, macro_table_t *macros);
void include_collect_set_output_cb(include_collect_t *c, void (*cb)(void *ctx, const char *path), void *ctx);
void include_collect_finish(include_collect_t *c);

/**
 * Process one source file: parse, output includes, recurse as per config.
 */
bool include_collect_file(include_collect_t *c, const char *path);

#endif /* INCLUDES_INCLUDE_COLLECT_H */
