#!/usr/bin/env python3
"""Package the Xbox Dashboard as its own Windows prerelease (user files excluded)."""
from release_title import main

if __name__ == '__main__':
    raise SystemExit(main(dashboard=True))
