#ifndef INCLUDES_CONFIG_H
#define INCLUDES_CONFIG_H

#include <stdbool.h>
#include <stddef.h>

typedef struct includes_config {
	/* Include search: -I paths (in order), then system */
	char **include_paths;
	size_t include_paths_count;
	size_t include_paths_cap;

	/* Toolkit: -T gcc|msc */
	const char *toolkit;

	/* Triplet: -t x86_64-linux-gnu etc., NULL = auto-detect */
	const char *triplet;

	/* Recursion: -L NUM (depth), 0 = root only, -1 = unlimited */
	int max_depth;

	/* -d: allow duplicate includes in output */
	bool allow_duplicates;

	/* -u: output quoted includes only; -a: angled only; default: both */
	bool quoted_only;
	bool angled_only;

	/* -A: recurse into angled includes (default: only quoted) */
	bool recurse_angled;

	/* -p: preserve "file" / <file> in output */
	bool preserve_quotes;

	/* -c: map headers to corresponding source files */
	bool header_to_source;

	/* -e: with -c, also output source files that appear on the command line */
	bool echo_sources;

	/* -f: canonicalize output paths (absolute, realpath) */
	bool canonicalize;

	/* -n: only output resolved (existing) include paths */
	bool exists_only;

	/* -m: only output missing (unresolved) include names */
	bool missing_only;

	/* -s: sort output lines */
	bool sort_output;

	/* -j: JSON output (single array of path strings) */
	bool json_output;

	/* -g: Graphviz .dot output (include graph) */
	bool graphviz_output;

	/* -P: parallel parsing (multiple root sources in threads) */
	bool parallel;

	/* -w: enable parser warning messages (e.g. missing #endif) */
	bool warnings;

	/* Logging: -q decrements, -v increments; 0 = default (WARN) */
	int verbosity_delta;

	/* Positional: source files to process */
	char **sources;
	size_t sources_count;
	size_t sources_cap;
} includes_config_t;

void config_init(includes_config_t *c);
void config_free(includes_config_t *c);

/**
 * Parse argv, fill config, set *first_arg to index of first non-option.
 * Returns 0 on success, -1 on parse error (e.g. unknown option), 1 for --help.
 */
int config_parse_options(includes_config_t *c, int argc, char **argv, int *first_arg);

void config_print_usage(void);

#endif /* INCLUDES_CONFIG_H */
