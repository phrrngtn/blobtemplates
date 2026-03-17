# blobtemplates

C/C++ library exposing Inja templating, JMESPath queries, JSON diff/patch, JSON reshaping, text diffing, and YAML conversion as scalar SQL functions for SQLite and DuckDB, with Python bindings.

> **Note:** This code is almost entirely AI-authored (Claude, Anthropic), albeit under close human supervision, and is for research and experimentation purposes. Successful experiments may be re-implemented in a more coordinated and curated manner.

## What it does

A shared C core (`src/blobtemplates_core.cpp`) implements all functionality, with thin wrappers for each target:

- **SQLite extension** -- loadable extension registering all `bt_*` scalar functions
- **DuckDB extension** -- C API extension registering all `bt_*` scalar functions
- **Python module** -- `blobtemplates.render()` and `blobtemplates.render_with_options()`
- **C example** -- standalone demo program

## Function reference

All SQL functions use the `bt_` prefix. Both the SQLite and DuckDB extensions register the same set of functions.

### Templating

| Function | Description |
|----------|-------------|
| `bt_template_render(template, json)` | Render an Inja/Jinja2-style template against a JSON object |
| `bt_template_render(template, json, options)` | Same, with a JSON options argument to override delimiters |

The options JSON supports these keys:

| Key | Default | Description |
|-----|---------|-------------|
| `expression` | `["{{", "}}"]` | Expression delimiters |
| `statement` | `["{%", "%}"]` | Statement delimiters |
| `comment` | `["{#", "#}"]` | Comment delimiters |
| `line_statement` | `"##"` | Line statement prefix |

### JMESPath

| Function | Description |
|----------|-------------|
| `bt_jmespath(json, expression)` | Evaluate a JMESPath expression against a JSON document |

Custom JMESPath functions available within expressions:

| Custom function | Description |
|-----------------|-------------|
| `zip_arrays` | Combine parallel arrays into an array of tuples |
| `unzip_arrays` | Inverse of `zip_arrays` |
| `to_entries` | Convert an object to an array of `{"key":..., "value":...}` pairs |
| `from_entries` | Convert an array of key/value pairs back to an object |

### JSON diff and patch

Two independent implementations are provided: one using jsoncons and one using nlohmann/json.

| Function | Library | Description |
|----------|---------|-------------|
| `bt_json_from_diff(source, target)` | jsoncons | Compute a JSON Merge Patch from source to target |
| `bt_json_apply_patch(document, patch)` | jsoncons | Apply a JSON Merge Patch to a document |
| `bt_json_diff(source, target)` | nlohmann | Compute a JSON Patch (RFC 6902) from source to target |
| `bt_json_patch(document, patch)` | nlohmann | Apply a JSON Patch (RFC 6902) to a document |

### JSON reshaping

| Function | Description |
|----------|-------------|
| `bt_json_flatten(json)` | Flatten a nested JSON object to a single-level object with JSON Pointer keys |
| `bt_json_unflatten(json)` | Reverse of `bt_json_flatten` |
| `bt_json_nest(json, spec)` | Restructure a flat JSON object into a nested shape according to a spec |

### Text diff

| Function | Description |
|----------|-------------|
| `bt_text_diff(old_text, new_text)` | Produce a unified diff between two text strings |
| `bt_text_diff(old_text, new_text, label_old, label_new)` | Same, with custom labels for the file headers |

### YAML

| Function | Description |
|----------|-------------|
| `bt_yaml_to_json(yaml)` | Convert a YAML string to JSON |

## Dependencies

All fetched automatically via CMake FetchContent:

| Library | Version | Purpose |
|---------|---------|---------|
| [Inja](https://github.com/pantor/inja) | v3.4.0 | Jinja2-style template engine |
| [nlohmann/json](https://github.com/nlohmann/json) | v3.11.3 | JSON parsing, RFC 6902 diff/patch |
| [jsoncons](https://github.com/danielaparker/jsoncons) | v1.1.0 | JMESPath, JSON Merge Patch, flatten/unflatten |
| [dtl](https://github.com/cubicdaiya/dtl) | v1.21 | Diff template library for unified text diffs |
| [rapidyaml](https://github.com/biojppm/rapidyaml) | v0.11.0 | YAML parsing |

## Building

Requires CMake 3.20+ and a C++17 compiler.

```bash
# Core library + C example only
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build

# With all extensions
cmake -B build -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_SQLITE_EXTENSION=ON \
  -DBUILD_DUCKDB_EXTENSION=ON \
  -DBUILD_PYTHON_BINDINGS=ON
cmake --build build
```

### CMake options

| Option | Default | Description |
|--------|---------|-------------|
| `BUILD_SQLITE_EXTENSION` | `OFF` | Build SQLite loadable extension |
| `BUILD_DUCKDB_EXTENSION` | `OFF` | Build DuckDB loadable extension |
| `BUILD_PYTHON_BINDINGS` | `OFF` | Build nanobind Python module |

### Python bindings

Requires [nanobind](https://github.com/wjakob/nanobind) (`pip install nanobind`).

```bash
cmake -B build -DBUILD_PYTHON_BINDINGS=ON
cmake --build build
PYTHONPATH=python python3 -c "
from blobtemplates import render
print(render('Hello {{ name }}!', '{\"name\": \"World\"}'))
"
```

## Usage examples

### SQLite

```bash
sqlite3 ':memory:' -cmd '.load build/sqlite/blobtemplates' \
  "SELECT bt_template_render('{{ x }} + {{ y }} = {{ x + y }}', '{\"x\": 1, \"y\": 2}');"
```

### DuckDB

```bash
duckdb -unsigned -c "
  LOAD 'build/duckdb/blobtemplates.duckdb_extension';
  SELECT bt_template_render('Hello {{ name }}!', json_object('name', name))
  FROM (VALUES ('Alice'), ('Bob'), ('Charlie')) AS t(name);
"
```

### Python

```python
from blobtemplates import render, render_with_options

render('{% for x in items %}{{ x }}, {% endfor %}done', '{"items": ["a","b","c"]}')
# 'a, b, c, done'

render_with_options('<<val>>', '{"val": 42}', '{"expression": ["<<", ">>"]}')
# '42'
```

## Architecture

```
blobtemplates/
├── include/blobtemplates.h              # C API
├── src/blobtemplates_core.cpp           # Core wrapper + thread-safe LRU cache
├── sqlite_ext/src/blobtemplates_sqlite.c  # SQLite extension
├── duckdb_ext/src/blobtemplates_duckdb.c  # DuckDB C extension
├── python/bindings.cpp                  # nanobind bindings
├── example/main.c                       # C demo
└── CMakeLists.txt
```

The core wrapper caches parsed templates in a process-wide LRU cache (capped at 1024 entries) protected by a readers-writer lock. The SQLite extension additionally uses `sqlite3_set_auxdata` / `sqlite3_get_auxdata` for per-statement caching of parsed templates, environments, and compiled JMESPath expressions.

## Prior art

Based on [phrrngtn/sqlite-template-inja](https://github.com/phrrngtn/sqlite-template-inja), a minimal SQLite extension for Inja. This project extends that idea with a shared C core, DuckDB and Python bindings, and a proper caching layer.

## License

MIT
