SET search_path = public;

DROP OPERATOR ~~% (text, text) CASCADE;
DROP SCHEMA senna CASCADE;
DROP TYPE senquery CASCADE;

DROP FUNCTION IF EXISTS senna_insert(internal);
DROP FUNCTION IF EXISTS senna_beginscan(internal);
DROP FUNCTION IF EXISTS senna_gettuple(internal);
DROP FUNCTION IF EXISTS senna_getbitmap(internal);
DROP FUNCTION IF EXISTS senna_getmulti(internal);
DROP FUNCTION IF EXISTS senna_rescan(internal);
DROP FUNCTION IF EXISTS senna_endscan(internal);
DROP FUNCTION IF EXISTS senna_build(internal);
DROP FUNCTION IF EXISTS senna_buildempty(internal);
DROP FUNCTION IF EXISTS senna_bulkdelete(internal);
DROP FUNCTION IF EXISTS senna_vacuumcleanup(internal);
DROP FUNCTION IF EXISTS senna_costestimate(internal);
DROP FUNCTION IF EXISTS senna_options(internal);

-- should delete the senna AM after drop functions.
DELETE FROM pg_catalog.pg_am WHERE amname = 'senna';

DROP FUNCTION IF EXISTS pg_sync_file(text);
