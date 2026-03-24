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

registered = set(
    row[0]
    for row in conn.execute("SELECT name FROM pragma_function_list").fetchall()
)

missing = [f for f in EXPECTED_FUNCTIONS if f not in registered]
if missing:
    raise AssertionError(f"Missing functions: {missing}")

print(f"OK: all {len(EXPECTED_FUNCTIONS)} expected functions present in pragma_function_list")
