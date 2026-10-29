-- Adapted from PostgreSQL src/test/regress/sql/btree_index.sql
--
-- DB coverage:
--   * ascending/descending index builds and low/high/equality retrieval
--   * composite range bounds
--   * duplicate-key posting lists and targeted deletion
--   * unique checking, including multiple NULL keys
--   * index cleanup after DELETE + VACUUM
--
-- PostgreSQL-only RowCompare, catalog/opclass, bitmap, LIKE, expression,
-- partitioned, parallel-build, and multilevel page-deletion cases are omitted.

CREATE TABLE bt_i4_heap (seqno INT, random INT);
CREATE TABLE bt_name_heap (seqno CHAR(8), random INT);
CREATE TABLE bt_txt_heap (seqno CHAR(8), random INT);
CREATE TABLE bt_f8_heap (seqno FLOAT, random INT);

COPY bt_i4_heap FROM 'data/btree_desc.data';
COPY bt_name_heap FROM 'data/btree_asc.data';
COPY bt_txt_heap FROM 'data/btree_desc.data';
COPY bt_f8_heap FROM 'data/btree_asc.data';

SELECT COUNT(*) AS loaded_rows FROM bt_i4_heap;

ANALYZE bt_i4_heap;
ANALYZE bt_name_heap;
ANALYZE bt_txt_heap;
ANALYZE bt_f8_heap;

-- Descending input repeatedly creates new low keys; ascending input creates
-- new high keys. The 2048-row fixture forces repeated leaf splits.
CREATE INDEX bt_i4_index ON bt_i4_heap (seqno);
CREATE INDEX bt_name_index ON bt_name_heap (seqno);
CREATE INDEX bt_txt_index ON bt_txt_heap (seqno);
CREATE INDEX bt_f8_index ON bt_f8_heap (seqno);

SELECT COUNT(*) AS loaded_rows FROM bt_i4_heap;

SELECT b.* FROM bt_i4_heap b WHERE b.seqno < 1;
SELECT b.* FROM bt_i4_heap b WHERE b.seqno >= 2047;
SELECT b.* FROM bt_i4_heap b WHERE b.seqno = 450;

SELECT b.* FROM bt_name_heap b WHERE b.seqno < '1';
SELECT b.* FROM bt_name_heap b WHERE b.seqno >= '999';
SELECT b.* FROM bt_name_heap b WHERE b.seqno = '450';

SELECT b.* FROM bt_txt_heap b WHERE b.seqno < '1';
SELECT b.* FROM bt_txt_heap b WHERE b.seqno >= '999';
SELECT b.* FROM bt_txt_heap b WHERE b.seqno = '450';

SELECT b.* FROM bt_f8_heap b WHERE b.seqno < 1.0;
SELECT b.* FROM bt_f8_heap b WHERE b.seqno >= 2047.0;
SELECT b.* FROM bt_f8_heap b WHERE b.seqno = 450.0;

-- Composite bounds and scalar-array-style IN qualification.
CREATE TABLE bt_multi (major INT, minor INT, payload INT);
INSERT INTO bt_multi VALUES
    (1, 1, 101), (1, 3, 103), (1, 5, 105), (1, 7, 107),
    (2, 1, 201), (2, 2, 202), (2, 3, 203), (2, 4, 204),
    (2, 5, 205), (2, 6, 206), (2, 7, 207), (2, 8, 208),
    (3, 1, 301), (3, 3, 303), (3, 5, 305), (3, 7, 307);
CREATE INDEX bt_multi_index ON bt_multi (major, minor);
SELECT major, minor, payload FROM bt_multi
 WHERE major = 2 AND minor >= 3 AND minor < 7 ORDER BY minor;
SELECT major, minor FROM bt_multi
 WHERE major IN (1, 3) AND minor = 5 ORDER BY major;

-- Duplicate-key posting-list behavior.
CREATE TABLE bt_dup (k INT, payload INT);
CREATE INDEX bt_dup_index ON bt_dup (k);
INSERT INTO bt_dup VALUES
    (7, 0),
    (7, 1),
    (7, 2),
    (7, 3),
    (7, 4),
    (7, 5),
    (7, 6),
    (7, 7),
    (7, 8),
    (7, 9),
    (7, 10),
    (7, 11),
    (7, 12),
    (7, 13),
    (7, 14),
    (7, 15),
    (7, 16),
    (7, 17),
    (7, 18),
    (7, 19),
    (7, 20),
    (7, 21),
    (7, 22),
    (7, 23),
    (7, 24),
    (7, 25),
    (7, 26),
    (7, 27),
    (7, 28),
    (7, 29),
    (7, 30),
    (7, 31),
    (7, 32),
    (7, 33),
    (7, 34),
    (7, 35),
    (7, 36),
    (7, 37),
    (7, 38),
    (7, 39),
    (7, 40),
    (7, 41),
    (7, 42),
    (7, 43),
    (7, 44),
    (7, 45),
    (7, 46),
    (7, 47),
    (7, 48),
    (7, 49),
    (7, 50),
    (7, 51),
    (7, 52),
    (7, 53),
    (7, 54),
    (7, 55),
    (7, 56),
    (7, 57),
    (7, 58),
    (7, 59),
    (7, 60),
    (7, 61),
    (7, 62),
    (7, 63);
SELECT COUNT(*) AS duplicate_count FROM bt_dup WHERE k = 7;
DELETE FROM bt_dup WHERE k = 7 AND payload = 5;
SELECT COUNT(*) AS duplicate_count FROM bt_dup WHERE k = 7;
SELECT payload FROM bt_dup WHERE k = 7 AND payload >= 60 ORDER BY payload;

-- Unique checking follows PostgreSQL NULL semantics.
CREATE TABLE bt_unique (k INT);
CREATE UNIQUE INDEX bt_unique_index ON bt_unique (k);
INSERT INTO bt_unique VALUES (1);
INSERT INTO bt_unique VALUES (1);
INSERT INTO bt_unique VALUES (NULL), (NULL);
SELECT k FROM bt_unique ORDER BY k NULLS LAST;

-- Vacuum must remove dead TIDs without losing neighboring keys.
DELETE FROM bt_i4_heap WHERE seqno >= 100 AND seqno < 200;
VACUUM bt_i4_heap;
SELECT seqno, random FROM bt_i4_heap WHERE seqno = 99;
SELECT seqno, random FROM bt_i4_heap WHERE seqno = 100;
SELECT seqno, random FROM bt_i4_heap WHERE seqno = 199;
SELECT seqno, random FROM bt_i4_heap WHERE seqno = 200;
