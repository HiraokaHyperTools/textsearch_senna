\set ECHO all
SET client_encoding = utf8;
SET client_min_messages = notice;

SELECT senna.contains('あいう', senna.to_tsquery('あい'));
SELECT senna.contains('あいう', senna.to_tsquery('いう'));
SELECT senna.contains('あいう', senna.to_tsquery('え'));

SELECT 'あいう' %% 'あい';
SELECT 'あいう' @@ 'あい'::senquery;
SELECT 'あいう' @@ senna.to_tsquery('あい');
SELECT senna.to_tsvector('あいう') @@ senna.to_tsquery('あい');
SELECT senna.to_tsvector('english', 'あいう') @@ senna.to_tsquery('english', 'あい');

SELECT sum(CASE WHEN 'あい1う' %% ('あい' || (i % 4)) THEN 1 ELSE 0 END)  FROM generate_series(1, 1000) t(i);
SELECT sum(CASE WHEN 'あい1う' %% ('あい' || (i % 100)) THEN 1 ELSE 0 END)  FROM generate_series(1, 1000) t(i);

CREATE TABLE tbl (id int4, t text);
INSERT INTO tbl VALUES(1, 'あいう');
INSERT INTO tbl VALUES(2, 'いうえ');
INSERT INTO tbl VALUES(3, 'うえお');
INSERT INTO tbl VALUES(10, 'ABC');
INSERT INTO tbl VALUES(11, 'BCD');
INSERT INTO tbl VALUES(12, 'CDE');
INSERT INTO tbl VALUES(20, 'A B');
INSERT INTO tbl VALUES(21, E'A \\ B');
INSERT INTO tbl VALUES(22, 'A " B');
INSERT INTO tbl VALUES(23, 'A * B');
INSERT INTO tbl VALUES(24, 'A + B');
INSERT INTO tbl VALUES(25, 'A % B');
INSERT INTO tbl VALUES(26, 'A _ B');

CREATE INDEX idx_senna ON tbl USING senna (t) WITH (initial_n_segments = 4);
CREATE INDEX idx_like ON tbl USING senna (t like_ops) WITH (initial_n_segments = 2);

SELECT relname, reloptions FROM pg_class WHERE relname IN ('idx_senna', 'idx_like') ORDER BY relname;

SET enable_seqscan = on;
SET enable_indexscan = off;
SET enable_bitmapscan = off;
SELECT * FROM tbl WHERE t %% '不在' ORDER BY id;
SELECT * FROM tbl WHERE t %% 'いう' ORDER BY id;
SELECT * FROM tbl WHERE t ~~% '%不在%' ORDER BY id;
SELECT * FROM tbl WHERE t ~~% '%いう%' ORDER BY id;
SELECT * FROM tbl WHERE t ~~% 'あ%う' ORDER BY id;
SELECT * FROM tbl WHERE t ~~% '%BC%' ORDER BY id;
SELECT * FROM tbl WHERE t ~~% 'A%C' ORDER BY id;

SET enable_seqscan = off;
SET enable_indexscan = on;
SET enable_bitmapscan = off;
SELECT * FROM tbl WHERE t %% '不在' ORDER BY id;
SELECT * FROM tbl WHERE t %% 'いう' ORDER BY id;
SELECT * FROM tbl WHERE t ~~% '%不在%' ORDER BY id;
SELECT * FROM tbl WHERE t ~~% '%いう%' ORDER BY id;
SELECT * FROM tbl WHERE t ~~% 'あ%う' ORDER BY id;
SELECT * FROM tbl WHERE t ~~% '%BC%' ORDER BY id;
SELECT * FROM tbl WHERE t ~~% 'A%C' ORDER BY id;

SET enable_seqscan = off;
SET enable_indexscan = off;
SET enable_bitmapscan = on;
SELECT * FROM tbl WHERE t %% '不在' ORDER BY id;
SELECT * FROM tbl WHERE t %% 'いう' ORDER BY id;
SELECT * FROM tbl WHERE t ~~% '%不在%' ORDER BY id;
SELECT * FROM tbl WHERE t ~~% '%いう%' ORDER BY id;
SELECT * FROM tbl WHERE t ~~% 'あ%う' ORDER BY id;
SELECT * FROM tbl WHERE t ~~% '%BC%' ORDER BY id;
SELECT * FROM tbl WHERE t ~~% 'A%C' ORDER BY id;

INSERT INTO tbl VALUES(4, 'あいうえお');
INSERT INTO tbl VALUES(5, 'かきくけこ');
INSERT INTO tbl VALUES(14, 'ABCDE');
INSERT INTO tbl VALUES(15, 'abcde');

SET enable_seqscan = on;
SET enable_indexscan = off;
SET enable_bitmapscan = off;
SELECT * FROM tbl WHERE t %% '不在' ORDER BY id;
SELECT * FROM tbl WHERE t %% 'いう' ORDER BY id;
SELECT * FROM tbl WHERE t ~~% '%不在%' ORDER BY id;
SELECT * FROM tbl WHERE t ~~% '%いう%' ORDER BY id;
SELECT * FROM tbl WHERE t ~~% 'あ%う' ORDER BY id;
SELECT * FROM tbl WHERE t ~~% '%BC%' ORDER BY id;
SELECT * FROM tbl WHERE t ~~% 'A%C' ORDER BY id;

SET enable_seqscan = off;
SET enable_indexscan = on;
SET enable_bitmapscan = off;
SELECT * FROM tbl WHERE t %% '不在' ORDER BY id;
SELECT * FROM tbl WHERE t %% 'いう' ORDER BY id;
SELECT * FROM tbl WHERE t ~~% '%不在%' ORDER BY id;
SELECT * FROM tbl WHERE t ~~% '%いう%' ORDER BY id;
SELECT * FROM tbl WHERE t ~~% 'あ%う' ORDER BY id;
SELECT * FROM tbl WHERE t ~~% '%BC%' ORDER BY id;
SELECT * FROM tbl WHERE t ~~% 'A%C' ORDER BY id;

SET enable_seqscan = off;
SET enable_indexscan = off;
SET enable_bitmapscan = on;
SELECT * FROM tbl WHERE t %% '不在' ORDER BY id;
SELECT * FROM tbl WHERE t %% 'いう' ORDER BY id;
SELECT * FROM tbl WHERE t ~~% '%不在%' ORDER BY id;
SELECT * FROM tbl WHERE t ~~% '%いう%' ORDER BY id;
SELECT * FROM tbl WHERE t ~~% 'あ%う' ORDER BY id;
SELECT * FROM tbl WHERE t ~~% '%BC%' ORDER BY id;
SELECT * FROM tbl WHERE t ~~% 'A%C' ORDER BY id;

UPDATE tbl SET id = 999 WHERE id = 1;
UPDATE tbl SET t = 'さしす' WHERE id = 2;
UPDATE tbl SET t = 'いういう' WHERE id = 3;
UPDATE tbl SET t = 'AB CD' WHERE id = 12;
UPDATE tbl SET t = 'BCBC' WHERE id = 13;

SET enable_seqscan = on;
SET enable_indexscan = off;
SET enable_bitmapscan = off;
SELECT * FROM tbl WHERE t %% '不在' ORDER BY id;
SELECT * FROM tbl WHERE t %% 'いう' ORDER BY id;
SELECT * FROM tbl WHERE t ~~% '%不在%' ORDER BY id;
SELECT * FROM tbl WHERE t ~~% '%いう%' ORDER BY id;
SELECT * FROM tbl WHERE t ~~% 'あ%う' ORDER BY id;
SELECT * FROM tbl WHERE t ~~% '%BC%' ORDER BY id;
SELECT * FROM tbl WHERE t ~~% '%B C%' ORDER BY id;
SELECT * FROM tbl WHERE t ~~% 'A%C' ORDER BY id;

SET enable_seqscan = off;
SET enable_indexscan = on;
SET enable_bitmapscan = off;
SELECT * FROM tbl WHERE t %% '不在' ORDER BY id;
SELECT * FROM tbl WHERE t %% 'いう' ORDER BY id;
SELECT * FROM tbl WHERE t ~~% '%不在%' ORDER BY id;
SELECT * FROM tbl WHERE t ~~% '%いう%' ORDER BY id;
SELECT * FROM tbl WHERE t ~~% 'あ%う' ORDER BY id;
SELECT * FROM tbl WHERE t ~~% '%BC%' ORDER BY id;
SELECT * FROM tbl WHERE t ~~% '%B C%' ORDER BY id;
SELECT * FROM tbl WHERE t ~~% 'A%C' ORDER BY id;

SET enable_seqscan = off;
SET enable_indexscan = off;
SET enable_bitmapscan = on;
SELECT * FROM tbl WHERE t %% '不在' ORDER BY id;
SELECT * FROM tbl WHERE t %% 'いう' ORDER BY id;
SELECT * FROM tbl WHERE t ~~% '%不在%' ORDER BY id;
SELECT * FROM tbl WHERE t ~~% '%いう%' ORDER BY id;
SELECT * FROM tbl WHERE t ~~% 'あ%う' ORDER BY id;
SELECT * FROM tbl WHERE t ~~% '%BC%' ORDER BY id;
SELECT * FROM tbl WHERE t ~~% '%B C%' ORDER BY id;
SELECT * FROM tbl WHERE t ~~% 'A%C' ORDER BY id;

VACUUM ANALYZE tbl;

SET enable_seqscan = on;
SET enable_indexscan = off;
SET enable_bitmapscan = off;
SELECT * FROM tbl WHERE t %% '不在' ORDER BY id;
SELECT * FROM tbl WHERE t %% 'いう' ORDER BY id;
SELECT * FROM tbl WHERE t ~~% '%不在%' ORDER BY id;
SELECT * FROM tbl WHERE t ~~% '%いう%' ORDER BY id;
SELECT * FROM tbl WHERE t ~~% 'あ%う' ORDER BY id;
SELECT * FROM tbl WHERE t ~~% '%BC%' ORDER BY id;
SELECT * FROM tbl WHERE t ~~% 'A%C' ORDER BY id;

SET enable_seqscan = off;
SET enable_indexscan = on;
SET enable_bitmapscan = off;
SELECT * FROM tbl WHERE t %% '不在' ORDER BY id;
SELECT * FROM tbl WHERE t %% 'いう' ORDER BY id;
SELECT * FROM tbl WHERE t ~~% '%不在%' ORDER BY id;
SELECT * FROM tbl WHERE t ~~% '%いう%' ORDER BY id;
SELECT * FROM tbl WHERE t ~~% 'あ%う' ORDER BY id;
SELECT * FROM tbl WHERE t ~~% '%BC%' ORDER BY id;
SELECT * FROM tbl WHERE t ~~% 'A%C' ORDER BY id;

SET enable_seqscan = off;
SET enable_indexscan = off;
SET enable_bitmapscan = on;
SELECT * FROM tbl WHERE t %% '不在' ORDER BY id;
SELECT * FROM tbl WHERE t %% 'いう' ORDER BY id;
SELECT * FROM tbl WHERE t ~~% '%不在%' ORDER BY id;
SELECT * FROM tbl WHERE t ~~% '%いう%' ORDER BY id;
SELECT * FROM tbl WHERE t ~~% 'あ%う' ORDER BY id;
SELECT * FROM tbl WHERE t ~~% '%BC%' ORDER BY id;
SELECT * FROM tbl WHERE t ~~% 'A%C' ORDER BY id;

DELETE FROM tbl WHERE id = 3;

SELECT senna.reindex_index('idx_senna');
SELECT senna.reindex_index('idx_like');

SET enable_seqscan = on;
SET enable_indexscan = off;
SET enable_bitmapscan = off;
SELECT * FROM tbl WHERE t %% '不在' ORDER BY id;
SELECT * FROM tbl WHERE t %% 'いう' ORDER BY id;
SELECT * FROM tbl WHERE t ~~% '%不在%' ORDER BY id;
SELECT * FROM tbl WHERE t ~~% '%いう%' ORDER BY id;
SELECT * FROM tbl WHERE t ~~% 'あ%う' ORDER BY id;
SELECT * FROM tbl WHERE t ~~% '%BC%' ORDER BY id;
SELECT * FROM tbl WHERE t ~~% 'A%C' ORDER BY id;

SET enable_seqscan = off;
SET enable_indexscan = on;
SET enable_bitmapscan = off;
SELECT * FROM tbl WHERE t %% '不在' ORDER BY id;
SELECT * FROM tbl WHERE t %% 'いう' ORDER BY id;
SELECT * FROM tbl WHERE t ~~% '%不在%' ORDER BY id;
SELECT * FROM tbl WHERE t ~~% '%いう%' ORDER BY id;
SELECT * FROM tbl WHERE t ~~% 'あ%う' ORDER BY id;
SELECT * FROM tbl WHERE t ~~% '%BC%' ORDER BY id;
SELECT * FROM tbl WHERE t ~~% 'A%C' ORDER BY id;

SET enable_seqscan = off;
SET enable_indexscan = off;
SET enable_bitmapscan = on;
SELECT * FROM tbl WHERE t %% '不在' ORDER BY id;
SELECT * FROM tbl WHERE t %% 'いう' ORDER BY id;
SELECT * FROM tbl WHERE t ~~% '%不在%' ORDER BY id;
SELECT * FROM tbl WHERE t ~~% '%いう%' ORDER BY id;
SELECT * FROM tbl WHERE t ~~% 'あ%う' ORDER BY id;
SELECT * FROM tbl WHERE t ~~% '%BC%' ORDER BY id;
SELECT * FROM tbl WHERE t ~~% 'A%C' ORDER BY id;

SELECT count(*) FROM tbl WHERE t ~~% '%いう%'
UNION ALL
SELECT count(*) FROM tbl WHERE t ~~% '%いう%';

SELECT * FROM tbl WHERE t ~~% '%あい%' AND t ~~% '%お%';

SELECT * FROM tbl WHERE t ~~% '%A B%';
SELECT * FROM tbl WHERE t ~~% E'%A\\ B%';
SELECT * FROM tbl WHERE t ~~% E'%A \\\\ B%';
SELECT * FROM tbl WHERE t ~~% '%A " B%';
SELECT * FROM tbl WHERE t ~~% '%A * B%';
SELECT * FROM tbl WHERE t ~~% '%A + B%';
SELECT * FROM tbl WHERE t ~~% E'%A \\% B%';
SELECT * FROM tbl WHERE t ~~% E'%A \\_ B%';

-- search for empty keys
SELECT * FROM tbl WHERE t ~~% '';
SELECT * FROM tbl WHERE t ~~% '%';
SELECT * FROM tbl WHERE t ~~% '_';
SELECT * FROM tbl WHERE t ~~% '%%';
SELECT * FROM tbl WHERE t ~~% '%_%';
SELECT * FROM tbl WHERE t ~~% '"';
SELECT * FROM tbl WHERE t ~~% E'\\';
SELECT * FROM tbl WHERE t ~~% NULL;

SELECT senna.checkpoint();

SELECT senna.drop_index('idx_senna');
SELECT senna.drop_index('idx_like');

CHECKPOINT;

SELECT * FROM senna.orphan_files;
