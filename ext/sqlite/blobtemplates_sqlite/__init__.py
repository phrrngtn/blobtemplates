"""Packaging wrapper for the blobtemplates SQLite extension."""

import pathlib
import sys

_HERE = pathlib.Path(__file__).parent


def extension_path() -> str:
    """Return the absolute path to the blobtemplates SQLite extension (without suffix).

    SQLite's .load command does not want the file extension:
        .load /path/returned/here/blobtemplates

    Usage from shell:
        sqlite3 -cmd ".load $(python -c \"import blobtemplates_sqlite; print(blobtemplates_sqlite.extension_path())\")"
    """
    # SQLite .load strips the suffix itself; return without it
    base = _HERE / "blobtemplates"
    # Check that at least one platform variant exists
    for suffix in (".so", ".dylib", ".dll"):
        if (base.parent / f"blobtemplates{suffix}").exists():
            return str(base)
    raise FileNotFoundError(f"Extension not found at {base}.*")
