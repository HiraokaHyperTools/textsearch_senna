--
-- Server must be started with -O option (pg_ctl start -o "-O") to allow catalog modification.
-- Or, ERROR: System catalog modifications are currently disallowed.
--

SET search_path = public;

BEGIN;

CREATE FUNCTION pg_catalog.pg_sync_file(text) RETURNS void
	AS '$libdir/textsearch_senna', 'pg_sync_file'
	LANGUAGE C STRICT;

CREATE FUNCTION pg_catalog.senquery_in(cstring) RETURNS pg_catalog.senquery
	AS 'textin' LANGUAGE internal STRICT IMMUTABLE;

CREATE FUNCTION pg_catalog.senquery_out(pg_catalog.senquery) RETURNS cstring
	AS 'textout' LANGUAGE internal STRICT IMMUTABLE;

CREATE FUNCTION pg_catalog.senquery_recv(internal) RETURNS pg_catalog.senquery
	AS 'textin' LANGUAGE internal STRICT IMMUTABLE;

CREATE FUNCTION pg_catalog.senquery_send(pg_catalog.senquery) RETURNS bytea
	AS 'textout' LANGUAGE internal STRICT IMMUTABLE;

CREATE FUNCTION pg_catalog.char_senquery(char) RETURNS senquery
	AS 'char_text' LANGUAGE internal STRICT IMMUTABLE;

CREATE TYPE pg_catalog.senquery (
	INPUT = pg_catalog.senquery_in,
	OUTPUT = pg_catalog.senquery_out,
	RECEIVE = pg_catalog.senquery_recv,
	SEND = pg_catalog.senquery_send,
	INTERNALLENGTH = VARIABLE,
	ALIGNMENT = int4,
	STORAGE = extended
);

CREATE CAST (text AS pg_catalog.senquery) WITHOUT FUNCTION;
CREATE CAST (varchar AS pg_catalog.senquery) WITHOUT FUNCTION;
CREATE CAST (char AS pg_catalog.senquery) WITH FUNCTION pg_catalog.char_senquery(char);

CREATE FUNCTION pg_catalog.senna_to_tsvector(regconfig, text) RETURNS text
	AS 'SELECT $2'
	LANGUAGE sql IMMUTABLE STRICT;

CREATE FUNCTION pg_catalog.senna_to_tsvector(text) RETURNS text
	AS 'SELECT $1'
	LANGUAGE sql IMMUTABLE STRICT;

CREATE FUNCTION pg_catalog.senna_to_tsquery(regconfig, text) RETURNS senquery
	AS 'SELECT $2::senquery'
	LANGUAGE sql IMMUTABLE STRICT;

CREATE FUNCTION pg_catalog.senna_to_tsquery(text) RETURNS senquery
	AS 'SELECT $1::senquery'
	LANGUAGE sql IMMUTABLE STRICT;

CREATE FUNCTION pg_catalog.senna_drop_index(regclass) RETURNS void
	AS '$libdir/textsearch_senna', 'senna_drop_index'
	LANGUAGE C STRICT;

CREATE FUNCTION pg_catalog.senna_reindex_index(regclass) RETURNS void
	AS '$libdir/textsearch_senna', 'senna_reindex_index'
	LANGUAGE C STRICT;

CREATE FUNCTION pg_catalog.senna_contains(text, text) RETURNS bool
	AS '$libdir/textsearch_senna', 'senna_contains'
	LANGUAGE C IMMUTABLE STRICT;

CREATE FUNCTION pg_catalog.senna_contains(text, senquery) RETURNS bool
	AS '$libdir/textsearch_senna', 'senna_contains'
	LANGUAGE C IMMUTABLE STRICT;

CREATE FUNCTION pg_catalog.senna_contained(senquery, text) RETURNS bool
	AS '$libdir/textsearch_senna', 'senna_contained'
	LANGUAGE C IMMUTABLE STRICT;

CREATE FUNCTION pg_catalog.senna_restsel(internal, oid, internal, integer) RETURNS float8
	AS '$libdir/textsearch_senna', 'senna_restsel'
	LANGUAGE C IMMUTABLE STRICT;

CREATE OPERATOR pg_catalog.%% (
	PROCEDURE = pg_catalog.senna_contains,
	LEFTARG = text,
	RIGHTARG = text,
	RESTRICT = pg_catalog.senna_restsel
);

CREATE OPERATOR pg_catalog.@@ (
	PROCEDURE = pg_catalog.senna_contains,
	LEFTARG = text,
	RIGHTARG = pg_catalog.senquery,
	COMMUTATOR = OPERATOR (pg_catalog.@@),
	RESTRICT = pg_catalog.senna_restsel
);

CREATE OPERATOR pg_catalog.@@ (
	PROCEDURE = pg_catalog.senna_contained,
	LEFTARG = pg_catalog.senquery,
	RIGHTARG = text,
	COMMUTATOR = OPERATOR (pg_catalog.@@),
	RESTRICT = pg_catalog.senna_restsel
);

CREATE FUNCTION pg_catalog.senna_insert(internal) RETURNS bool AS '$libdir/textsearch_senna' LANGUAGE C;
CREATE FUNCTION pg_catalog.senna_beginscan(internal) RETURNS internal AS '$libdir/textsearch_senna' LANGUAGE C;
CREATE FUNCTION pg_catalog.senna_gettuple(internal) RETURNS bool AS '$libdir/textsearch_senna' LANGUAGE C;
CREATE FUNCTION pg_catalog.senna_getbitmap(internal) RETURNS int8 AS '$libdir/textsearch_senna' LANGUAGE C;
CREATE FUNCTION pg_catalog.senna_rescan(internal) RETURNS void AS '$libdir/textsearch_senna' LANGUAGE C;
CREATE FUNCTION pg_catalog.senna_endscan(internal) RETURNS void AS '$libdir/textsearch_senna' LANGUAGE C;
CREATE FUNCTION pg_catalog.senna_build(internal) RETURNS internal AS '$libdir/textsearch_senna' LANGUAGE C;
CREATE FUNCTION pg_catalog.senna_bulkdelete(internal) RETURNS internal AS '$libdir/textsearch_senna' LANGUAGE C;
CREATE FUNCTION pg_catalog.senna_vacuumcleanup(internal) RETURNS internal AS '$libdir/textsearch_senna' LANGUAGE C;
CREATE FUNCTION pg_catalog.senna_costestimate(internal) RETURNS internal AS '$libdir/textsearch_senna' LANGUAGE C;
CREATE FUNCTION pg_catalog.senna_options(internal) RETURNS internal AS '$libdir/textsearch_senna' LANGUAGE C;

INSERT INTO pg_catalog.pg_am VALUES(
	'senna',	-- amname
	4,			-- amstrategies
	0,			-- amsupport
	false,		-- amcanorder
	false,		-- amcanbackward
	false,		-- amcanunique
	false,		-- amcanmulticol
	false,		-- amoptionalkey
	false,		-- amindexnulls
	false,		-- amsearchnulls
	false,		-- amstorage
	false,		-- amclusterable
	0,			-- amkeytype
	'pg_catalog.senna_insert',
	'pg_catalog.senna_beginscan',
	'pg_catalog.senna_gettuple',
	'pg_catalog.senna_getbitmap',
	'pg_catalog.senna_rescan',
	'pg_catalog.senna_endscan',
	0,	-- ammarkpos,
	0,	-- amrestrpos,
	'pg_catalog.senna_build',
	'pg_catalog.senna_bulkdelete',
	'pg_catalog.senna_vacuumcleanup',
	'pg_catalog.senna_costestimate',
	'pg_catalog.senna_options'
);

CREATE OPERATOR CLASS pg_catalog.senna_ops DEFAULT FOR TYPE text USING senna AS
		OPERATOR 1 @@ (text, senquery),
		OPERATOR 2 %% (text, text)
;

-- same as LIKE (~~)
CREATE OPERATOR pg_catalog.~~% (
	PROCEDURE = pg_catalog.textlike,
	LEFTARG = text,
	RIGHTARG = text,
	COMMUTATOR = !~~,
	RESTRICT = pg_catalog.likesel,
	JOIN = pg_catalog.likejoinsel
);

CREATE OPERATOR CLASS pg_catalog.like_ops FOR TYPE text USING senna AS
		OPERATOR 3 ~~% (text, text),
		OPERATOR 4 ~~ (text, text)
;

CREATE VIEW pg_catalog.senna_all_files AS
  SELECT CASE WHEN NOT (pg_stat_file(dir)).isdir
         THEN dir
         ELSE dir || '/' || pg_ls_dir(dir)
         END AS file
    FROM (SELECT 'base/' || dir AS dir
            FROM pg_ls_dir('base') AS base(dir)
         UNION ALL
         (SELECT 'pg_tblspc/' || tbs || '/' || pg_ls_dir('pg_tblspc/' || tbs) AS dir
            FROM pg_ls_dir('pg_tblspc') AS tblspc(tbs))) AS t
   WHERE dir NOT LIKE '%/pgsql_tmp';

CREATE VIEW pg_catalog.senna_index_files AS
  SELECT file FROM pg_catalog.senna_all_files WHERE file LIKE '%.SEN%';

CREATE VIEW pg_catalog.senna_orphan_files AS
  SELECT file FROM (SELECT file, min(file) OVER
      (PARTITION BY substring(file from E'^(.*/[0-9]+)[^/]*$')) base
    FROM pg_catalog.senna_all_files) t
   WHERE file LIKE '%.SEN%'
     AND base LIKE '%.SEN%';

CREATE FUNCTION pg_catalog.senna_checkpoint() RETURNS void AS
$$SELECT pg_sync_file(file) FROM pg_catalog.senna_index_files$$
LANGUAGE sql;

COMMIT;
