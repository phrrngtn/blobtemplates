-- blobtemplates DuckDB extension — SQL smoke tests
-- Run: duckdb -unsigned -c ".read test/test_duckdb.sql"
-- (after: LOAD 'build/duckdb/blobtemplates.duckdb_extension')

.print === bt_template_render ===
SELECT bt_template_render('Hello {{ name }}!', '{"name":"World"}') AS r;

.print === bt_template_render (3-arg with options) ===
SELECT bt_template_render('<% name %>', '{"name":"OK"}',
    '{"expression":["<%","%>"]}') AS r;

.print === bt_jmespath ===
SELECT bt_jmespath('[{"a":1},{"a":2}]', '[?a>`1`]') AS r;

.print === bt_json_from_diff / bt_json_apply_patch (jsoncons) ===
SELECT bt_json_apply_patch('{"a":1}',
    bt_json_from_diff('{"a":1}', '{"a":1,"b":2}')) AS r;

.print === bt_json_diff / bt_json_patch (nlohmann) ===
SELECT bt_json_patch('{"a":1}',
    bt_json_diff('{"a":1}', '{"a":1,"c":3}')) AS r;

.print === bt_json_flatten / bt_json_unflatten ===
SELECT bt_json_unflatten(bt_json_flatten('{"a":{"b":1}}')) AS r;

.print === bt_json_nest ===
SELECT bt_json_nest(
    '[{"k":"x","v":1},{"k":"y","v":2}]', '["k"]') AS r;

.print === bt_yaml_to_json ===
SELECT bt_yaml_to_json('name: test
value: 42') AS r;

.print === bt_text_diff (2-arg) ===
SELECT bt_text_diff('line1', 'line2') AS r;

.print === bt_text_diff (4-arg) ===
SELECT bt_text_diff('old', 'new', 'before', 'after') AS r;

.print === bt_json_patch_fold (aggregate) ===
SELECT bt_json_patch_fold(val ORDER BY seq) AS r
FROM (VALUES
    (1, '{"x":1}'),
    (2, '[{"op":"add","path":"/y","value":2}]')
) AS t(seq, val);

.print === bt_json_patch_fold (window) ===
SELECT seq, bt_json_patch_fold(val) OVER (
    ORDER BY seq ROWS BETWEEN UNBOUNDED PRECEDING AND CURRENT ROW
) AS r
FROM (VALUES
    (1, '{"x":1}'),
    (2, '[{"op":"add","path":"/y","value":2}]'),
    (3, '[{"op":"add","path":"/z","value":3}]')
) AS t(seq, val)
ORDER BY seq;

.print === bt_json_patch_fold (partitioned window) ===
WITH D AS (
    SELECT 'a' AS id, 1 AS seq, '{"v":1}' AS val
    UNION ALL SELECT 'a', 2, '[{"op":"replace","path":"/v","value":2}]'
    UNION ALL SELECT 'b', 1, '{"w":10}'
    UNION ALL SELECT 'b', 2, '[{"op":"replace","path":"/w","value":20}]'
)
SELECT id, seq, bt_json_patch_fold(val) OVER (
    PARTITION BY id ORDER BY seq
    ROWS BETWEEN UNBOUNDED PRECEDING AND CURRENT ROW
) AS r FROM D ORDER BY id, seq;

.print === NULL handling ===
SELECT bt_json_patch_fold(val ORDER BY seq) AS r
FROM (VALUES (1, '{"a":1}'), (2, NULL), (3, '[{"op":"add","path":"/b","value":2}]')) AS t(seq, val);

.print === All tests complete ===
