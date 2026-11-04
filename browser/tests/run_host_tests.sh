#!/bin/sh
set -eu

test_dir="$(mktemp -d)"
trap 'rm -rf "$test_dir"' EXIT HUP INT TERM
qjs_dir=browser/third_party/quickjs
for part in quickjs dtoa libregexp libunicode cutils; do
    cc -std=gnu11 -O1 -fwrapv -D_GNU_SOURCE -DCONFIG_VERSION='"2026-06-04"' \
      -I"$qjs_dir" -c "$qjs_dir/$part.c" -o "$test_dir/$part.o"
done
ar rcs "$test_dir/libquickjs.a" "$test_dir"/*.o

cc -std=gnu11 -Wall -Wextra -Werror -Ibrowser/source \
  browser/tests/page_test.c browser/source/page.c -o "$test_dir/page-test" \
  $(pkg-config --cflags --libs libcurl)
"$test_dir/page-test"

cc -std=gnu11 -Wall -Wextra -Werror -Ibrowser/source -isystem "$qjs_dir" \
  browser/tests/js_test.c browser/source/js_runtime.c browser/source/page.c \
  "$test_dir/libquickjs.a" -o "$test_dir/js-test" \
  $(pkg-config --cflags --libs libcurl) -lm
"$test_dir/js-test"
