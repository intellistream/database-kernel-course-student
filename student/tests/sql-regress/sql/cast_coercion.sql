-- Cast and coercion contexts follow PostgreSQL-style resolution.
CREATE TABLE cast_matrix (id INT, score FLOAT, label CHAR(8), enabled BOOL);
INSERT INTO cast_matrix VALUES ('7', 3, '123', TRUE);
SELECT id, score, CAST(label AS INT) AS parsed, CAST(enabled AS INT) AS enabled_int
FROM cast_matrix;
SELECT id + score AS total, id = 7.0 AS same_id
FROM cast_matrix
WHERE score > 2;
UPDATE cast_matrix SET id = 8.6 WHERE id = 7.0;
SELECT id, CAST(id AS CHAR(8)) AS id_text FROM cast_matrix;
SELECT CAST('not-an-int' AS INT);
SELECT id FROM cast_matrix WHERE id = enabled;
