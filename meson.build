project('includes', 'c',
  version : '0.0.1',
  license : 'GPL',
  default_options : ['warning_level=2', 'c_std=c11'])
thread_dep = dependency('threads')

inc = include_directories('src')

logger_sources = files(
  'src/util/logger.c',
  'src/util/logging.c',
)

includes_sources = files(
  'src/main.c',
  'src/config.c',
  'src/file_loader.c',
  'src/pp_lexer.c',
  'src/macro.c',
  'src/pp_expr.c',
  'src/pp_cond.c',
  'src/pp_parse.c',
  'src/include_collect.c',
  'src/toolkit.c',
  'src/lib.c',
) + logger_sources

exe = executable('includes',
  includes_sources,
  include_directories : [inc, include_directories('src/util')],
  dependencies : thread_dep,
  install : true)
install_man('includes.1')

# Library: same code without main.c, for embedding
lib_sources = files(
  'src/config.c', 'src/file_loader.c', 'src/pp_lexer.c', 'src/macro.c',
  'src/pp_expr.c', 'src/pp_cond.c', 'src/pp_parse.c', 'src/include_collect.c',
  'src/toolkit.c', 'src/lib.c',
) + logger_sources
lib_includes = static_library('includes',
  lib_sources,
  include_directories : [inc, include_directories('src/util')],
  dependencies : thread_dep,
  install : true)
install_headers('include/includes.h', subdir : 'includes')
install_headers('src/config.h', subdir : 'includes')

# Tests (run from fixture dir so -I . resolves empty.h)
test('one_include', find_program('tests/run_tests.sh'),
  args : [exe, meson.current_source_dir() + '/tests/fixture', 'one_include'],
  workdir : meson.current_source_dir() + '/tests/fixture')
test('if_endif', find_program('tests/run_tests.sh'),
  args : [exe, meson.current_source_dir() + '/tests/fixture', 'if_endif'],
  workdir : meson.current_source_dir() + '/tests/fixture')
test('unmatched_endif', find_program('tests/run_tests.sh'),
  args : [exe, meson.current_source_dir() + '/tests/fixture', 'unmatched_endif'],
  workdir : meson.current_source_dir() + '/tests/fixture')
test('sort_output', find_program('tests/run_tests.sh'),
  args : [exe, meson.current_source_dir() + '/tests/fixture', 'sort_output'],
  workdir : meson.current_source_dir() + '/tests/fixture')
test('depth_limit_0', find_program('tests/run_tests.sh'),
  args : [exe, meson.current_source_dir() + '/tests/fixture', 'depth_limit_0'],
  workdir : meson.current_source_dir() + '/tests/fixture')
test('depth_limit_1', find_program('tests/run_tests.sh'),
  args : [exe, meson.current_source_dir() + '/tests/fixture', 'depth_limit_1'],
  workdir : meson.current_source_dir() + '/tests/fixture')
test('circular', find_program('tests/run_tests.sh'),
  args : [exe, meson.current_source_dir() + '/tests/fixture', 'circular'],
  workdir : meson.current_source_dir() + '/tests/fixture')
test('header_to_source', find_program('tests/run_tests.sh'),
  args : [exe, meson.current_source_dir() + '/tests/fixture', 'header_to_source'],
  workdir : meson.current_source_dir() + '/tests/fixture')
test('parallel', find_program('tests/run_tests.sh'),
  args : [exe, meson.current_source_dir() + '/tests/fixture', 'parallel'],
  workdir : meson.current_source_dir() + '/tests/fixture')
test('toolkit_macros', find_program('tests/run_tests.sh'),
  args : [exe, meson.current_source_dir() + '/tests/fixture', 'toolkit_macros'],
  workdir : meson.current_source_dir() + '/tests/fixture')
