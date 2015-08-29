/*
 * textsearch_senna.c
 *
 *	Copyright (c) 2009-2010, NIPPON TELEGRAPH AND TELEPHONE CORPORATION
 */
#include "postgres.h"

#include <senna/senna.h>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "access/genam.h"
#include "access/heapam.h"
#include "access/reloptions.h"
#include "access/relscan.h"
#include "access/xact.h"
#include "catalog/catalog.h"
#include "catalog/index.h"
#include "catalog/namespace.h"
#if PG_VERSION_NUM >= 90100
#include "catalog/pg_opclass.h"
#endif
#include "catalog/pg_type.h"
#include "commands/tablecmds.h"
#include "mb/pg_wchar.h"
#include "miscadmin.h"
#include "nodes/parsenodes.h"
#include "nodes/relation.h"
#include "parser/parsetree.h"
#if PG_VERSION_NUM >= 90100
#include "storage/smgr.h"
#endif

#if PG_VERSION_NUM >= 80300
#include "postmaster/syslogger.h"
#else
/* 8.2 does not install postmaster headers !? */
extern DLLIMPORT char *Log_directory;
#endif

#include "storage/fd.h"
#include "storage/lmgr.h"
#include "storage/bufmgr.h"
#include "utils/acl.h"
#include "utils/builtins.h"
#include "utils/inval.h"
#include "utils/lsyscache.h"
#include "utils/memutils.h"
#include "utils/relcache.h"
#include "utils/selfuncs.h"
#include "utils/syscache.h"

#include "pgut/pgut-be.h"

#if PG_VERSION_NUM >= 90100
#define RelationIsTemp(rel)	\
	((rel)->rd_rel->relpersistence == RELPERSISTENCE_TEMP)
#else
#define RelationIsTemp(rel)	 \
	((rel)->rd_istemp)
#endif

PG_MODULE_MAGIC;

/*
#define DEBUG_LEVEL		LOG
*/

#ifdef DEBUG_LEVEL
#define DEBUG_INDEX_CREATE(rnode) \
	elog(DEBUG_LEVEL, "senna: sen_index_create(%u/%u/%u)", (rnode).spcNode, (rnode).dbNode, (rnode).relNode)
#define DEBUG_INDEX_OPEN(rnode) \
	elog(DEBUG_LEVEL, "senna: sen_index_open(%u/%u/%u)", (rnode).spcNode, (rnode).dbNode, (rnode).relNode)
#define DEBUG_INDEX_CLOSE(rnode) \
	elog(DEBUG_LEVEL, "senna: sen_index_close(%u/%u/%u)", (rnode).spcNode, (rnode).dbNode, (rnode).relNode)
#define DEBUG_DELETE(message, ctid) \
	elog(DEBUG_LEVEL, "senna: delete with %s (%u, %u)", \
		message, \
		ItemPointerGetBlockNumber(ctid), \
		ItemPointerGetOffsetNumber(ctid))
#define DEBUG_RECORDS_CACHE(found) \
	elog(DEBUG_LEVEL, "senna: records is %scached", (found ? "" : "NOT "))
#else
#define DEBUG_INDEX_CREATE(rnode)		((void) 0)
#define DEBUG_INDEX_OPEN(rnode)			((void) 0)
#define DEBUG_INDEX_CLOSE(rnode)		((void) 0)
#define DEBUG_DELETE(message, ctid)		((void) 0)
#define DEBUG_RECORDS_CACHE(found)		((void) 0)
#endif

#define INDEX_CACHE_SIZE	4
#define QUERY_CACHE_SIZE	8
#define SENNA_MAX_N_EXPR	32
#define REMOVE_RETRY		300	/* 30s */

/*
 * VACUUM_USES_HEAP - read heap tuples during vacuum if defined.
 */
#define VACUUM_USES_HEAP

/*
 * RESTSEL_USES_SCAN - do actual scan during estimation.
 */
#define RESTSEL_USES_SCAN

typedef struct SennaOptions
{
	int32       vl_len_;	/* varlena header (do not touch directly!) */
	int32		initial_n_segments;
} SennaOptions;

typedef struct SennaIndexCache
{
	Oid				relid;
	RelFileNode		rnode;
	sen_index	   *index;
} SennaIndexCache;

typedef struct SennaQueryCache
{
	char		   *key;
	sen_query	   *query;
} SennaQueryCache;

typedef struct SennaRecordsCache
{
	sen_records	   *records;
	sen_query	   *query;
	Oid				relid;
} SennaRecordsCache;

typedef struct SennaScanDesc
{
	sen_records	   *records;
	bool			recheck;
#if NOT_USED
	sen_sort_optarg	sorter;
#endif
} SennaScanDesc;

typedef struct SennaBuildState
{
	sen_index	   *index;
} SennaBuildState;

PG_FUNCTION_INFO_V1(pg_sync_file);
PG_FUNCTION_INFO_V1(senna_drop_index);
PG_FUNCTION_INFO_V1(senna_reindex_index);
PG_FUNCTION_INFO_V1(senna_contains);
PG_FUNCTION_INFO_V1(senna_contained);
PG_FUNCTION_INFO_V1(senna_restsel);
PG_FUNCTION_INFO_V1(senna_insert);
PG_FUNCTION_INFO_V1(senna_beginscan);
PG_FUNCTION_INFO_V1(senna_gettuple);
#if PG_VERSION_NUM >= 80400
PG_FUNCTION_INFO_V1(senna_getbitmap);
#else
PG_FUNCTION_INFO_V1(senna_getmulti);
#endif
PG_FUNCTION_INFO_V1(senna_rescan);
PG_FUNCTION_INFO_V1(senna_endscan);
PG_FUNCTION_INFO_V1(senna_build);
#if PG_VERSION_NUM >= 90100
PG_FUNCTION_INFO_V1(senna_buildempty);
#endif
PG_FUNCTION_INFO_V1(senna_bulkdelete);
PG_FUNCTION_INFO_V1(senna_vacuumcleanup);
PG_FUNCTION_INFO_V1(senna_costestimate);
#if PG_VERSION_NUM >= 80200
PG_FUNCTION_INFO_V1(senna_options);
#endif

extern void PGDLLEXPORT _PG_init(void);
extern void PGDLLEXPORT _PG_fini(void);
extern Datum PGDLLEXPORT pg_sync_file(PG_FUNCTION_ARGS);
extern Datum PGDLLEXPORT senna_drop_index(PG_FUNCTION_ARGS);
extern Datum PGDLLEXPORT senna_reindex_index(PG_FUNCTION_ARGS);
extern Datum PGDLLEXPORT senna_contains(PG_FUNCTION_ARGS);
extern Datum PGDLLEXPORT senna_contained(PG_FUNCTION_ARGS);
extern Datum PGDLLEXPORT senna_restsel(PG_FUNCTION_ARGS);
extern Datum PGDLLEXPORT senna_insert(PG_FUNCTION_ARGS);
extern Datum PGDLLEXPORT senna_beginscan(PG_FUNCTION_ARGS);
extern Datum PGDLLEXPORT senna_gettuple(PG_FUNCTION_ARGS);
#if PG_VERSION_NUM >= 80400
extern Datum PGDLLEXPORT senna_getbitmap(PG_FUNCTION_ARGS);
#else
extern Datum PGDLLEXPORT senna_getmulti(PG_FUNCTION_ARGS);
#endif
extern Datum PGDLLEXPORT senna_rescan(PG_FUNCTION_ARGS);
extern Datum PGDLLEXPORT senna_endscan(PG_FUNCTION_ARGS);
extern Datum PGDLLEXPORT senna_build(PG_FUNCTION_ARGS);
#if PG_VERSION_NUM >= 90100
extern Datum PGDLLEXPORT senna_buildempty(PG_FUNCTION_ARGS);
#endif
extern Datum PGDLLEXPORT senna_bulkdelete(PG_FUNCTION_ARGS);
extern Datum PGDLLEXPORT senna_vacuumcleanup(PG_FUNCTION_ARGS);
extern Datum PGDLLEXPORT senna_costestimate(PG_FUNCTION_ARGS);
#if PG_VERSION_NUM >= 80200
extern Datum PGDLLEXPORT senna_options(PG_FUNCTION_ARGS);
#endif

static char *SennaPath(const RelFileNode *rnode, bool istemp);
static sen_encoding SennaEncoding(void);
static sen_index *SennaIndexOpen(Relation index, int flags);
static sen_index *SennaIndexCreate(Relation index);
static void SennaIndexClose(Oid relid, const RelFileNode *rnode);
static bool SennaRemove(const RelFileNode *rnode, bool istemp);
static sen_query *SennaQuery(const char *key, size_t len);
static sen_records *SennaRecordsOpen(Relation indexRelation, const char *str, int len, sen_index **idx);
static void SennaRecordsClose(sen_records *records);
static void SennaLock(Relation index);
static void SennaUnlock(Relation index);
static bool SennaScanNext(SennaScanDesc *desc, IndexScanDesc scan, ItemPointer ctid);
static void SennaScanClose(SennaScanDesc *desc);
static void SennaInsert(sen_index *index, ItemPointer ctid, Datum value);
static void SennaDelete(sen_index *index, ItemPointer key, text *value);
static void SennaBuildCallback(Relation index, HeapTuple htup, Datum *values, bool *isnull, bool tupleIsAlive, void *context);
static uint64 SennaIndexSize(sen_index *index);
static IndexBulkDeleteResult *SennaBulkDeleteResult(sen_index *index);
static void SennaXactCallback(XactEvent event, void *arg);
static void SennaCacheRelCallback(Datum arg, Oid relid);

#if PG_VERSION_NUM < 80400

static void
RemoveRelations(DropStmt *drop)
{
	ListCell *cell;
	foreach(cell, drop->objects)
		RemoveRelation(makeRangeVarFromNameList(lfirst(cell)), drop->behavior);
}

#endif

#define STRATEGY_IS_LIKE(s)	((s) == 3 || (s) == 4)

#define FLAGS_NORM		(SEN_INDEX_NGRAM | SEN_INDEX_NORMALIZE)
#define FLAGS_LIKE		(SEN_INDEX_NGRAM | SEN_INDEX_SPLIT_ALPHA | SEN_INDEX_SPLIT_DIGIT | SEN_INDEX_SPLIT_SYMBOL)

static SennaIndexCache	index_cache[INDEX_CACHE_SIZE];
static SennaQueryCache	query_cache[QUERY_CACHE_SIZE];
static List			   *records_cache;	/* List of SennaRecordsCache */

#if PG_VERSION_NUM >= 80400
static int RELOPT_KIND_SENNA;
#endif

#define DEFAULT_INITIAL_N_SEGMENTS			512

#define SennaInitialSegments(relation) \
	((relation)->rd_options ? \
	 ((SennaOptions *) (relation)->rd_options)->initial_n_segments : \
	 DEFAULT_INITIAL_N_SEGMENTS)

#if PG_VERSION_NUM < 80300
/* copied from src/backend/utils/adt/varlena.c (9.4) */

/*
 * text_to_cstring
 *
 * Create a palloc'd, null-terminated C string from a text value.
 *
 * We support being passed a compressed or toasted text value.
 * This is a bit bogus since such values shouldn't really be referred to as
 * "text *", but it seems useful for robustness.  If we didn't handle that
 * case here, we'd need another routine that did, anyway.
 */
char *
text_to_cstring(const text *t)
{
	/* must cast away the const, unfortunately */
	text	   *tunpacked = pg_detoast_datum_packed((struct varlena *) t);
	int			len = VARSIZE_ANY_EXHDR(tunpacked);
	char	   *result;

	result = (char *) palloc(len + 1);
	memcpy(result, VARDATA_ANY(tunpacked), len);
	result[len] = '\0';

	if (tunpacked != t)
		pfree(tunpacked);

	return result;
}
#endif

/* copied from adt/genfile.c */
static char *
convert_and_check_filename(text *arg)
{
	char	   *filename;

	filename = text_to_cstring(arg);
	canonicalize_path(filename);	/* filename can change length here */

	/* Disallow ".." in the path */
	if (path_contains_parent_reference(filename))
		ereport(ERROR,
				(errcode(ERRCODE_INSUFFICIENT_PRIVILEGE),
			(errmsg("reference to parent directory (\"..\") not allowed"))));

	if (is_absolute_path(filename))
	{
		/* Allow absolute references within DataDir */
		if (path_is_prefix_of_path(DataDir, filename))
			return filename;
		/* The log directory might be outside our datadir, but allow it */
		if (is_absolute_path(Log_directory) &&
			path_is_prefix_of_path(Log_directory, filename))
			return filename;

		ereport(ERROR,
				(errcode(ERRCODE_INSUFFICIENT_PRIVILEGE),
				 (errmsg("absolute path not allowed"))));
		return NULL;			/* keep compiler quiet */
	}
	else
	{
		return filename;
	}
}

Datum
pg_sync_file(PG_FUNCTION_ARGS)
{
	text	   *filename_t = PG_GETARG_TEXT_P(0);
	char	   *filename;
	int			fd;

	if (!superuser())
		ereport(ERROR,
				(errcode(ERRCODE_INSUFFICIENT_PRIVILEGE),
				 (errmsg("must be superuser to read files"))));

	filename = convert_and_check_filename(filename_t);

	if ((fd = BasicOpenFile(filename, O_RDWR | PG_BINARY, 0)) < 0)
		ereport(ERROR,
			(errcode_for_file_access(),
			 errmsg("could not open file \"%s\" for reading: %m",
					filename)));

	if (pg_fsync(fd))
		ereport(WARNING,
				(errcode_for_file_access(),
				 errmsg("could not fsync file \"%s\": %m", filename)));

	if (close(fd))
		ereport(WARNING,
				(errcode_for_file_access(),
				 errmsg("could not close file \"%s\": %m", filename)));

	pfree(filename);

	PG_RETURN_VOID();
}

static bool
is_senna_index(Relation index)
{
	return strcmp(NameStr(index->rd_am->amname), "senna") == 0;
}

/*
 * Open senna index, or raise error if the index is not a senna.
 */
static Relation
senna_index_open(Oid relid, LOCKMODE lockmode)
{
	Relation	index;

	index = index_open(relid, lockmode);

	/* Check permissions */
	if (!pg_class_ownercheck(relid, GetUserId()))
		aclcheck_error(ACLCHECK_NOT_OWNER, ACL_KIND_CLASS,
					   RelationGetRelationName(index));

	if (!is_senna_index(index))
		ereport(ERROR,
				(errcode(ERRCODE_WRONG_OBJECT_TYPE),
				 errmsg("senna: not a senna index (%s)",
						NameStr(index->rd_am->amname))));

	return index;
}

static void
IndexCacheDispose(SennaIndexCache *cache)
{
	sen_rc	rc;

	DEBUG_INDEX_CLOSE(cache->rnode);
	rc = sen_index_close(cache->index);
	if (rc != sen_success)
		elog(WARNING, "senna: sen_index_close() : code=%d", rc);
}

Datum
senna_drop_index(PG_FUNCTION_ARGS)
{
	Oid			relid = PG_GETARG_OID(0);
	Relation	index;
	RelFileNode	rnode;
	bool		istemp;
	DropStmt   *stmt;
	List	   *obj;

	index = senna_index_open(relid, NoLock);

	obj = list_make2(
		makeString(get_namespace_name(RelationGetNamespace(index))),
		makeString(pstrdup(RelationGetRelationName(index))));

	stmt = makeNode(DropStmt);
	stmt->removeType = OBJECT_INDEX;
	stmt->missing_ok = true;
	stmt->objects = list_make1(obj);
	stmt->behavior = DROP_CASCADE;

	rnode = index->rd_node;
	istemp = RelationIsTemp(index);
	index_close(index, NoLock);

	RemoveRelations(stmt);

	SennaRemove(&rnode, istemp);

	PG_RETURN_VOID();
}

Datum
senna_reindex_index(PG_FUNCTION_ARGS)
{
	Oid			relid = PG_GETARG_OID(0);
	Relation	index;
	RelFileNode	rnode;
	bool		istemp;

	index = senna_index_open(relid, NoLock);
	rnode = index->rd_node;
	istemp = RelationIsTemp(index);
	index_close(index, NoLock);

	reindex_index(relid, false);

	SennaRemove(&rnode, istemp);

	PG_RETURN_VOID();
}

static bool
SennaContains(text *doc, text *query)
{
	const char *str = VARDATA_ANY(doc);
	unsigned	len = VARSIZE_ANY_EXHDR(doc);
	sen_query  *q;
	sen_rc		rc;
	int			found;
	int			score;

	q = SennaQuery(VARDATA_ANY(query), VARSIZE_ANY_EXHDR(query));

	rc = sen_query_scan(q, &str, &len, 1, SEN_QUERY_SCAN_NORMALIZE, &found, &score);
	if (rc != sen_success)
		elog(ERROR, "senna: sen_query_scan() : code=%d", rc);

	return found && score;
}

Datum
senna_contains(PG_FUNCTION_ARGS)
{
	PG_RETURN_BOOL(SennaContains(
		PG_GETARG_TEXT_PP(0), PG_GETARG_TEXT_PP(1)));
}

Datum
senna_contained(PG_FUNCTION_ARGS)
{
	PG_RETURN_BOOL(SennaContains(
		PG_GETARG_TEXT_PP(1), PG_GETARG_TEXT_PP(0)));
}

/*
 * selectivity for %% and @@ operator.
 */
Datum
senna_restsel(PG_FUNCTION_ARGS)
{
#ifdef RESTSEL_USES_SCAN
	PlannerInfo *root = (PlannerInfo *) PG_GETARG_POINTER(0);
#ifdef NOT_USED
	Oid			operator = PG_GETARG_OID(1);
#endif
	List	   *args = (List *) PG_GETARG_POINTER(2);
	int			varRelid = PG_GETARG_INT32(3);
	VariableStatData vardata;
	Node	   *other;
	bool		varonleft;
	double		selec;

	if (!get_restriction_variable(root, args, varRelid, &vardata, &other, &varonleft))
	{
		selec = DEFAULT_MATCH_SEL;
	}
	else if (!IsA(other, Const))
	{
		bool	isdefault;
		/* no idea if non-const... */
		selec = 1.0 / get_variable_numdistinct(&vardata, &isdefault);
	}
	else if (((Const *) other)->constisnull)
	{
		/* variable = NULL will never return TRUE. */
		selec = 0.0;
	}
	else if (vardata.rel == NULL || vardata.rel->rtekind != RTE_RELATION)
	{
		/* no idea if non-heap... */
		selec = DEFAULT_MATCH_SEL;
	}
	else
	{
		Oid			relid;
		Relation	heapRel;
		ListCell   *cell;

		selec = DEFAULT_MATCH_SEL;

		/* Find a senna index for on the heap */
		relid = planner_rt_fetch(vardata.rel->relid, root)->relid;
		heapRel = heap_open(relid, AccessShareLock);
		foreach(cell, RelationGetIndexList(heapRel))
		{
			Oid			indrelid = lfirst_oid(cell);
			Relation	indexRel;

			indexRel = index_open(indrelid, AccessShareLock);
			if (is_senna_index(indexRel))
			{
				text		   *query;
				sen_index	   *index;
				sen_records	   *records;
				unsigned int	nhits;
				unsigned int	nkeys;

				/*
				 * If we find a senna index, we search values actually with
				 * the index. We don't free the records here so that the result
				 * records will be cached and we would reuse them in executor.
				 * Cleanup will be performed at the end of transaction.
				 */
				query = DatumGetTextPP(((Const *) other)->constvalue);
				records = SennaRecordsOpen(indexRel,
					VARDATA_ANY(query), VARSIZE_ANY_EXHDR(query), &index);
				nhits = sen_records_nhits(records);
				nkeys = sen_sym_size(index->keys);
#ifdef NOT_USED
				SennaRecordsClose(records);
#endif
				if (nhits >= nkeys)
					selec = 1.0;
				else if (nkeys > 0)
					selec = (double)nhits / nkeys;

				index_close(indexRel, AccessShareLock);
				break;
			}
			index_close(indexRel, AccessShareLock);
		}
		heap_close(heapRel, AccessShareLock);
	}

	ReleaseVariableStats(vardata);

	PG_RETURN_FLOAT8((float8) selec);

#else	/* RESTSEL_USES_SCAN */

	PG_RETURN_FLOAT8(DEFAULT_MATCH_SEL);

#endif	/* RESTSEL_USES_SCAN */
}

Datum
senna_insert(PG_FUNCTION_ARGS)
{
	Relation	index = (Relation) PG_GETARG_POINTER(0);
	Datum	   *values = (Datum *) PG_GETARG_POINTER(1);
	bool	   *isnull = (bool *) PG_GETARG_POINTER(2);
	ItemPointer	ctid = (ItemPointer) PG_GETARG_POINTER(3);
#ifdef NOT_USED
	Relation	heap = (Relation) PG_GETARG_POINTER(4);
	bool		checkUnique = PG_GETARG_BOOL(5);
#endif

	sen_index  *i;

	/* cannot index nulls */
	if (isnull[0])
		PG_RETURN_BOOL(false);

	i = SennaIndexOpen(index, 0);

	SennaLock(index);
	SennaInsert(i, ctid, values[0]);
	SennaUnlock(index);

	PG_RETURN_BOOL(true);
}

Datum
senna_beginscan(PG_FUNCTION_ARGS)
{
	Relation		index = (Relation) PG_GETARG_POINTER(0);
	int				nkeys = PG_GETARG_INT32(1);
#if PG_VERSION_NUM >= 90100
	int			norderbys = PG_GETARG_INT32(2);
#else
	ScanKey			key = (ScanKey) PG_GETARG_POINTER(2);
#endif
	IndexScanDesc	scan;

	if (nkeys < 1)
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("senna: do not support whole-index scans")));

#if PG_VERSION_NUM >= 90100
	/* no order by operators allowed */
	Assert(norderbys == 0);

	scan = RelationGetIndexScan(index, nkeys, norderbys);
#else
	scan = RelationGetIndexScan(index, nkeys, key);
#endif

	PG_RETURN_POINTER(scan);
}

static text *
extract_value(HeapTuple tuple, Relation heap, Relation index)
{
	TupleDesc	tupdesc;
	int			attnum;
	Datum		value;
	bool		isnull;

	if (tuple->t_data == NULL)
		return NULL;

	tupdesc = RelationGetDescr(heap);
	attnum = index->rd_index->indkey.values[0];

	/* attnum could be 0 for expression indexes */
	if (attnum == 0)
	{
		/* TODO: re-evaluate expression and construct an actual value */
		return NULL;
	}

	value = heap_getattr(tuple, attnum, tupdesc, &isnull);

	if (isnull)
		return NULL;
	else
		return DatumGetTextPP(value);
}

#ifdef NOT_USED
static int
sort_by_score(sen_records *r1, const sen_recordh *rh1,
			  sen_records *r2, const sen_recordh *rh2, void *arg)
{
	sen_rc	rc;
	int		score1;
	int		score2;
	
	rc = sen_record_info(r1, rh1, NULL, 0, NULL, NULL, NULL, &score1, NULL);
	rc = sen_record_info(r2, rh2, NULL, 0, NULL, NULL, NULL, &score2, NULL);

	if (score1 < score2)
		return -1;
	else if (score1 > score2)
		return +1;
	else
		return 0;
}
#endif

Datum
senna_gettuple(PG_FUNCTION_ARGS)
{
	IndexScanDesc	scan = (IndexScanDesc) PG_GETARG_POINTER(0);
	ScanDirection	dir = (ScanDirection) PG_GETARG_INT32(1);

	SennaScanDesc *desc = (SennaScanDesc *) scan->opaque;
	ItemPointerData ctid;

	if (dir != ForwardScanDirection)
		elog(ERROR, "senna: only supports forward scans");

	if (scan->kill_prior_tuple)
	{
		sen_index  *index = SennaIndexOpen(scan->indexRelation, 0);
		text	   *value = extract_value(&scan->xs_ctup,
								scan->heapRelation, scan->indexRelation);

		SennaLock(scan->indexRelation);
		SennaDelete(index, &scan->xs_ctup.t_self, value);
		SennaUnlock(scan->indexRelation);
	}

#if NOT_USED
	/*
	 * sort is performed in gettuple but not in rescan because it is
	 * not useful for bitmap scans.
	 */
	if (desc->sorter.compar != NULL)
	{
		int	limit = sen_records_nhits(desc->records);
		sen_records_sort(desc->records, limit, &desc->sorter);
		desc->sorter.compar = NULL;
	}
#endif

	if (!SennaScanNext(desc, scan, &ctid))
		PG_RETURN_BOOL(false);

	scan->xs_ctup.t_self = ctid;
#if PG_VERSION_NUM >= 80400
	scan->xs_recheck = desc->recheck;
#endif

	PG_RETURN_BOOL(true);
}

#if PG_VERSION_NUM >= 80400

Datum
senna_getbitmap(PG_FUNCTION_ARGS)
{
	IndexScanDesc	scan = (IndexScanDesc) PG_GETARG_POINTER(0);
	TIDBitmap	   *tbm = (TIDBitmap *) PG_GETARG_POINTER(1);

	SennaScanDesc *desc = (SennaScanDesc *) scan->opaque;
	int64			ntids;
	ItemPointerData ctid;

	for (ntids = 0; SennaScanNext(desc, scan, &ctid); ntids++)
	{
		CHECK_FOR_INTERRUPTS();
		tbm_add_tuples(tbm, &ctid, 1, desc->recheck);
	}

	PG_RETURN_INT64(ntids);
}

#else

Datum
senna_getmulti(PG_FUNCTION_ARGS)
{
	IndexScanDesc scan = (IndexScanDesc) PG_GETARG_POINTER(0);
	ItemPointer tids = (ItemPointer) PG_GETARG_POINTER(1);
	int32		max_tids = PG_GETARG_INT32(2);
	int32	   *returned_tids = (int32 *) PG_GETARG_POINTER(3);

	SennaScanDesc *desc = (SennaScanDesc *) scan->opaque;
	int64			ntids;
	ItemPointerData ctid;

	if (max_tids <= 0)			/* behave correctly in boundary case */
		PG_RETURN_BOOL(true);

	for (ntids = 0;
		 ntids < max_tids && SennaScanNext(desc, scan, &ctid);
		 ntids++)
	{
		/* Save tuple ID, and continue scanning */
		tids[ntids] = ctid;
	}

	*returned_tids = (int32) ntids;
	PG_RETURN_BOOL(ntids >= max_tids);
}

#endif

Datum
senna_rescan(PG_FUNCTION_ARGS)
{
	IndexScanDesc	scan = (IndexScanDesc) PG_GETARG_POINTER(0);
	ScanKey			key = (ScanKey) PG_GETARG_POINTER(1);

	SennaScanDesc *desc = (SennaScanDesc *) scan->opaque;

	if (desc == NULL)
	{
		scan->opaque = desc = palloc0(sizeof(SennaScanDesc));
#ifdef NOT_USED
		desc->sorter.mode = sen_sort_descending;
		desc->sorter.compar = sort_by_score;
#endif
	}
	else
		SennaScanClose(desc);

	if (key && scan->numberOfKeys > 0)
		memmove(scan->keyData, key, scan->numberOfKeys * sizeof(ScanKeyData));

	PG_RETURN_VOID();
}

Datum
senna_endscan(PG_FUNCTION_ARGS)
{
	IndexScanDesc	scan = (IndexScanDesc) PG_GETARG_POINTER(0);

	SennaScanDesc *desc = (SennaScanDesc *) scan->opaque;

	if (desc != NULL)
	{
		SennaScanClose(desc);
		pfree(desc);
	}

	PG_RETURN_VOID();
}

Datum
senna_build(PG_FUNCTION_ARGS)
{
	Relation	heap = (Relation) PG_GETARG_POINTER(0);
	Relation	index = (Relation) PG_GETARG_POINTER(1);
	IndexInfo  *indexInfo = (IndexInfo *) PG_GETARG_POINTER(2);

	IndexBuildResult   *result;
	SennaBuildState		state;

	if (indexInfo->ii_Unique)
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("senna: do not support unique index")));

	if (RelationGetNumberOfAttributes(index) != 1)
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("senna: do not support multi columns")));

	state.index = SennaIndexCreate(index);

	PG_TRY();
	{
		result = (IndexBuildResult *) palloc(sizeof(IndexBuildResult));
		SennaLock(index);
		result->heap_tuples = IndexBuildHeapScan(heap, index, indexInfo, true, SennaBuildCallback, &state);
		SennaUnlock(index);
		result->index_tuples = sen_sym_size(state.index->keys);
	}
	PG_CATCH();
	{
		SennaRemove(&index->rd_node, RelationIsTemp(index));
		PG_RE_THROW();
	}
	PG_END_TRY();

	PG_RETURN_POINTER(result);
}

#if PG_VERSION_NUM >= 90100
/**
 * senna_buildempty() -- ambuildempty
 */
Datum
senna_buildempty(PG_FUNCTION_ARGS)
{
#ifdef NOT_USED
	Relation	index = (Relation) PG_GETARG_POINTER(0);
#endif

	PG_RETURN_VOID();
}
#endif

Datum
senna_bulkdelete(PG_FUNCTION_ARGS)
{
	IndexVacuumInfo		   *info = (IndexVacuumInfo *) PG_GETARG_POINTER(0);
	IndexBulkDeleteResult  *stats = (IndexBulkDeleteResult *) PG_GETARG_POINTER(1);
	IndexBulkDeleteCallback	callback = (IndexBulkDeleteCallback) PG_GETARG_POINTER(2);
	void				   *callback_state = (void *) PG_GETARG_POINTER(3);

	double			tuples_removed;
	sen_id			id;
	sen_index	   *index;

#ifdef VACUUM_USES_HEAP
	Relation		heap;
	HeapTupleData	tuple;
#endif

	index = SennaIndexOpen(info->index, 0);

	if (stats == NULL)
		stats = SennaBulkDeleteResult(index);

	if (callback == NULL)
		PG_RETURN_POINTER(stats);

#ifdef VACUUM_USES_HEAP
	heap = heap_open(info->index->rd_index->indrelid, NoLock);
	tuple.t_tableOid = RelationGetRelid(heap);
#endif

	tuples_removed = 0;
	id = 0;
	for (;;)
	{
		ItemPointerData	ctid;
		sen_id			next_id;
		
		CHECK_FOR_INTERRUPTS();

		next_id = sen_sym_next(index->keys, id);
		if (next_id == SEN_SYM_NIL || next_id == id)
			break;
		id = next_id;

		if (sen_sym_key(index->keys, id, &ctid, SizeOfIptrData) != SizeOfIptrData)
		{
			elog(WARNING, "senna: sen_sym_key()");
			continue;
		}

		if (callback(&ctid, callback_state))
		{
			text		   *value = NULL;

#ifdef VACUUM_USES_HEAP
			BlockNumber		blknum;
			BlockNumber		offnum;
			Buffer			buffer;
			Page			page;
			ItemId			itemid;

			blknum = ItemPointerGetBlockNumber(&ctid);
			offnum = ItemPointerGetOffsetNumber(&ctid);
			buffer = ReadBuffer(heap, blknum);

			LockBuffer(buffer, BUFFER_LOCK_SHARE);
			page = BufferGetPage(buffer);
			itemid = PageGetItemId(page, offnum);
			tuple.t_data = ItemIdIsNormal(itemid)
				? (HeapTupleHeader) PageGetItem(page, itemid)
				: NULL;
			LockBuffer(buffer, BUFFER_LOCK_UNLOCK);

			if (tuple.t_data != NULL)
			{
				tuple.t_len = ItemIdGetLength(itemid);
				tuple.t_self = ctid;
				value = extract_value(&tuple, heap, info->index);
			}
#endif
			SennaLock(info->index);
			SennaDelete(index, &ctid, value);
			SennaUnlock(info->index);

#ifdef VACUUM_USES_HEAP
			ReleaseBuffer(buffer);
#endif

			tuples_removed += 1;
		}
	}

#ifdef VACUUM_USES_HEAP
	heap_close(heap, NoLock);
#endif

	stats->tuples_removed = tuples_removed;

	PG_RETURN_POINTER(stats);
}

Datum
senna_vacuumcleanup(PG_FUNCTION_ARGS)
{
	IndexVacuumInfo *info = (IndexVacuumInfo *) PG_GETARG_POINTER(0);
	IndexBulkDeleteResult *stats = (IndexBulkDeleteResult *) PG_GETARG_POINTER(1);

	if (stats == NULL)
		stats = SennaBulkDeleteResult(SennaIndexOpen(info->index, 0));

	PG_RETURN_POINTER(stats);
}

Datum
senna_costestimate(PG_FUNCTION_ARGS)
{
	/*
	 * We cannot use genericcostestimate because it is a static funciton.
	 * Use gistcostestimate instead, which just calls genericcostestimate.
	 * Actual estimation is done in senna_restsel, which is selectivity
	 * functions for %% and @@ operator.
	 */
	return gistcostestimate(fcinfo);
}

#if PG_VERSION_NUM >= 80400
Datum
senna_options(PG_FUNCTION_ARGS)
{
	Datum			reloptions = PG_GETARG_DATUM(0);
	bool			validate = PG_GETARG_BOOL(1);
	relopt_value   *options;
	SennaOptions   *rdopts;
	int				numoptions;
	static const relopt_parse_elt tab[] = {
		{"initial_n_segments", RELOPT_TYPE_INT, offsetof(SennaOptions, initial_n_segments)}
	};

	options = parseRelOptions(reloptions, validate, RELOPT_KIND_SENNA,
							  &numoptions);

	/* if none set, we're done */
	if (numoptions == 0)
		PG_RETURN_NULL();

	rdopts = allocateReloptStruct(sizeof(SennaOptions), options, numoptions);

	fillRelOptions(rdopts, sizeof(SennaOptions), options, numoptions,
					validate, tab, lengthof(tab));

	pfree(options);
	PG_RETURN_BYTEA_P(rdopts);
}

#elif PG_VERSION_NUM >= 80200

Datum
senna_options(PG_FUNCTION_ARGS)
{
	Datum		reloptions = PG_GETARG_DATUM(0);
	bool		validate = PG_GETARG_BOOL(1);
	static const char *const keywords[1] = {"initial_n_segments"};
	char	   *values[1];

	parseRelOptions(reloptions, 1, keywords, values, validate);

	if (values[0] != NULL)
	{
		int32		initial_n_segments;
		SennaOptions *result;

		initial_n_segments = pg_atoi(values[0], sizeof(int32), 0);
		result = (SennaOptions *) palloc(sizeof(SennaOptions));
		SET_VARSIZE(result, sizeof(SennaOptions));
		result->initial_n_segments = initial_n_segments;

		PG_RETURN_BYTEA_P(result);
	}

	PG_RETURN_NULL();
}

#endif

static char *
SennaPath(const RelFileNode *rnode, bool istemp)
{
#if PG_VERSION_NUM >= 90100
	if (istemp)
		return relpathbackend(*rnode, MyBackendId, MAIN_FORKNUM);
	else
		return relpathbackend(*rnode, InvalidBackendId, MAIN_FORKNUM);
#else
	return relpath(*rnode, MAIN_FORKNUM);
#endif
}

static sen_encoding	sen_encoding_cache = sen_enc_default;

static sen_encoding
SennaEncoding(void)
{
	if (sen_encoding_cache == sen_enc_default)
	{
		if (pg_database_encoding_max_length() == 1)
			sen_encoding_cache = sen_enc_none;
		else switch (GetDatabaseEncoding())
		{
			case PG_EUC_JP:
#if PG_VERSION_NUM >= 80300
			case PG_EUC_JIS_2004:
#endif
				sen_encoding_cache = sen_enc_euc_jp;
				break;
			case PG_UTF8:
				sen_encoding_cache = sen_enc_utf8;
				break;
			default:
				ereport(ERROR,
						(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
						 errmsg("senna: do not support %s encoding", GetDatabaseEncodingName())));
		}
	}
	return sen_encoding_cache;
}

static void
SennaBuildCallback(Relation index,
				   HeapTuple htup,
				   Datum *values,
				   bool *isnull,
				   bool tupleIsAlive,
				   void *context)
{
	SennaBuildState	*state = (SennaBuildState *) context;

	if (!isnull[0])
		SennaInsert(state->index, &htup->t_self, values[0]);
}

static sen_index *
SennaIndexOpen(Relation indexRelation, int flags)
{
	int					i;
	SennaIndexCache    *cache;
	sen_index		   *index;
	char			   *path;

	/* search query cache */
	for (i = 0; i < lengthof(index_cache); i++)
	{
		cache = &index_cache[i];

		if (cache->index == NULL)
			break;
		if (RelFileNodeEquals(cache->rnode, indexRelation->rd_node))
			return cache->index;
	}

	/* Be careful not to leak resources on error. */
	path = SennaPath(&indexRelation->rd_node, RelationIsTemp(indexRelation));
	if (flags != 0)
	{
		DEBUG_INDEX_CREATE(indexRelation->rd_node);
		index = sen_index_create(
			path,
			SizeOfIptrData,
			flags,
			SennaInitialSegments(indexRelation),
			SennaEncoding());
		if (index == NULL)
			elog(ERROR, "senna: sen_index_create(%s)", path);
	}
	else
	{
		DEBUG_INDEX_OPEN(indexRelation->rd_node);
		index = sen_index_open(path);
		if (index == NULL)
			elog(ERROR, "senna: sen_index_open(%s)", path);
	}

	if (cache->index != NULL)
	{
		/* release the last */
		cache = &index_cache[lengthof(index_cache) - 1];
		IndexCacheDispose(cache);
		/* pop the last */
		memmove(&index_cache[1], &index_cache[0],
			(lengthof(index_cache) - 1) * sizeof(SennaIndexCache));
		/* first one */
		cache = &index_cache[0];
	}

	cache->relid = RelationGetRelid(indexRelation);
	cache->rnode = indexRelation->rd_node;
	cache->index = index;

	pfree(path);

	return index;
}

static sen_index *
SennaIndexCreate(Relation index)
{
	TupleDesc	tupdesc;
	int			flags;

#if PG_VERSION_NUM >= 90100

	Datum		indclassDatum;
	bool		isnull;
	oidvector  *indclass;
	Oid			opclass;
	HeapTuple	tp;
	Form_pg_opclass opc;

	indclassDatum = SysCacheGetAttr(INDEXRELID, index->rd_indextuple,
									Anum_pg_index_indclass, &isnull);
	Assert(!isnull);
	indclass = (oidvector *) DatumGetPointer(indclassDatum);
	opclass = indclass->values[0];

	tp = SearchSysCache1(CLAOID, ObjectIdGetDatum(opclass));
	if (!HeapTupleIsValid(tp))
		elog(ERROR, "cache lookup failed for opclass %u", opclass);

	opc = (Form_pg_opclass) GETSTRUCT(tp);
	if (strcmp(opc->opcname.data, "like_ops") != 0)
		flags = FLAGS_NORM;
	else
		flags = FLAGS_LIKE;

	ReleaseSysCache(tp);
#else

#define OPCLASS_IS_NORM(opr) 	(((opr)[0] || (opr)[1]) && !((opr)[2]) && !((opr)[3]))
#define OPCLASS_IS_LIKE(opr) 	(((opr)[2] || (opr)[3]) && !((opr)[0]) && !((opr)[1]))

	if (OPCLASS_IS_NORM(index->rd_operator))
		flags = FLAGS_NORM;
	else if (OPCLASS_IS_LIKE(index->rd_operator))
		flags = FLAGS_LIKE;
	else
	{
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("senna: unsupported operator class")));
		flags = 0;	/* keep compilar quiet */
	}

#endif

	tupdesc = RelationGetDescr(index);
	switch (tupdesc->attrs[0]->atttypid)
	{
		case BPCHAROID:
		case VARCHAROID:
		case TEXTOID:
			break;
		default:
			ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("senna: only supports char, varchar and text")));
	}

	return SennaIndexOpen(index, flags);
}

static void
SennaIndexClose(Oid relid, const RelFileNode *rnode)
{
	int		i;

	if (relid == InvalidOid && rnode == NULL)
	{
		/* close all indexes */
		for (i = lengthof(index_cache) - 1; i >= 0; i--)
		{
			SennaIndexCache *cache = &index_cache[i];
			if (cache->index != NULL)
			{
				IndexCacheDispose(cache);
				cache->index = NULL;
			}
		}
	}
	else
	{
		/* search index cache */
		for (i = 0; i < lengthof(index_cache); i++)
		{
			SennaIndexCache *cache = &index_cache[i];

			if (cache->index == NULL)
				break;
			if ((rnode != NULL && RelFileNodeEquals(cache->rnode, *rnode)) ||
				cache->relid == relid)
			{
				IndexCacheDispose(cache);
				memmove(&index_cache[i], &index_cache[i + 1],
					(lengthof(index_cache) - i - 1) * sizeof(SennaIndexCache));
				index_cache[lengthof(index_cache) - 1].index = NULL;
				return;
			}
		}
		/* not found */
	}
}

static bool
SennaRemove(const RelFileNode *rnode, bool istemp)
{
	int			loops = 0;
	sen_rc		rc;
	char	   *path;

	SennaIndexClose(InvalidOid, rnode);
	path = SennaPath(rnode, istemp);

	while ((rc = sen_index_remove(path)) != sen_success)
	{
		if (rc != sen_file_operation_error || ++loops > REMOVE_RETRY)
		{
			elog(WARNING, "senna: sen_index_remove(%s) : code=%d", path, rc);
			break;
		}

		pg_usleep(100000);		/* us */

		CHECK_FOR_INTERRUPTS();
	}

	pfree(path);

	return rc == sen_success;
}

static sen_query *
SennaQuery(const char *str, size_t len)
{
	int					i;
	SennaQueryCache    *cache;
	sen_query		   *query;
	char			   *key;

	/* search query cache */
	for (i = 0; i < lengthof(query_cache); i++)
	{
		cache = &query_cache[i];

		if (cache->query == NULL)
			break;
		if (strncmp(cache->key, str, len) == 0)
			return cache->query;
	}

	/* Be careful not to leak resources on error. */
	key = malloc(len + 1);
	if (key == NULL)
		ereport(ERROR,
				(errcode(ERRCODE_OUT_OF_MEMORY),
				 errmsg("out of memory")));
	strlcpy(key, str, len + 1);
	query = sen_query_open(str, len, sen_sel_or, SENNA_MAX_N_EXPR, SennaEncoding());
	if (query == NULL)
		elog(ERROR, "senna: sen_query_open()");

	if (cache->query != NULL)
	{
		/* release the last */
		cache = &query_cache[lengthof(query_cache) - 1];
		sen_query_close(cache->query);
		free(cache->key);
		/* pop the last */
		memmove(&query_cache[1], &query_cache[0],
			(lengthof(query_cache) - 1) * sizeof(SennaQueryCache));
		/* first one */
		cache = &query_cache[0];
	}

	cache->key = key;
	cache->query = query;
	return query;
}

static sen_records *
SennaRecordsOpen(Relation indexRelation, const char *str, int len, sen_index **idx)
{
	MemoryContext		oldctx;
	sen_query		   *query;
	sen_records		   *records;
	sen_index		   *index;
	sen_rc				rc;
	ListCell		   *cell;
	SennaRecordsCache	*cache;

	if (len == 0)
		ereport(ERROR,
			(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
			 errmsg("scan key must not be empty for senna index: %s.%s",
					get_namespace_name(RelationGetNamespace(indexRelation)),
					 RelationGetRelationName(indexRelation))));

	query = SennaQuery(str, len);

	/* search records cache first */
	foreach(cell, records_cache)
	{
		cache = (SennaRecordsCache *) lfirst(cell);
		if (cache->query == query &&
			cache->relid == RelationGetRelid(indexRelation))
		{
			DEBUG_RECORDS_CACHE(true);
			records = cache->records;
			if (idx)
				*idx = SennaIndexOpen(indexRelation, 0);
			sen_records_rewind(records);
			return records;
		}
	}

	DEBUG_RECORDS_CACHE(false);

	records = sen_records_open(sen_rec_document, sen_rec_none, 0);
	if (records == NULL)
		elog(ERROR, "senna: sen_records_open()");

	oldctx = MemoryContextSwitchTo(TopMemoryContext);
	cache = palloc(sizeof(SennaRecordsCache));
	cache->records = records;
	cache->query = query;
	cache->relid = RelationGetRelid(indexRelation);
	records_cache = lappend(records_cache, cache);
	MemoryContextSwitchTo(oldctx);

	index = SennaIndexOpen(indexRelation, 0);

	/* Senna doesn't require any locks in reads. */
	rc = sen_query_exec(index, query, records, sen_sel_or);

	if (rc != sen_success)
		elog(ERROR, "senna: sen_query_exec() : code=%d", rc);

	if (idx)
		*idx = index;
	return records;
}

/*
 * Release records from cache.
 */
static void
SennaRecordsClose(sen_records *records)
{
	ListCell   *cell;
	ListCell   *prev;
	sen_rc		rc;

	prev = NULL;
	foreach(cell, records_cache)
	{
		SennaRecordsCache *cache = (SennaRecordsCache *) lfirst(cell);
		if (cache->records == records)
		{
			rc = sen_records_close(cache->records);
			if (rc != sen_success)
				elog(WARNING, "senna: sen_records_close() : code=%d", rc);
			records_cache = list_delete_cell(records_cache, cell, prev);
			pfree(cache);
			break;
		}

		prev = cell;
	}
}

static void
SennaLock(Relation index)
{
	const RelFileNode *rnode = &index->rd_node;
	LockDatabaseObject(rnode->spcNode,
					   rnode->dbNode,
					   rnode->relNode,
					   ExclusiveLock);
}

static void
SennaUnlock(Relation index)
{
	const RelFileNode *rnode = &index->rd_node;
	UnlockDatabaseObject(rnode->spcNode,
						 rnode->dbNode,
						 rnode->relNode,
						 ExclusiveLock);
}

/*
 * Note that senna cannot search " in words because the double quote is a
 * special marker to separate words. I cannot find any descriptions in
 * senna documentation to escape the character.
 */
#define LIKE_WILDCARD			"%_"
#define WORD_SEP_CHARS			LIKE_WILDCARD "\""
#define NON_SIMPLE_CHARS		WORD_SEP_CHARS " \\\t\v\r\n"

/*
 * is_simple_like - true iff str matches %foo%.
 *
 * The foo part should not have '%', '_' nor any spaces.
 */
static bool
is_simple_like(const char *str, int len)
{
	int		i;

	if (len <= 2 || str[0] != '%' || str[len - 1] != '%')
		return false;

	for (i = 1; i < len - 1; i++)
	{
		if (strchr(NON_SIMPLE_CHARS, str[i]))
			return false;
	}
	return true;
}

/*
 * Concat multiple keys into one senna query.
 * For senna index, "col %% 'e1' AND col %% 'e2'" will be 'e1+e2'.
 * For like index, "col LIKE '%w1%' AND col LIKE '%w2%'" will be '"w1"+"w2"'.
 */
static void
SennaScanOpen(SennaScanDesc *desc,
			  Relation indexRelation,
			  int nkeys,
			  const ScanKeyData keys[/*nkeys*/])
{
	StringInfoData	buf;
	text		   *query;
	const char	   *str;
	int				len;
	int				n;

	/* fast path for scanning senna index with just one key. */
	if (nkeys == 1 && !STRATEGY_IS_LIKE(keys[0].sk_strategy))
	{
		query = DatumGetTextPP(keys[0].sk_argument);
		str = VARDATA_ANY(query);
		len = VARSIZE_ANY_EXHDR(query);

		desc->records = SennaRecordsOpen(indexRelation, str, len, NULL);
		return;
	}

	/* scan with multiple keys or LIKE strategy */
	initStringInfo(&buf);
	for (n = 0; n < nkeys; n++)
	{
		query = DatumGetTextPP(keys[n].sk_argument);
		str = VARDATA_ANY(query);
		len = VARSIZE_ANY_EXHDR(query);

		if (STRATEGY_IS_LIKE(keys[n].sk_strategy))
		{
			/* convert LIKE query to senna query */
			if (is_simple_like(str, len))
			{
				/* %foo% is converted to "foo" */
				if (buf.len > 0)
					appendStringInfoString(&buf, " +");
				appendStringInfoChar(&buf, '"');
				appendBinaryStringInfo(&buf, str + 1, len - 2);
				appendStringInfoChar(&buf, '"');

				/* don't need recheck if simple %foo% query */
			}
			else
			{
				int			i;
				bool		quoted = false;

				/* LIKE query needs recheck */
				desc->recheck = true;

				/* skip wildcard at head */
				i = (int) strspn(str, LIKE_WILDCARD);
				str += i;
				len -= i;

				/* convert LIKE pattern into senna query */
				for (i = 0; i < len; i++)
				{
					if (strchr(WORD_SEP_CHARS, str[i]))
					{
						/* close the previous quote if open */
						if (quoted)
						{
							appendStringInfoChar(&buf, '"');
							quoted = false;
						}
					}
					else
					{
						if (str[i] == '\\')
						{
							if (i >= len - 1)
								ereport(ERROR,
									(errcode(ERRCODE_INVALID_ESCAPE_SEQUENCE),
									 errmsg("LIKE pattern must not end with escape character")));
							/* skip the escape sequence and copy the next char. */
							i++;
						}

						if (!quoted)
						{
							if (buf.len > 0)
								appendStringInfoString(&buf, " +");
							appendStringInfoChar(&buf, '"');
							quoted = true;
						}

						/* escape backslash in senna query */
						if (str[i] == '\\')
							appendStringInfoChar(&buf, '\\');
						appendStringInfoChar(&buf, str[i]);
					}
				}

				if (quoted)
					appendStringInfoChar(&buf, '"');
			}
		}
		else
		{
			if (buf.len > 0)
				appendStringInfoString(&buf, " +");
			/* just copy for senna query */
			appendBinaryStringInfo(&buf, str, len);
		}
	}

	desc->records = SennaRecordsOpen(indexRelation, buf.data, buf.len, NULL);

	pfree(buf.data);
}

/*
 * Scan senna index or return the next tuple.
 */
static bool
SennaScanNext(SennaScanDesc *desc, IndexScanDesc scan, ItemPointer ctid)
{
	if (desc->records == NULL)
	{
		int					i,
							nkeys = scan->numberOfKeys;

		/* NULL key is not supported */
		for (i = 0; i < nkeys; i++)
			if (scan->keyData[i].sk_flags & SK_ISNULL)
				return false;

		SennaScanOpen(desc, scan->indexRelation, nkeys, scan->keyData);
	}

	Assert(desc->records != NULL);
	return sen_records_next(desc->records, ctid, SizeOfIptrData, NULL) == SizeOfIptrData;
}

static void
SennaScanClose(SennaScanDesc *desc)
{
	if (desc->records)
	{
		SennaRecordsClose(desc->records);
		desc->records = NULL;
	}
}

static void
SennaInsert(sen_index *index, ItemPointer ctid, Datum value)
{
	text	   *doc = DatumGetTextPP(value);
	const char *str = VARDATA_ANY(doc);
	int			len = VARSIZE_ANY_EXHDR(doc);
	sen_rc		rc;

	rc = sen_index_upd(index, ctid, NULL, 0, str, len);

	if (rc != sen_success)
		elog(ERROR, "senna: sen_index_upd(insert) : code=%d", rc);
}

static void
SennaDelete(sen_index *index, ItemPointer ctid, text *value)
{
	sen_rc	rc;

	if (!sen_sym_at(index->keys, ctid))
		return;	/* deletion will be failed in this case. */

	if (value != NULL)
	{
		DEBUG_DELETE("upd", ctid);

		rc = sen_index_upd(
				index, ctid,
				VARDATA_ANY(value), VARSIZE_ANY_EXHDR(value),
				NULL, 0);
		if (rc != sen_success)
			elog(WARNING, "senna: sen_index_upd(delete) : code=%d", rc);
	}
	else
	{
		/* The value could be unavailable in 8.3 or later. */
		DEBUG_DELETE("del", ctid);

		rc = sen_index_del(index, ctid);
		if (rc != sen_success)
			elog(WARNING, "senna: sen_index_del() : code=%d", rc);
		rc = sen_sym_del(index->keys, ctid);
		if (rc != sen_success)
			elog(WARNING, "senna: sen_sym_del() : code=%d", rc);
	}
}

/*
 * Total index size in bytes.
 */
static uint64
SennaIndexSize(sen_index *index)
{
	unsigned file_size_keys;
	unsigned file_size_lexicon;
	unsigned long long inv_seg_size;
	unsigned long long inv_chunk_size;
	sen_rc rc;

	rc = sen_index_info(index,
						NULL,	/* key_size */
						NULL,	/* flags */
						NULL,	/* initial_n_segments */
						NULL,	/* encoding */
						NULL,	/* nrecords_keys */
						&file_size_keys,
						NULL,	/* nrecords_lexicon */
						&file_size_lexicon,
						&inv_seg_size,
						&inv_chunk_size);

	if (rc != sen_success)
		return 0;

	return file_size_keys + file_size_lexicon +
		   inv_seg_size + inv_chunk_size;
}

static IndexBulkDeleteResult *
SennaBulkDeleteResult(sen_index *index)
{
	IndexBulkDeleteResult *stats;

	stats = (IndexBulkDeleteResult *) palloc0(sizeof(IndexBulkDeleteResult));
	stats->num_pages = (BlockNumber) (SennaIndexSize(index) / BLCKSZ);
	stats->num_index_tuples = sen_sym_size(index->keys);

	return stats;
}

/*
 * Release senna objects at the end of transactions.
 */
static void
SennaXactCallback(XactEvent event, void *arg)
{
	sen_rc		rc;
	int			i;

	/* cleanup records */
	if (records_cache != NIL)
	{
		ListCell   *cell;

		foreach(cell, records_cache)
		{
			SennaRecordsCache *cache = (SennaRecordsCache *) lfirst(cell);
			rc = sen_records_close(cache->records);
			if (rc != sen_success)
				elog(WARNING, "senna: sen_records_close() : code=%d", rc);
		}
		list_free_deep(records_cache);
		records_cache = NIL;
	}

	/* cleanup queries */
	for (i = 0; i < lengthof(query_cache); i++)
	{
		SennaQueryCache *cache = &query_cache[i];

		if (cache->query != NULL)
		{
			rc = sen_query_close(cache->query);
			if (rc != sen_success)
				elog(WARNING, "senna: sen_query_close() : code=%d", rc);
			cache->query = NULL;
		}
		if (cache->key)
		{
			free(cache->key);
			cache->key = NULL;
		}
	}
}

static void
SennaCacheRelCallback(Datum arg, Oid relid)
{
	SennaIndexClose(relid, NULL);
}

static void
SennaLogger(int level, const char *time, const char *title,
			const char *msg, const char *location, void *func_arg)
{
	int	elevel;

	const char *log_level[] =
	{
		"",			/* sen_log_none */
		"PANIC",	/* sen_log_emerg */
		"FATAL",	/* sen_log_alert */
		"CRITICAL",	/* sen_log_crit */
		"ERROR",	/* sen_log_error */
		"WARNING",	/* sen_log_warning */
		"NOTICE",	/* sen_log_notice */
		"INFO",		/* sen_log_info */
		"DEBUG",	/* sen_log_debug */
		"NOTICE"	/* sen_log_dump */
	};

	if (level <= sen_log_crit)
		elevel = WARNING;
	else
		elevel = LOG;

	elog(elevel, "senna: [%s]%s %s %s", log_level[level], title, msg, location);
}

static sen_logger_info senna_logger =
{
	sen_log_warning,
	SEN_LOG_TIME | SEN_LOG_MESSAGE,
	SennaLogger
};

void
_PG_init(void)
{
	sen_rc	rc;

#if PG_VERSION_NUM >= 80400
	RELOPT_KIND_SENNA = add_reloption_kind();
	add_int_reloption(RELOPT_KIND_SENNA,
		"initial_n_segments",
		"initial size of .sen.i",
		DEFAULT_INITIAL_N_SEGMENTS,
		1, INT_MAX);
#endif

	sen_logger_info_set(&senna_logger);
	rc = sen_init();
	if (rc != sen_success)
		elog(ERROR, "senna: sen_init() : code=%d", rc);

	RegisterXactCallback(SennaXactCallback, NULL);
	CacheRegisterRelcacheCallback(SennaCacheRelCallback, (Datum) 0);
}

void
_PG_fini(void)
{
#ifdef NOT_USED
	/* Somehow postgres doesn't support unregistration. */
	CacheUnregisterRelcacheCallback(SennaCacheRelCallback, (Datum) 0);
#endif
	UnregisterXactCallback(SennaXactCallback, NULL);
	sen_fin();
}
