"""Packaging wrapper for the blobtemplates DuckDB extension."""

import pathlib

_HERE = pathlib.Path(__file__).parent


def extension_path() -> str:
    """Return the absolute path to the blobtemplates DuckDB extension.

    Usage from SQL:
        LOAD '/path/returned/here/blobtemplates.duckdb_extension';

    Usage from shell:
        duckdb -cmd "LOAD '$(python -c \"import blobtemplates_duckdb; print(blobtemplates_duckdb.extension_path())\")'"
    """
    ext = _HERE / "blobtemplates.duckdb_extension"
    if not ext.exists():
        raise FileNotFoundError(f"Extension not found at {ext}")
    return str(ext)
