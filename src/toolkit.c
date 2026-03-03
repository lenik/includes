#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE
#include "toolkit.h"
#include "util/logging.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#define BUF_SIZE 65536
#define PATHS_GROW 32
#define CACHE_SUFFIX ".macros"

static bool run_cmd(const char *cmd, char *out, size_t out_size) {
	FILE *f = popen(cmd, "r");
	if (!f) return false;
	size_t n = 0;
	while (n < out_size - 1 && fgets(out + n, (int)(out_size - n), f))
		n += strlen(out + n);
	out[n] = '\0';
	pclose(f);
	return true;
}

static void parse_define_line(const char *line, macro_table_t *macros) {
	const char *p = line;
	while (*p == ' ' || *p == '\t') p++;
	if (strncmp(p, "#define ", 8) != 0) return;
	p += 8;
	while (*p == ' ' || *p == '\t') p++;
	const char *name_start = p;
	while (*p && *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r') p++;
	if (p == name_start) return;
	char name[256];
	size_t name_len = (size_t)(p - name_start);
	if (name_len >= sizeof(name)) return;
	memcpy(name, name_start, name_len);
	name[name_len] = '\0';
	while (*p == ' ' || *p == '\t') p++;
	const char *value_start = p;
	while (*p && *p != '\n' && *p != '\r') p++;
	char value[1024];
	size_t value_len = (size_t)(p - value_start);
	if (value_len >= sizeof(value)) value_len = sizeof(value) - 1;
	memcpy(value, value_start, value_len);
	value[value_len] = '\0';
	macro_define(macros, name, value[0] ? value : NULL);
}

static bool load_macros_from_buf(char *buf, macro_table_t *macros) {
	char *line = buf;
	while (*line) {
		char *eol = strchr(line, '\n');
		if (eol) *eol = '\0';
		parse_define_line(line, macros);
		line = eol ? eol + 1 : line + strlen(line);
	}
	return true;
}

static bool gcc_capture_macros(macro_table_t *macros) {
	const char *cache_dir = getenv("INCLUDES_CACHE_DIR");
	char cache_path[1024];
	FILE *f = NULL;
	char *buf = (char *)malloc(BUF_SIZE);
	if (!buf) return false;

	if (cache_dir && cache_dir[0]) {
		snprintf(cache_path, sizeof(cache_path), "%s/gcc" CACHE_SUFFIX, cache_dir);
		f = fopen(cache_path, "r");
		if (f) {
			size_t n = fread(buf, 1, BUF_SIZE - 1, f);
			buf[n] = '\0';
			fclose(f);
			load_macros_from_buf(buf, macros);
			free(buf);
			return true;
		}
	}

	/* Run gcc -dM -E */
	if (!run_cmd("gcc -dM -E -x c - < /dev/null 2>/dev/null", buf, BUF_SIZE)) {
		free(buf);
		return false;
	}
	load_macros_from_buf(buf, macros);

	if (cache_dir && cache_dir[0]) {
		snprintf(cache_path, sizeof(cache_path), "%s/gcc" CACHE_SUFFIX, cache_dir);
		f = fopen(cache_path, "w");
		if (f) {
			fputs(buf, f);
			fclose(f);
		}
	}
	free(buf);
	return true;
}

static int push_path(char ***paths, size_t *count, size_t *cap, const char *path) {
	if (*count >= *cap) {
		size_t new_cap = *cap ? *cap * 2 : PATHS_GROW;
		char **n = (char **)realloc(*paths, new_cap * sizeof(char *));
		if (!n) return -1;
		*paths = n;
		*cap = new_cap;
	}
	(*paths)[*count] = strdup(path);
	if (!(*paths)[*count]) return -1;
	(*count)++;
	return 0;
}

/* Parse " gcc -E -Wp,-v -x c - 2>&1 < /dev/null" output for #include "..." search paths. */
static bool gcc_capture_include_paths(char ***paths, size_t *path_count, size_t *path_cap) {
	char *buf = (char *)malloc(BUF_SIZE);
	if (!buf) return false;
	bool ok = run_cmd("gcc -E -Wp,-v -x c - < /dev/null 2>&1", buf, BUF_SIZE);
	if (!ok) { free(buf); return false; }
	bool in_list = false;
	char *line = buf;
	while (*line) {
		char *eol = strchr(line, '\n');
		if (eol) *eol = '\0';
		if (strstr(line, "search starts here") != NULL)
			in_list = true;
		else if (strstr(line, "End of search list") != NULL)
			in_list = false;
		else if (in_list) {
			const char *p = line;
			while (*p == ' ' || *p == '\t') p++;
			if (*p == '/' && strlen(p) > 1)
				push_path(paths, path_count, path_cap, p);
		}
		line = eol ? eol + 1 : line + strlen(line);
	}
	free(buf);
	return true;
}

bool toolkit_capture(const char *toolkit_name, macro_table_t *macros,
                     char ***paths, size_t *path_count, size_t *path_cap) {
	const char *t = toolkit_name && toolkit_name[0] ? toolkit_name : "gcc";
	if (strcmp(t, "gcc") == 0) {
		if (!gcc_capture_macros(macros)) {
			logdebug_fmt("gcc -dM -E failed");
			return false;
		}
		if (paths && path_count && path_cap) {
			*path_count = 0;
			*path_cap = 0;
			*paths = NULL;
			gcc_capture_include_paths(paths, path_count, path_cap);
		}
		return true;
	}
	if (strcmp(t, "msc") == 0) {
		/* Minimal MSVC predefined macros (stub; no cl.exe invocation) */
		macro_define(macros, "_WIN32", "1");
		macro_define(macros, "_MSC_VER", "1900");
		macro_define(macros, "_MSC_FULL_VER", "190000000");
		return true;
	}
	return false;
}

char *toolkit_get_triplet(const char *toolkit_name) {
	char buf[256];
	const char *t = toolkit_name && toolkit_name[0] ? toolkit_name : "gcc";
	if (strcmp(t, "gcc") != 0) return NULL;
	if (!run_cmd("gcc -dumpmachine 2>/dev/null", buf, sizeof(buf)))
		return NULL;
	buf[strcspn(buf, "\n\r")] = '\0';
	return strdup(buf);
}

void toolkit_triplet_to_macros(const char *triplet, macro_table_t *macros) {
	if (!triplet || !macros) return;
	if (strstr(triplet, "linux") != NULL)
		macro_define(macros, "__linux__", "1");
	if (strstr(triplet, "mingw") != NULL || strstr(triplet, "win32") != NULL)
		macro_define(macros, "__WIN32__", "1");
	if (strstr(triplet, "apple") != NULL || strstr(triplet, "macos") != NULL)
		macro_define(macros, "__APPLE__", "1");
	if (strncmp(triplet, "x86_64", 6) == 0)
		macro_define(macros, "__x86_64__", "1");
	if (strncmp(triplet, "i686", 4) == 0 || strncmp(triplet, "i386", 4) == 0)
		macro_define(macros, "__i386__", "1");
}
