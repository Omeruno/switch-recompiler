#!/bin/sh
set -eu

python3 browser/tests/inspect_wasm_test.py

test_dir="$(mktemp -d)"
trap 'rm -rf "$test_dir"' EXIT HUP INT TERM
qjs_dir=browser/third_party/quickjs
python3 browser/tools/embed_js.py browser/source/dom.js "$test_dir/dom_js.h"
for part in quickjs dtoa libregexp libunicode cutils; do
    cc -std=gnu11 -O1 -fwrapv -D_GNU_SOURCE -DCONFIG_VERSION='"2026-06-04"' \
      -I"$qjs_dir" -c "$qjs_dir/$part.c" -o "$test_dir/$part.o"
done
ar rcs "$test_dir/libquickjs.a" "$test_dir"/*.o

cc -std=gnu11 -Wall -Wextra -Werror -Ibrowser/source \
  browser/tests/page_test.c browser/source/page.c browser/source/profile.c browser/source/ca_bundle.c -o "$test_dir/page-test" \
  $(pkg-config --cflags --libs libcurl)
"$test_dir/page-test"

cc -std=gnu11 -Wall -Wextra -Werror -I"$test_dir" -Ibrowser/source -isystem "$qjs_dir" \
  browser/tests/js_test.c browser/source/js_runtime.c browser/source/page.c browser/source/profile.c browser/source/ca_bundle.c \
  "$test_dir/libquickjs.a" -o "$test_dir/js-test" \
  $(pkg-config --cflags --libs libcurl) -lm
"$test_dir/js-test"

cc -std=gnu11 -Wall -Wextra -Werror -I"$test_dir" -Ibrowser/source -isystem "$qjs_dir" \
  browser/tests/session_test.c browser/source/js_runtime.c browser/source/page.c browser/source/profile.c browser/source/ca_bundle.c \
  "$test_dir/libquickjs.a" -o "$test_dir/session-test" \
  $(pkg-config --cflags --libs libcurl) -lm
"$test_dir/session-test"

python3 browser/tools/update_ca.py --check
cc -std=gnu11 -Wall -Wextra -Werror -Ibrowser/source \
  browser/tests/profile_test.c browser/source/profile.c browser/source/ca_bundle.c -o "$test_dir/profile-test" \
  $(pkg-config --cflags --libs libcurl)
"$test_dir/profile-test" "$test_dir/profile"

for test_name in storage_js https; do
  cc -std=gnu11 -Wall -Wextra -Werror -I"$test_dir" -Ibrowser/source -isystem "$qjs_dir" \
    "browser/tests/${test_name}_test.c" browser/source/js_runtime.c browser/source/page.c browser/source/profile.c browser/source/ca_bundle.c \
    "$test_dir/libquickjs.a" -o "$test_dir/${test_name}-test" \
    $(pkg-config --cflags --libs libcurl) -lm
done
"$test_dir/storage_js-test" "$test_dir/js-profile"
python3 browser/tests/https_fixture.py "$test_dir/https-test"

cc -std=gnu11 -Wall -Wextra -Werror -Ibrowser/source browser/tests/ca_bundle_test.c browser/source/ca_bundle.c -o "$test_dir/ca-bundle-test"
mkdir "$test_dir/ca-batches"
"$test_dir/ca-bundle-test" browser/romfs/cacert.pem "$test_dir/ca-batches"
python3 - "$test_dir/ca-batches" <<'PYTEST'
import json, ssl, sys
from pathlib import Path
roots = 0
files = list(Path(sys.argv[1]).glob('*.pem'))
for file in files:
    payload = file.read_bytes()
    assert payload[-1:] == b'\0' and b'\0' not in payload[:-1]
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
    context.load_verify_locations(cadata=payload[:-1].decode('ascii'))
    roots += len(context.get_ca_certs())
expected = json.loads(Path('browser/romfs/CA_BUNDLE.json').read_text())['certificates']
assert roots == expected and len(files) == (expected + 15) // 16
print(f'Native CA payload cryptographic parsing: PASS ({roots} roots / {len(files)} objects)')
PYTEST
