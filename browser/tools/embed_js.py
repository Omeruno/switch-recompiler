"""Embed UTF-8 JavaScript without depending on a target-side filesystem."""
import json
import sys
from pathlib import Path

source, target = map(Path, sys.argv[1:])
target.parent.mkdir(parents=True, exist_ok=True)
target.write_text('static const char browser_dom[] =\n' + '\n'.join(
    json.dumps(line, ensure_ascii=False) for line in source.read_text().splitlines(True)
) + ';\n')
