#!/usr/bin/env python3
"""Write the Miyoo credits: upstream credits.txt with the port named under the version line.

    port_credits.py <credits.txt> <out file> <port version>   (the version in CMakeLists.txt)
"""
import sys

src, out, version = sys.argv[1:4]
lines = open(src).read().split('\n')
at = next(i for i, line in enumerate(lines) if line.startswith('version '))
lines[at + 1:at + 1] = [f'Miyoo Mini Plus port {version}', 'by ZJSS']
open(out, 'w').write('\n'.join(lines))
print(f'credits written to {out}')
