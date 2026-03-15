"""Smoke tests for the blobtemplates Python extension.

These verify that each function is callable and produces the expected output
for a trivial input. They are NOT testing correctness of the underlying C++
libraries (inja, jsoncons, nlohmann/json) — just that the bindings work.
"""

import json

import pytest

import blobtemplates


# ── Template rendering ──────────────────────────────────────────────


def test_render_basic():
    result = blobtemplates.render("Hello {{ name }}!", '{"name": "world"}')
    assert result == "Hello world!"


def test_render_with_options():
    result = blobtemplates.render_with_options(
        "Hello <% name %>!",
        '{"name": "world"}',
        '{"expression": ["<%", "%>"]}'
    )
    assert result == "Hello world!"


def test_render_bad_template():
    with pytest.raises(ValueError):
        blobtemplates.render("{{ unclosed", '{"x": 1}')


def test_render_bad_json():
    with pytest.raises(ValueError):
        blobtemplates.render("{{ x }}", "not json")


# ── JMESPath ────────────────────────────────────────────────────────


def test_jmespath_simple():
    doc = json.dumps({"a": {"b": [1, 2, 3]}})
    result = blobtemplates.jmespath_search(doc, "a.b[1]")
    assert json.loads(result) == 2


def test_jmespath_array_projection():
    doc = json.dumps({"people": [{"name": "Alice"}, {"name": "Bob"}]})
    result = blobtemplates.jmespath_search(doc, "people[*].name")
    assert json.loads(result) == ["Alice", "Bob"]


def test_jmespath_null_result():
    result = blobtemplates.jmespath_search('{"a": 1}', "b")
    assert result is None


def test_jmespath_bad_expression():
    with pytest.raises(ValueError):
        blobtemplates.jmespath_search('{"a": 1}', "[invalid!!")


# ── JMESPath custom functions: zip_arrays ─────────────────────────


def test_zip_arrays_basic():
    """Two parallel arrays → array of objects."""
    doc = json.dumps({"a": [1, 2, 3], "b": ["x", "y", "z"]})
    result = json.loads(blobtemplates.jmespath_search(doc, "zip_arrays(@)"))
    assert len(result) == 3
    assert result[0] == {"a": 1, "b": "x"}
    assert result[2] == {"a": 3, "b": "z"}


def test_zip_arrays_with_projection():
    """zip_arrays + multi-select hash for field renaming."""
    doc = json.dumps({
        "time": ["2025-01-01", "2025-01-02"],
        "temperature_2m_mean": [44.0, 36.5],
    })
    result = json.loads(blobtemplates.jmespath_search(
        doc, "zip_arrays(@)[].{date: time, temp_f: temperature_2m_mean}"
    ))
    assert result == [
        {"date": "2025-01-01", "temp_f": 44.0},
        {"date": "2025-01-02", "temp_f": 36.5},
    ]


def test_zip_arrays_nested():
    """zip_arrays on a nested object (Open-Meteo daily pattern)."""
    doc = json.dumps({
        "daily": {
            "time": ["2025-01-01", "2025-01-02"],
            "temperature_2m_mean": [44.0, 36.5],
        }
    })
    result = json.loads(blobtemplates.jmespath_search(
        doc, "zip_arrays(daily)[].{date: time, temp_f: temperature_2m_mean}"
    ))
    assert len(result) == 2
    assert result[0]["date"] == "2025-01-01"


def test_zip_arrays_three_columns():
    """Three parallel arrays."""
    doc = json.dumps({"x": [1, 2], "y": [3, 4], "z": [5, 6]})
    result = json.loads(blobtemplates.jmespath_search(doc, "zip_arrays(@)"))
    assert len(result) == 2
    assert result[0] == {"x": 1, "y": 3, "z": 5}


def test_zip_arrays_empty():
    """Empty arrays produce empty result."""
    doc = json.dumps({"a": [], "b": []})
    result = json.loads(blobtemplates.jmespath_search(doc, "zip_arrays(@)"))
    assert result == []


def test_zip_arrays_not_object():
    """Non-object input raises ValueError."""
    with pytest.raises(ValueError):
        blobtemplates.jmespath_search("[1,2,3]", "zip_arrays(@)")


# ── JMESPath custom functions: to_entries ─────────────────────────


def test_to_entries_basic():
    """Object → [{key, value}, ...]."""
    doc = json.dumps({"USD": 1.0, "EUR": 0.92})
    result = json.loads(blobtemplates.jmespath_search(doc, "to_entries(@)"))
    keys = {e["key"] for e in result}
    assert keys == {"USD", "EUR"}
    usd = [e for e in result if e["key"] == "USD"][0]
    assert usd["value"] == 1.0


def test_to_entries_with_filter():
    """to_entries + JMESPath filter."""
    doc = json.dumps({"a": 1, "b": 2, "c": 3})
    result = json.loads(blobtemplates.jmespath_search(
        doc, "to_entries(@)[?value > `1`].key"
    ))
    assert set(result) == {"b", "c"}


def test_to_entries_empty():
    doc = json.dumps({})
    result = json.loads(blobtemplates.jmespath_search(doc, "to_entries(@)"))
    assert result == []


def test_to_entries_not_object():
    """Non-object input raises ValueError."""
    with pytest.raises(ValueError):
        blobtemplates.jmespath_search("[1,2,3]", "to_entries(@)")


# ── JMESPath custom functions: unzip_arrays (inverse of zip_arrays) ──


def test_unzip_arrays_basic():
    """Array of objects → object of parallel arrays."""
    doc = json.dumps([{"a": 1, "b": "x"}, {"a": 2, "b": "y"}])
    result = json.loads(blobtemplates.jmespath_search(doc, "unzip_arrays(@)"))
    assert result == {"a": [1, 2], "b": ["x", "y"]}


def test_unzip_arrays_roundtrip():
    """zip_arrays(unzip_arrays(x)) == x for row-oriented input."""
    doc = json.dumps([{"a": 1, "b": "x"}, {"a": 2, "b": "y"}])
    columnar = blobtemplates.jmespath_search(doc, "unzip_arrays(@)")
    roundtripped = blobtemplates.jmespath_search(columnar, "zip_arrays(@)")
    assert json.loads(roundtripped) == json.loads(doc)


def test_zip_arrays_roundtrip():
    """unzip_arrays(zip_arrays(x)) == x for columnar input."""
    doc = json.dumps({"a": [1, 2, 3], "b": ["x", "y", "z"]})
    rows = blobtemplates.jmespath_search(doc, "zip_arrays(@)")
    roundtripped = blobtemplates.jmespath_search(rows, "unzip_arrays(@)")
    assert json.loads(roundtripped) == json.loads(doc)


def test_unzip_arrays_empty():
    doc = json.dumps([])
    result = json.loads(blobtemplates.jmespath_search(doc, "unzip_arrays(@)"))
    assert result == {}


def test_unzip_arrays_missing_keys():
    """Missing keys in later elements produce null."""
    doc = json.dumps([{"a": 1, "b": 2}, {"a": 3}])
    result = json.loads(blobtemplates.jmespath_search(doc, "unzip_arrays(@)"))
    assert result["a"] == [1, 3]
    assert result["b"] == [2, None]


def test_unzip_arrays_not_array():
    with pytest.raises(ValueError):
        blobtemplates.jmespath_search('{"a": 1}', "unzip_arrays(@)")


# ── JMESPath custom functions: from_entries (inverse of to_entries) ──


def test_from_entries_basic():
    """[{key, value}, ...] → object."""
    doc = json.dumps([{"key": "a", "value": 1}, {"key": "b", "value": 2}])
    result = json.loads(blobtemplates.jmespath_search(doc, "from_entries(@)"))
    assert result == {"a": 1, "b": 2}


def test_from_entries_roundtrip():
    """from_entries(to_entries(x)) == x."""
    original = {"x": 1, "y": "hello", "z": [1, 2]}
    entries = blobtemplates.jmespath_search(json.dumps(original), "to_entries(@)")
    roundtripped = json.loads(blobtemplates.jmespath_search(entries, "from_entries(@)"))
    assert roundtripped == original


def test_to_entries_roundtrip():
    """to_entries(from_entries(x)) produces same entries."""
    original = [{"key": "a", "value": 1}, {"key": "b", "value": 2}]
    obj = blobtemplates.jmespath_search(json.dumps(original), "from_entries(@)")
    roundtripped = json.loads(blobtemplates.jmespath_search(obj, "to_entries(@)"))
    assert {e["key"]: e["value"] for e in roundtripped} == {"a": 1, "b": 2}


def test_from_entries_skips_malformed():
    """Elements without key/value fields are silently skipped."""
    doc = json.dumps([
        {"key": "a", "value": 1},
        {"nope": "bad"},
        {"key": "b", "value": 2},
    ])
    result = json.loads(blobtemplates.jmespath_search(doc, "from_entries(@)"))
    assert result == {"a": 1, "b": 2}


def test_from_entries_empty():
    result = json.loads(blobtemplates.jmespath_search("[]", "from_entries(@)"))
    assert result == {}


def test_from_entries_not_array():
    with pytest.raises(ValueError):
        blobtemplates.jmespath_search('{"a": 1}', "from_entries(@)")


# ── JSON diff/patch (jsoncons) ──────────────────────────────────────


def test_json_from_diff():
    source = json.dumps({"a": 1, "b": 2})
    target = json.dumps({"a": 1, "b": 3, "c": 4})
    patch = json.loads(blobtemplates.json_from_diff(source, target))
    assert isinstance(patch, list)
    assert len(patch) > 0
    ops = {op["op"] for op in patch}
    assert ops <= {"add", "remove", "replace"}


def test_json_apply_patch():
    doc = json.dumps({"a": 1})
    patch = json.dumps([{"op": "add", "path": "/b", "value": 2}])
    result = json.loads(blobtemplates.json_apply_patch(doc, patch))
    assert result == {"a": 1, "b": 2}


def test_json_roundtrip_jsoncons():
    """from_diff then apply_patch should reconstruct the target."""
    source = json.dumps({"x": [1, 2], "y": "hello"})
    target = json.dumps({"x": [1, 3], "z": True})
    patch = blobtemplates.json_from_diff(source, target)
    result = json.loads(blobtemplates.json_apply_patch(source, patch))
    assert result == {"x": [1, 3], "z": True}


# ── JSON diff/patch (nlohmann) ──────────────────────────────────────


def test_json_diff():
    source = json.dumps({"a": 1})
    target = json.dumps({"a": 2})
    patch = json.loads(blobtemplates.json_diff(source, target))
    assert isinstance(patch, list)
    assert patch[0]["op"] == "replace"


def test_json_patch():
    source = json.dumps({"a": 1})
    patch = json.dumps([{"op": "replace", "path": "/a", "value": 99}])
    result = json.loads(blobtemplates.json_patch(source, patch))
    assert result == {"a": 99}


def test_json_roundtrip_nlohmann():
    """diff then patch should reconstruct the target."""
    source = json.dumps({"items": [1, 2, 3]})
    target = json.dumps({"items": [1, 2, 4]})
    patch = blobtemplates.json_diff(source, target)
    result = json.loads(blobtemplates.json_patch(source, patch))
    assert result == {"items": [1, 2, 4]}


# ── JSON flatten/unflatten ──────────────────────────────────────────


def test_json_flatten():
    doc = json.dumps({"a": {"b": 1, "c": [2, 3]}})
    flat = json.loads(blobtemplates.json_flatten(doc))
    assert flat["/a/b"] == 1
    assert flat["/a/c/0"] == 2
    assert flat["/a/c/1"] == 3


def test_json_unflatten():
    flat = json.dumps({"/a/b": 1, "/a/c/0": 2, "/a/c/1": 3})
    nested = json.loads(blobtemplates.json_unflatten(flat))
    assert nested == {"a": {"b": 1, "c": [2, 3]}}


def test_flatten_unflatten_roundtrip():
    original = {"top": {"mid": {"leaf": "value"}, "list": [10, 20]}}
    flat = blobtemplates.json_flatten(json.dumps(original))
    restored = json.loads(blobtemplates.json_unflatten(flat))
    assert restored == original
