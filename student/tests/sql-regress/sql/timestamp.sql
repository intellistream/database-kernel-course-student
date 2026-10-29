-- TIMESTAMP(p) uses PostgreSQL epoch microseconds and context-driven input.
CREATE TABLE timestamp_matrix (id INT, happened_at TIMESTAMP(3));
CREATE UNIQUE INDEX timestamp_matrix_time_key ON timestamp_matrix(happened_at);
INSERT INTO timestamp_matrix VALUES
  (1, '1999-12-31 23:59:59.9994'),
  (2, '2000-01-01 00:00:00.1236'),
  (3, '2000-01-02 00:00:00');
SELECT id, happened_at FROM timestamp_matrix
WHERE happened_at >= '2000-01-01 00:00:00'
ORDER BY happened_at;
SELECT CAST(happened_at AS CHAR(32)) AS rendered
FROM timestamp_matrix WHERE id = 2;
SELECT CAST('2023-02-29 00:00:00' AS TIMESTAMP);
