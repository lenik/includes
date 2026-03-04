#define _POSIX_C_SOURCE 200809L
#include "include_collect.h"
#include "util/logging.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static int strcmp_ptr(const void *a, const void *b) {
	return strcmp(*(const char **)a, *(const char **)b);
}

#define GROW 32
#define PATH_SEG_MAX 256

/** Normalize path: resolve . and .. segments. Preserves absolute (leading /). Caller must free result. */
static char *path_normalize(const char *path) {
	size_t seg_len[PATH_SEG_MAX];
	const char *seg_ptr[PATH_SEG_MAX];
	int n = 0;
	const char *p = path;
	int absolute = (path && path[0] == '/');

	if (!path) return NULL;
	if (absolute) p++;
	while (*p) {
		while (*p == '/') p++;
		if (!*p) break;
		const char *start = p;
		while (*p && *p != '/') p++;
		size_t len = (size_t)(p - start);
		if (len == 1 && start[0] == '.')
			continue;
		if (len == 2 && start[0] == '.' && start[1] == '.') {
			if (n > 0)
				n--;
			else if (n < PATH_SEG_MAX) {
				seg_ptr[n] = start;
				seg_len[n] = 2;
				n++;
			}
			continue;
		}
		if (n < PATH_SEG_MAX) {
			seg_ptr[n] = start;
			seg_len[n] = len;
			n++;
		}
	}
	size_t total = absolute ? 1 : 0;
	for (int i = 0; i < n; i++)
		total += seg_len[i] + (i > 0 ? 1 : 0);
	char *out = (char *)malloc(total + 1);
	if (!out) return NULL;
	char *q = out;
	if (absolute) *q++ = '/';
	for (int i = 0; i < n; i++) {
		if (i) *q++ = '/';
		memcpy(q, seg_ptr[i], seg_len[i]);
		q += seg_len[i];
	}
	*q = '\0';
	return out;
}

/** Join base_dir and path then normalize (e.g. "io" + "../net/URI.cpp" -> "net/URI.cpp"). Caller must free result. */
static char *path_join_and_normalize(const char *base_dir, const char *path) {
	if (!base_dir || !*base_dir) return path_normalize(path);
	if (!path || !*path) return path_normalize(base_dir);
	size_t bl = strlen(base_dir), pl = strlen(path);
	char *combined = (char *)malloc(bl + 1 + pl + 1);
	if (!combined) return NULL;
	memcpy(combined, base_dir, bl);
	combined[bl] = '/';
	memcpy(combined + bl + 1, path, pl + 1);
	char *norm = path_normalize(combined);
	free(combined);
	return norm;
}

static int push_list(char ***list, size_t *count, size_t *cap, const char *path);

static bool in_list(char **list, size_t count, const char *path) {
	size_t i;
	for (i = 0; i < count; i++)
		if (strcmp(list[i], path) == 0) return true;
	return false;
}

static void output_include(include_collect_t *c, const char *out_path, bool is_angled) {
	bool preserve = c->config->preserve_quotes && !c->config->header_to_source;
	if (c->config->sort_output || c->config->json_output) {
		char buf[4096];
		int n = preserve && !c->config->json_output
			? snprintf(buf, sizeof(buf), is_angled ? "<%s>" : "\"%s\"", out_path)
			: snprintf(buf, sizeof(buf), "%s", out_path);
		if (n > 0 && (size_t)n < sizeof(buf))
			push_list(&c->output_lines, &c->output_count, &c->output_cap, strdup(buf));
	} else {
		if (c->output_cb)
			c->output_cb(c->output_ctx, out_path);
		else if (preserve) {
			if (is_angled) printf("<%s>\n", out_path);
			else printf("\"%s\"\n", out_path);
		} else {
			printf("%s\n", out_path);
		}
	}
}

static int push_list(char ***list, size_t *count, size_t *cap, const char *path) {
	if (*count >= *cap) {
		size_t new_cap = *cap ? *cap * 2 : GROW;
		char **n = (char **)realloc(*list, new_cap * sizeof(char *));
		if (!n) return -1;
		*list = n;
		*cap = new_cap;
	}
	(*list)[*count] = strdup(path);
	if (!(*list)[*count]) return -1;
	(*count)++;
	return 0;
}

static void include_cb(void *ctx, const char *include_name, bool is_angled) {
	include_collect_t *c = (include_collect_t *)ctx;
	char *resolved;
	const file_content_t *content;
	char *canonical;
	char *source_path = NULL;
	char *display_alloc = NULL;
	char *canon_visit_alloc = NULL; /* for -c: canonical of source we recurse into (to free at end) */
	const char *display;
	int max_depth = c->config->max_depth;
	bool recurse_quoted = true;
	bool recurse_angled = c->config->recurse_angled;

	if (!c->current_path) return;
	resolved = file_resolve_include(c->current_path, include_name, is_angled);
	if (!resolved) {
		if (c->config && c->config->warnings)
			logwarn_fmt("Include not found: %s", include_name);
		/* In -c (header_to_source) mode, missing headers are ignored entirely:
		 * we only care about headers that can be mapped to real sources.
		 * For the normal mode, still output missing includes by default
		 * unless -n (exists_only) is set. */
		if (!c->config->header_to_source) {
			if (!c->config->exists_only)
				output_include(c, include_name, is_angled);
		}
		return;
	}
	/* Use path as given (no symlink resolution) for visited and display. */
	canonical = strdup(resolved);
	if (!canonical) canonical = resolved;
	/* With -m we only output missing; -n/-m ignored when -c (output existing sources always). */
	if (c->config->missing_only && !c->config->header_to_source)
		goto recurse_only;

	if (max_depth >= 0 && c->depth >= max_depth)
		recurse_quoted = recurse_angled = false;

	/* Only skip recursion when already visited; still output the include. */
	if (in_list(c->visited, c->visited_count, canonical)) {
		free(resolved);
		if (canonical != resolved) free(canonical);
		return;
	}

	if (c->config->quoted_only && is_angled) { free(resolved); if (canonical != resolved) free(canonical); return; }
	if (c->config->angled_only && !is_angled) { free(resolved); if (canonical != resolved) free(canonical); return; }

	bool have_mapped_source = true;
	bool is_root_source = false;

	/* By default (no -c), we print the include name as it appeared in the
	 * source (modulo quotes handled by -p), but joined with the root
	 * directory of the first source when the name is relative (no leading
	 * '.' or '/'). This makes a root like "./src/pp_parse.c" and include
	 * "pp_lexer.h" yield "./src/pp_lexer.h". */
	if (c->config->canonicalize) {
		display = canonical; /* -f: use path as we have it (no realpath) */
	} else {
		display = include_name;
		if (c->root_dir && c->root_dir[0] &&
		    include_name[0] != '.' && include_name[0] != '/') {
			size_t root_len = strlen(c->root_dir);
			size_t name_len = strlen(include_name);
			size_t total = root_len + 1 + name_len + 1;
			display_alloc = (char *)malloc(total);
			if (display_alloc) {
				memcpy(display_alloc, c->root_dir, root_len);
				display_alloc[root_len] = '/';
				memcpy(display_alloc + root_len + 1, include_name, name_len + 1);
				display = display_alloc;
			}
		}
	}

	if (c->config->header_to_source) {
		source_path = file_header_to_source(resolved, c->current_path);
		if (source_path) {
			/* In -c mode, always base display on the actual mapped source
			 * path, not on the include name. This avoids mislabeling
			 * sources when headers come from other directories (e.g.
			 * mapping demo/net/URI.hpp to demo/io/URI.cpp). */
			if (c->config->canonicalize) {
				/* -f: absolute source path (already normalized). */
				display = source_path;
			} else {
				if (display_alloc) {
					free(display_alloc);
					display_alloc = NULL;
				}
				display_alloc = strdup(source_path);
				if (display_alloc)
					display = display_alloc;
				else
					display = source_path;
			}

			/* By default, do not echo the current root source file when -c is used. */
			if (!c->config->echo_sources && c->current_path &&
			    source_path && strcmp(source_path, c->current_path) == 0)
				is_root_source = true;
		} else {
			/* With -c and no corresponding source file, suppress output for this include. */
			have_mapped_source = false;
		}
	}
	/* In non-canonicalize mode, resolve relative display paths (e.g. ../net/URI.cpp)
	 * against root_dir so that "io/../net/URI.cpp" becomes "net/URI.cpp". */
	if (!c->config->canonicalize && c->root_dir && c->root_dir[0] && display &&
	    display[0] == '.') {
		char *norm = path_join_and_normalize(c->root_dir, display);
		if (norm) {
			if (display_alloc) free(display_alloc);
			display_alloc = norm;
			display = norm;
		}
	}
	/* With -c, only output existing C/C++ sources; without -c, output headers as usual.
	 * Also, unless -e/--echo is set, suppress sources that are the current root file. */
	if (!c->config->allow_duplicates) {
		if (!c->config->header_to_source || (have_mapped_source && !is_root_source)) {
			if (in_list(c->printed, c->printed_count, (char *)display)) {
				free(resolved);
				if (canonical != resolved) free(canonical);
				if (source_path) free(source_path);
				if (display_alloc) free(display_alloc);
				return;
			}
			push_list(&c->printed, &c->printed_count, &c->printed_cap, display);
		}
	}
	if (c->config->graphviz_output && c->current_path &&
	    (!c->config->header_to_source || (have_mapped_source && !is_root_source))) {
		if (c->edge_count >= c->edge_cap) {
			size_t new_cap = c->edge_cap ? c->edge_cap * 2 : GROW;
			char **f = (char **)realloc(c->edge_from, new_cap * sizeof(char *));
			char **t = (char **)realloc(c->edge_to, new_cap * sizeof(char *));
			if (f) c->edge_from = f;
			if (t) c->edge_to = t;
			if (f && t) c->edge_cap = new_cap;
		}
		if (c->edge_count < c->edge_cap) {
			c->edge_from[c->edge_count] = strdup(c->current_path);
			c->edge_to[c->edge_count] = strdup(display);
			if (c->edge_from[c->edge_count] && c->edge_to[c->edge_count])
				c->edge_count++;
			else {
				free(c->edge_from[c->edge_count]);
				free(c->edge_to[c->edge_count]);
			}
		}
	}
	if (!c->config->graphviz_output &&
	    (!c->config->header_to_source || (have_mapped_source && !is_root_source)))
		output_include(c, display, is_angled);
	if (display_alloc) free(display_alloc);

recurse_only:

	{
		bool do_recurse = (is_angled && recurse_angled) || (!is_angled && recurse_quoted);
		/* In -c mode, recurse into the mapped source file so we discover its includes
		 * and map those to sources too (transitive closure for compile automation). */
		const char *file_to_read = (c->config->header_to_source && source_path) ? source_path : resolved;
		const char *canon_visit = canonical;

		if (c->config->header_to_source && source_path) {
			canon_visit_alloc = strdup(source_path);
			if (canon_visit_alloc) canon_visit = canon_visit_alloc;
		}
		if (do_recurse && canon_visit && !in_list(c->visited, c->visited_count, canon_visit)) {
			content = file_read(file_to_read);
			if (content) {
				push_list(&c->visited, &c->visited_count, &c->visited_cap, canon_visit);
			}
		} else {
			content = NULL;
		}
	}
	if (content) {
		logdebug_fmt("Parsing %s", (c->config->header_to_source && source_path) ? source_path : resolved);
		c->depth++;
		{
			const char *prev = c->current_path;
			const char *file_to_read = (c->config->header_to_source && source_path) ? source_path : resolved;
			c->current_path = file_to_read;
			if (!pp_parse(content->data, content->len, c->current_path, c->config, c->macros, include_cb, c, NULL))
				c->parse_error = 1;
			c->current_path = prev;
		}
		c->depth--;
	}
	/* In -c mode, also parse the header itself (if it differs from the source) to discover
	 * includes that are in the header but not in the source. This ensures we find transitive
	 * includes through header files and map them to their corresponding sources. */
	if (c->config->header_to_source && source_path && resolved && strcmp(resolved, source_path) != 0) {
		const file_content_t *header_content = file_read(resolved);
		if (header_content) {
			bool header_in_visited = in_list(c->visited, c->visited_count, canonical);
			if (!header_in_visited) {
				push_list(&c->visited, &c->visited_count, &c->visited_cap, canonical);
				logdebug_fmt("Parsing %s", resolved);
				c->depth++;
				{
					const char *prev = c->current_path;
					c->current_path = resolved;
					if (!pp_parse(header_content->data, header_content->len, c->current_path, c->config, c->macros, include_cb, c, NULL))
						c->parse_error = 1;
					c->current_path = prev;
				}
				c->depth--;
			}
		}
	}
	free(resolved);
	if (canonical != resolved) free(canonical);
	if (source_path) free(source_path);
	if (canon_visit_alloc) free(canon_visit_alloc);
}

void include_collect_init(include_collect_t *c, const includes_config_t *config, macro_table_t *macros) {
	memset(c, 0, sizeof(*c));
	c->config = config;
	c->macros = macros;
}

static void collect_root_source(include_collect_t *c, const char *path) {
	char *path_copy;

	if (!c->config->header_to_source) return;
	if (push_list(&c->root_sources, &c->root_count, &c->root_cap, path) != 0) return;
	
	/* In -c mode without -e, add root sources to printed list to suppress them
	 * if they're encountered as includes later. */
	if (!c->config->echo_sources) {
		path_copy = strdup(path);
		if (path_copy)
			push_list(&c->printed, &c->printed_count, &c->printed_cap, path_copy);
	} else {
		/* With -e, output the root source immediately */
		output_include(c, path, false);
	}
}

void include_collect_set_output_cb(include_collect_t *c, void (*cb)(void *ctx, const char *path), void *ctx) {
	c->output_cb = cb;
	c->output_ctx = ctx;
}

static void json_escape_print(const char *s) {
	putchar('"');
	for (; *s; s++) {
		if (*s == '\\') fputs("\\\\", stdout);
		else if (*s == '"') fputs("\\\"", stdout);
		else putchar((unsigned char)*s);
	}
	putchar('"');
}

static void dot_escape_print(const char *s) {
	putchar('"');
	for (; *s; s++) {
		if (*s == '\\') fputs("\\\\", stdout);
		else if (*s == '"') fputs("\\\"", stdout);
		else putchar((unsigned char)*s);
	}
	putchar('"');
}

void include_collect_finish(include_collect_t *c) {
	size_t i;
	if ((c->config->sort_output || c->config->json_output) && c->output_count > 0) {
		if (c->config->sort_output)
			qsort(c->output_lines, c->output_count, sizeof(char *), strcmp_ptr);
		if (c->config->json_output)
			putchar('[');
		for (i = 0; i < c->output_count; i++) {
			if (c->output_cb) {
				c->output_cb(c->output_ctx, c->output_lines[i]);
			} else if (c->config->json_output) {
				json_escape_print(c->output_lines[i]);
				if (i + 1 < c->output_count) printf(", ");
			} else {
				printf("%s\n", c->output_lines[i]);
			}
			free(c->output_lines[i]);
		}
		if (c->config->json_output && !c->output_cb)
			printf("]\n");
		free(c->output_lines);
		c->output_lines = NULL;
		c->output_count = c->output_cap = 0;
	}
	if (c->config->graphviz_output && c->edge_count > 0) {
		printf("digraph includes {\n");
		for (i = 0; i < c->edge_count; i++) {
			printf("  ");
			dot_escape_print(c->edge_from[i]);
			printf(" -> ");
			dot_escape_print(c->edge_to[i]);
			printf(";\n");
			free(c->edge_from[i]);
			free(c->edge_to[i]);
		}
		printf("}\n");
		free(c->edge_from);
		free(c->edge_to);
		c->edge_from = c->edge_to = NULL;
		c->edge_count = c->edge_cap = 0;
	}
	for (i = 0; i < c->visited_count; i++) free(c->visited[i]);
	free(c->visited);
	for (i = 0; i < c->printed_count; i++) free(c->printed[i]);
	free(c->printed);
	for (i = 0; i < c->root_count; i++) free(c->root_sources[i]);
	free(c->root_sources);
	c->visited = NULL; c->visited_count = c->visited_cap = 0;
	c->printed = NULL; c->printed_count = c->printed_cap = 0;
	c->root_sources = NULL; c->root_count = c->root_cap = 0;
	free(c->root_dir);
	c->root_dir = NULL;
}

bool include_collect_file(include_collect_t *c, const char *path) {
	const file_content_t *content;
	char *path_norm;

	path_norm = path_normalize(path);
	if (!path_norm) path_norm = strdup(path);
	if (!path_norm) return false;

	/* Track root source files for -c -e mode */
	collect_root_source(c, path_norm);

	logdebug_fmt("Parsing %s", path_norm);
	content = file_read(path_norm);
	if (!content) { free(path_norm); return false; }
	if (in_list(c->visited, c->visited_count, path_norm)) {
		free(path_norm);
		return true;
	}
	push_list(&c->visited, &c->visited_count, &c->visited_cap, path_norm);
	/* Record root directory for -c join-name logic (first call only). */
	if (!c->root_dir) {
		const char *slash = strrchr(path_norm, '/');
		if (slash) {
			size_t len = (size_t)(slash - path_norm);
			c->root_dir = (char *)malloc(len + 1);
			if (c->root_dir) {
				memcpy(c->root_dir, path_norm, len);
				c->root_dir[len] = '\0';
			}
		}
	}
	c->depth = 0;
	c->current_path = path_norm;
	if (!pp_parse(content->data, content->len, path_norm, c->config, c->macros, include_cb, c, NULL)) {
		c->parse_error = 1;
		c->current_path = NULL;
		free(path_norm);
		return false;
	}
	c->current_path = NULL;
	free(path_norm);
	return true;
}
