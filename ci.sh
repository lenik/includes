#!/bin/sh
# CI script: build, test, and optionally install. Usage: ./ci.sh [install]
set -eu
cd "$(dirname "$0")"
echo "== Configuring =="
meson setup build -Dbuildtype=release
echo "== Building =="
meson compile -C build
echo "== Testing =="
meson test -C build --print-errorlogs
if [ "${1:-}" = "install" ]; then
	echo "== Installing =="
	DESTDIR=./install meson install -C build --no-rebuild
	echo "Installed under ./install"
fi
