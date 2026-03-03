#!/bin/sh
# Run includes and check exit code / output. Called from meson test.
set -eu
BINARY="$1"
FIXTURE="$2"
TEST_NAME="$3"
shift 3

cd "$FIXTURE"
case "$TEST_NAME" in
  one_include)
    out=$(mktemp)
    "$BINARY" -I . one_include.c 2>/dev/null >"$out"
    grep -q empty.h "$out" || { cat "$out"; exit 1; }
    rm -f "$out"
    ;;
  if_endif)
    "$BINARY" -I . if_endif.c 2>/dev/null | grep -q empty.h || exit 1
    ;;
  unmatched_endif)
    set +e
    "$BINARY" -I . unmatched_endif.c 2>/dev/null
    r=$?
    set -e
    [ "$r" -eq 1 ] || exit 1
    ;;
  sort_output)
    out=$(mktemp)
    "$BINARY" -I . -s one_include.c 2>/dev/null >"$out"
    test "$(wc -l <"$out")" -ge 1 || exit 1
    rm -f "$out"
    ;;
  depth_limit_0)
    n=$("$BINARY" -I . -L 0 depth_root.c 2>/dev/null | wc -l)
    [ "$n" -eq 1 ] || exit 1
    ;;
  depth_limit_1)
    n=$("$BINARY" -I . -L 1 depth_root.c 2>/dev/null | wc -l)
    [ "$n" -eq 2 ] || exit 1
    ;;
  circular)
    n=$("$BINARY" -I . circ_root.c 2>/dev/null | wc -l)
    [ "$n" -eq 2 ] || exit 1
    ;;
  header_to_source)
    "$BINARY" -I . -c one_include.c 2>/dev/null | grep -q '\.c"' || \
    "$BINARY" -I . -c one_include.c 2>/dev/null | grep -q '\.c$' || exit 1
    ;;
  parallel)
    n=$("$BINARY" -I . -P one_include.c depth_root.c 2>/dev/null | wc -l)
    [ "$n" -ge 2 ] || exit 1
    ;;
  toolkit_macros)
    # With gcc toolkit, __linux__ is defined on Linux so empty.h should appear
    out=$(mktemp)
    "$BINARY" -I . -T gcc toolkit_ifdef.c 2>/dev/null >"$out"
    grep -q empty.h "$out" || exit 1
    rm -f "$out"
    ;;
  *)
    echo "Unknown test: $TEST_NAME" >&2
    exit 1
    ;;
esac
