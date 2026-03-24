"""Smoke test: load the blobtemplates SQLite extension and verify functions exist."""
import sqlite3
from blobtemplates_sqlite import extension_path

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

conn = sqlite3.connect(":memory:")
conn.enable_load_extension(True)
conn.load_extension(extension_path())

for func in EXPECTED_FUNCTIONS:
    try:
        conn.execute(f"SELECT {func}('dummy')")
    except sqlite3.OperationalError as e:
        if "no such function" in str(e).lower():
            raise AssertionError(f"Function {func} not registered") from e

print(f"OK: all {len(EXPECTED_FUNCTIONS)} expected functions present")
