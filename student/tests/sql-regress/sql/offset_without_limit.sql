-- PostgreSQL limit.sql: OFFSET without LIMIT
CREATE TABLE offset_input (id INT);
INSERT INTO offset_input VALUES (1), (2), (3);

SELECT id FROM offset_input ORDER BY id OFFSET 1;
SELECT id FROM offset_input ORDER BY id OFFSET 99;
