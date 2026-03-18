-- blobtemplates SQLite extension — SQL smoke tests
-- Run: sqlite3 :memory: -cmd ".load build/sqlite/blobtemplates" < test/test_sqlite.sql

SELECT '=== bt_template_render ===' AS test;
SELECT bt_template_render('Hello {{ name }}!', '{"name":"World"}') AS r;

SELECT '=== bt_template_render (3-arg) ===' AS test;
SELECT bt_template_render('<% name %>', '{"name":"OK"}',
    '{"expression":["<%","%>"]}') AS r;

SELECT '=== bt_jmespath ===' AS test;
SELECT bt_jmespath('[{"a":1},{"a":2}]', '[?a>`1`]') AS r;

SELECT '=== bt_json_from_diff / bt_json_apply_patch ===' AS test;
SELECT bt_json_apply_patch('{"a":1}',
    bt_json_from_diff('{"a":1}', '{"a":1,"b":2}')) AS r;

SELECT '=== bt_json_diff / bt_json_patch ===' AS test;
SELECT bt_json_patch('{"a":1}',
    bt_json_diff('{"a":1}', '{"a":1,"c":3}')) AS r;

SELECT '=== bt_json_flatten / bt_json_unflatten ===' AS test;
SELECT bt_json_unflatten(bt_json_flatten('{"a":{"b":1}}')) AS r;

SELECT '=== bt_json_nest ===' AS test;
SELECT bt_json_nest(
    '[{"k":"x","v":1},{"k":"y","v":2}]', '["k"]') AS r;

SELECT '=== bt_yaml_to_json ===' AS test;
SELECT bt_yaml_to_json('name: test
value: 42') AS r;

SELECT '=== bt_text_diff ===' AS test;
SELECT bt_text_diff('line1', 'line2') AS r;

SELECT '=== bt_text_diff (4-arg) ===' AS test;
SELECT bt_text_diff('old', 'new', 'before', 'after') AS r;

SELECT '=== bt_json_patch_fold (aggregate) ===' AS test;
SELECT bt_json_patch_fold(val) AS r
FROM (
    SELECT 1 AS seq, '{"x":1}' AS val
    UNION ALL SELECT 2, '[{"op":"add","path":"/y","value":2}]'
    ORDER BY seq
);

SELECT '=== bt_json_patch_fold (window) ===' AS test;
SELECT seq, bt_json_patch_fold(val) OVER (
    ORDER BY seq ROWS BETWEEN UNBOUNDED PRECEDING AND CURRENT ROW
) AS r
FROM (
    SELECT 1 AS seq, '{"x":1}' AS val
    UNION ALL SELECT 2, '[{"op":"add","path":"/y","value":2}]'
    UNION ALL SELECT 3, '[{"op":"add","path":"/z","value":3}]'
)
ORDER BY seq;

SELECT '=== bt_json_patch_fold (partitioned window) ===' AS test;
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

SELECT '=== NULL handling ===' AS test;
SELECT bt_json_patch_fold(val) AS r
FROM (
    SELECT 1 AS seq, '{"a":1}' AS val
    UNION ALL SELECT 2, NULL
    UNION ALL SELECT 3, '[{"op":"add","path":"/b","value":2}]'
    ORDER BY seq
);

SELECT '=== All tests complete ===' AS test;
