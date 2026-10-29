-- SQL regression runner smoke test
CREATE TABLE values_test (id INT, label CHAR(5), active BOOL);
INSERT INTO values_test VALUES (2, 'a;b', TRUE), (10, NULL, FALSE);
SELECT id, label, active FROM values_test ORDER BY id;
SELECT id FROM values_test WHERE id > 99;
BEGIN;
INSERT INTO values_test VALUES (20, 'lost', TRUE);
SELECT missing FROM values_test;
COMMIT;
SELECT id FROM values_test ORDER BY id;
SHOW TABLES;
