SET search_path = public;

DROP OPERATOR ~~% (text, text) CASCADE;
DROP SCHEMA senna CASCADE;
DROP TYPE senquery CASCADE;

DROP FUNCTION senna_insert(internal);
DROP FUNCTION senna_beginscan(internal);
DROP FUNCTION senna_gettuple(internal);
DROP FUNCTION senna_getbitmap(internal);
DROP FUNCTION senna_getmulti(internal);
DROP FUNCTION senna_rescan(internal);
DROP FUNCTION senna_endscan(internal);
DROP FUNCTION senna_build(internal);
DROP FUNCTION senna_bulkdelete(internal);
DROP FUNCTION senna_vacuumcleanup(internal);
DROP FUNCTION senna_costestimate(internal);
DROP FUNCTION senna_options(internal);

-- should delete the senna AM after drop functions.
DELETE FROM pg_catalog.pg_am WHERE amname = 'senna';

DROP FUNCTION pg_sync_file(text);
