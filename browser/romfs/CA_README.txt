Trust bundle source: https://curl.se/ca/cacert.pem
Extraction details: https://curl.se/docs/caextract.html
The upstream Mozilla-derived bundle is distributed under MPL 2.0:
https://www.mozilla.org/MPL/2.0/

CA_BUNDLE.json records the extraction date, SHA-256 and certificate count.
Refresh deliberately with: python3 browser/tools/update_ca.py
Validate offline with: python3 browser/tools/update_ca.py --check
The application does not download or accept replacement trust roots at runtime.
On Switch, roots are imported individually into the libnx TLS context.
Horizon's built-in trust store may also apply. Import failures never disable
certificate or hostname validation. Native import still needs a hardware test.
