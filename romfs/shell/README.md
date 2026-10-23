# Native shell artwork

The three unmodified 32x32 Tango PNGs are pinned to upstream commit
`41b8f6abd7ebb9d5c35e563c8cd8c410c88f114a` of
https://github.com/stephenc/tango-icon-theme . Its public-domain declaration is
preserved verbatim in COPYING.tango. manifest.json records each original path,
source URL and SHA-256, decoded RGBA hash and generated native header hash.

The C++ renderer uses the generated alpha-bearing pixels in
source/ui/shell_icons.h. Regenerate/verify them with
`python3 tools/generate_shell_icons.py [--check]`; no image decoder or Python is
required on Switch. The original PNGs are retained here for source provenance.

The blue/green gradients, background and project mark are original native
geometry. No XP.css artwork/code is imported in this package, and there is no
CSS runtime. This is an XP-like arrangement rather than Microsoft artwork.
Fonts are the existing Liberation files under romfs/fonts, with OFL.txt. Glyphs
outside those fonts' coverage may display the font's missing-glyph symbol.
