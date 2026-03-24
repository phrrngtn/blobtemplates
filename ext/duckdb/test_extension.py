"""Smoke test: load the blobtemplates DuckDB extension and verify functions exist."""
import duckdb
from blobtemplates_duckdb import extension_path

EXPECTED_FUNCTIONS = [
    "bt_template_render",
    "bt_jmespath",
    "bt_json_diff",
    "bt_json_patch",
    "bt_json_flatten",
    "bt_json_unflatten",
    "bt_yaml_to_json",
    "bt_json_nest",
    "bt_text_diff",
]

conn = duckdb.connect()
conn.execute("SET allow_unsigned_extensions = true")
conn.execute(f"LOAD '{extension_path()}'")

registered = set(
    row[0]
    for row in conn.execute(
        "SELECT DISTINCT function_name FROM duckdb_functions() WHERE function_name LIKE 'bt_%'"
    ).fetchall()
)

missing = [f for f in EXPECTED_FUNCTIONS if f not in registered]
if missing:
    raise AssertionError(f"Missing functions: {missing}")

print(f"OK: {len(registered)} bt_* functions registered, all {len(EXPECTED_FUNCTIONS)} expected functions present")
