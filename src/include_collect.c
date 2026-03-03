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
	canonical = file_canonical_path(resolved);
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
		display = canonical;
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
		source_path = file_header_to_source(resolved);
		if (source_path) {
			if (c->config->canonicalize) {
				/* -f: absolute canonical source path. */
				display = source_path;
			} else {
				/* Build a relative source path directly from the include name:
				 *   "foo/bar.h"     -> "foo/bar.c"
				 *   "./foo/bar.hxx" -> "./foo/bar.cpp"
				 * using the extension chosen by file_header_to_source(). */
				const char *src_ext = strrchr(source_path, '.');
				const char *ext = src_ext ? src_ext : "";
				size_t name_len = strlen(include_name);
				const char *slash = strrchr(include_name, '/');
				const char *base = slash ? slash + 1 : include_name;
				const char *dot = strrchr(base, '.');
				size_t stem_len = dot ? (size_t)(dot - include_name) : name_len;
				const char *rel = include_name;

				/* For join-names: if we have a root_dir and the relative path does not
				 * start with '.' or '/', prepend root_dir + '/'. This makes:
				 *   root: "src/pp_parse.c", include "pp_lexer.h" -> "src/pp_lexer.c"
				 * while keeping "./foo/bar.c" unchanged. */
				if (c->root_dir && c->root_dir[0] &&
				    include_name[0] != '.' && include_name[0] != '/') {
					size_t root_len = strlen(c->root_dir);
					size_t total = root_len + 1 + stem_len + strlen(ext) + 1;
					display_alloc = (char *)malloc(total);
					if (display_alloc) {
						memcpy(display_alloc, c->root_dir, root_len);
						display_alloc[root_len] = '/';
						memcpy(display_alloc + root_len + 1, include_name, stem_len);
						strcpy(display_alloc + root_len + 1 + stem_len, ext);
						display = display_alloc;
					}
				} else {
					display_alloc = (char *)malloc(stem_len + strlen(ext) + 1);
					if (display_alloc) {
						memcpy(display_alloc, rel, stem_len);
						strcpy(display_alloc + stem_len, ext);
						display = display_alloc;
					}
				}
			}

			/* By default, do not echo the current root source file when -c is used. */
			if (!c->config->echo_sources && c->current_path) {
				char *canon_out = file_canonical_path(source_path);
				char *canon_cur = file_canonical_path(c->current_path);
				if (canon_out && canon_cur && strcmp(canon_out, canon_cur) == 0)
					is_root_source = true;
				free(canon_out);
				free(canon_cur);
			}
		} else {
			/* With -c and no corresponding source file, suppress output for this include. */
			have_mapped_source = false;
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
	if (source_path) free(source_path);
	if (display_alloc) free(display_alloc);

recurse_only:

	{
		bool do_recurse = (is_angled && recurse_angled) || (!is_angled && recurse_quoted);
		content = do_recurse ? file_read(resolved) : NULL;
	}
	if (content) {
		logdebug_fmt("Parsing %s", resolved);
		push_list(&c->visited, &c->visited_count, &c->visited_cap, canonical);
		c->depth++;
		{
			const char *prev = c->current_path;
			c->current_path = resolved;
			if (!pp_parse(content->data, content->len, c->current_path, c->config, c->macros, include_cb, c, NULL))
				c->parse_error = 1;
			c->current_path = prev;
		}
		c->depth--;
	}
	free(resolved);
	if (canonical != resolved) free(canonical);
}

void include_collect_init(include_collect_t *c, const includes_config_t *config, macro_table_t *macros) {
	memset(c, 0, sizeof(*c));
	c->config = config;
	c->macros = macros;
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
	c->visited = NULL; c->visited_count = c->visited_cap = 0;
	c->printed = NULL; c->printed_count = c->printed_cap = 0;
	free(c->root_dir);
	c->root_dir = NULL;
}

bool include_collect_file(include_collect_t *c, const char *path) {
	const file_content_t *content;
	char *canonical;
	logdebug_fmt("Parsing %s", path);
	content = file_read(path);
	if (!content) return false;
	canonical = file_canonical_path(path);
	if (canonical) {
		if (in_list(c->visited, c->visited_count, canonical)) {
			free(canonical);
			return true;
		}
		push_list(&c->visited, &c->visited_count, &c->visited_cap, canonical);
		free(canonical);
	}
	/* Record root directory for -c join-name logic (first call only). */
	if (!c->root_dir) {
		const char *slash = strrchr(path, '/');
		if (slash) {
			size_t len = (size_t)(slash - path);
			c->root_dir = (char *)malloc(len + 1);
			if (c->root_dir) {
				memcpy(c->root_dir, path, len);
				c->root_dir[len] = '\0';
			}
		}
	}
	c->depth = 0;
	c->current_path = path;
	if (!pp_parse(content->data, content->len, path, c->config, c->macros, include_cb, c, NULL)) {
		c->parse_error = 1;
		c->current_path = NULL;
		return false;
	}
	c->current_path = NULL;
	return true;
}
