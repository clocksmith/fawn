#!/usr/bin/env python3
"""Package the unified artwork for file: launch without cross-file module access."""
from __future__ import annotations

import argparse
import base64
import json
from pathlib import Path
import re
import shutil

RESOURCES = Path(__file__).resolve().parents[1] / 'resources'
IMPORT = re.compile(r"(\bfrom\s*|\bimport\s*)(['\"])(\./[^'\"]+)\2")


def package(output: Path) -> None:
    if output.resolve() == RESOURCES.resolve():
        raise ValueError('Package output must differ from authored resources')
    modules: dict[str, str] = {}

    def visit(name: str) -> None:
        if name in modules:
            return
        if Path(name).name != name or not name.startswith('fawn-') or not name.endswith('.js'):
            raise ValueError(f'Unexpected artwork module: {name}')
        source = (RESOURCES / name).read_text()
        modules[name] = ''

        def resolve(match: re.Match[str]) -> str:
            dependency = match[3][2:]
            visit(dependency)
            return match[1] + match[2] + dependency + match[2]

        linked = IMPORT.sub(resolve, source)
        modules[name] = 'data:text/javascript;base64,' + base64.b64encode(linked.encode()).decode()

    visit('fawn-cycle.js')
    html = (RESOURCES / 'fawn-cycle.html').read_text()
    html = html.replace('<script src="./fawn-formation.js"></script>',
                        '<script>\n' + (RESOURCES / 'fawn-formation.js').read_text() + '</script>')
    entry = '<script type="module" src="./fawn-cycle.js"></script>'
    if entry not in html:
        raise ValueError('Unified artwork module entry is missing')
    html = html.replace(entry, '<script type="importmap">' + json.dumps({'imports': modules})
                        + '</script>\n<script type="module">import "fawn-cycle.js";</script>')
    output.mkdir(parents=True, exist_ok=True)
    (output / 'fawn-cycle.html').write_text(html)
    shutil.copy2(RESOURCES / 'fawn-icon.svg', output / 'fawn-icon.svg')
    obsolete = ['fawn-start.html', 'fawn-heavy-particles.html', 'fawn-image-lab.html',
                'fawn-magnetic-fluids.html', 'fawn-prismatic-fluids.html', 'fawn-shaders.js',
                'fawn-formation.js', *modules]
    for name in obsolete:
        (output / name).unlink(missing_ok=True)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    package(parser.parse_args().output)


if __name__ == '__main__':
    main()
