#define _POSIX_C_SOURCE 200809L
#include "config.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <getopt.h>

#define CONFIG_GROW 16

static int push_include_path(includes_config_t *c, const char *path) {
	if (c->include_paths_count >= c->include_paths_cap) {
		size_t new_cap = c->include_paths_cap ? c->include_paths_cap * 2 : CONFIG_GROW;
		char **n = (char **)realloc(c->include_paths, new_cap * sizeof(char *));
		if (!n) return -1;
		c->include_paths = n;
		c->include_paths_cap = new_cap;
	}
	c->include_paths[c->include_paths_count++] = strdup(path);
	return 0;
}

static int push_source(includes_config_t *c, const char *path) {
	if (c->sources_count >= c->sources_cap) {
		size_t new_cap = c->sources_cap ? c->sources_cap * 2 : CONFIG_GROW;
		char **n = (char **)realloc(c->sources, new_cap * sizeof(char *));
		if (!n) return -1;
		c->sources = n;
		c->sources_cap = new_cap;
	}
	c->sources[c->sources_count++] = strdup(path);
	return 0;
}

void config_init(includes_config_t *c) {
	memset(c, 0, sizeof(*c));
	c->max_depth = -1; /* unlimited by default */
}

void config_free(includes_config_t *c) {
	size_t i;
	for (i = 0; i < c->include_paths_count; i++)
		free(c->include_paths[i]);
	free(c->include_paths);
	for (i = 0; i < c->sources_count; i++)
		free(c->sources[i]);
	free(c->sources);
	c->include_paths = NULL;
	c->include_paths_count = c->include_paths_cap = 0;
	c->sources = NULL;
	c->sources_count = c->sources_cap = 0;
}

void config_print_usage(void) {
	fprintf(stderr,
		"Usage: includes [OPTIONS] SOURCES...\n"
		"Resolve #include directives from C/C++ sources.\n\n"
		"Options:\n"
		"  -I PATH       Add include search path\n"
		"  -T TOOLKIT    Toolkit: gcc, msc\n"
		"  -t TRIPLET    Target triplet (e.g. x86_64-linux-gnu)\n"
		"  -L NUM        Max include depth (0 = root only)\n"
		"  -d            Allow duplicate includes in output\n"
		"  -u            Output quoted includes only\n"
		"  -a            Output angled includes only\n"
		"  -A            Recurse into angled includes\n"
		"  -p            Preserve quotes in output (\"file\" / <file>)\n"
		"  -c            Map headers to corresponding source files (suppress root sources by default)\n"
		"  -e            With -c, also echo source files listed on the command line\n"
		"  -n            Only output existing (resolved) paths (ignored with -c)\n"
		"  -m            Only output missing (unresolved) include names (ignored with -c)\n"
		"  -f            Canonicalize output paths (absolute)\n"
		"  -s            Sort output\n"
		"  -j            JSON output (single array)\n"
		"  -g            Graphviz .dot output (include graph)\n"
		"  -P            Parallel (multiple sources in threads)\n"
		"  -w            Enable parser warnings (missing #endif, etc.)\n"
		"  -q            Quiet (errors only)\n"
		"  -v            Verbose\n"
		"  --help        Show this help\n"
	);
}

int config_parse_options(includes_config_t *c, int argc, char **argv, int *first_arg) {
	static struct option long_options[] = {
		{"help", no_argument, 0, 'h'},
		{"echo", no_argument, 0, 'e'},
		{"exists", no_argument, 0, 'n'},
		{"missing", no_argument, 0, 'm'},
		{"canonicalize", no_argument, 0, 'f'},
		{0, 0, 0, 0}
	};
	int opt;
	const char *arg;

	while ((opt = getopt_long(argc, argv, "I:T:t:L:duaApcenmfqsvjgPwh", long_options, NULL)) != -1) {
		switch (opt) {
		case 'I':
			arg = optarg ? optarg : "";
			if (push_include_path(c, arg) != 0) return -1;
			break;
		case 'T':
			c->toolkit = optarg;
			break;
		case 't':
			c->triplet = optarg;
			break;
		case 'L':
			c->max_depth = (int)strtol(optarg, NULL, 10);
			break;
		case 'd':
			c->allow_duplicates = true;
			break;
		case 'u':
			c->quoted_only = true;
			break;
		case 'a':
			c->angled_only = true;
			break;
		case 'A':
			c->recurse_angled = true;
			break;
		case 'p':
			c->preserve_quotes = true;
			break;
		case 'c':
			c->header_to_source = true;
			break;
		case 'e':
			c->echo_sources = true;
			break;
		case 'n':
			c->exists_only = true;
			break;
		case 'm':
			c->missing_only = true;
			break;
		case 'f':
			c->canonicalize = true;
			break;
		case 's':
			c->sort_output = true;
			break;
		case 'j':
			c->json_output = true;
			break;
		case 'g':
			c->graphviz_output = true;
			break;
		case 'P':
			c->parallel = true;
			break;
		case 'w':
			c->warnings = true;
			break;
		case 'q':
			c->verbosity_delta--;
			break;
		case 'v':
			c->verbosity_delta++;
			break;
		case 'h':
			return 1;
		default:
			return -1;
		}
	}

	*first_arg = optind;
	for (; optind < argc; optind++) {
		if (push_source(c, argv[optind]) != 0) return -1;
	}
	return 0;
}
