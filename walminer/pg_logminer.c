/*-------------------------------------------------------------------------
 *
 * IDENTIFICATION
 *	  contrib/xlogminer/pg_logminer.c
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"

#include "pg_logminer.h"
#include "utils/builtins.h"
#include <dirent.h>
#include "logminer.h"
#include "catalog/pg_class.h"
#include "access/heapam.h"
#include "utils/relcache.h"
#include "catalog/pg_proc.h"
#include "catalog/pg_auth_members.h"
#include "access/transam.h"
#include "commands/dbcommands_xlog.h"
#include "datadictionary.h"
#include "xlogminer_contents.h"
#include "catalog/pg_namespace.h"
#include "catalog/pg_extension.h"
#include "catalog/pg_depend.h"
#include "catalog/pg_attrdef.h"
#include "catalog/pg_namespace.h"
#include "catalog/pg_tablespace.h"
#include "catalog/pg_database.h"
#include "catalog/pg_constraint.h"
#include "catalog/pg_type.h"
#include "storage/bufmgr.h"
#include "catalog/pg_control.h"
#include "common/pg_lzcompress.h"
#include "utils/relmapper.h"



RecordRecycleCtl 	rrctl;
SQLRecycleCtl		srctl;
uint32				sqlnoser;
FILE				*tempFileOpen = NULL;
bool				tempresultout = false;

char				globleStrInfo[PG_DEBUG_STRINFO_SIZE] = {0};
bool				debug_mode = false;
bool				log_mod = false;


static SystemClass sysclass[PG_LOGMINER_SYSCLASS_MAX];
static int sysclassNum = 0;
RelationKind *relkind_miner;

XLogMinerSQL	sql_reass;
XLogMinerSQL	sql;


SysClassLevel ImportantSysClass[] = {
	{PG_LOGMINER_IMPTSYSCLASS_PGCLASS, "pg_class", 0},
	{PG_LOGMINER_IMPTSYSCLASS_PGDATABASE, "pg_database", 0},
	{PG_LOGMINER_IMPTSYSCLASS_PGEXTENSION, "pg_extension", 0},
	{PG_LOGMINER_IMPTSYSCLASS_PGNAMESPACE, "pg_namespace", 0},
	{PG_LOGMINER_IMPTSYSCLASS_PGTABLESPACE, "pg_tablespace", 0},
	{PG_LOGMINER_IMPTSYSCLASS_PGCONSTRAINT, "pg_constraint", 0},
	{PG_LOGMINER_IMPTSYSCLASS_PGAUTHID, "pg_authid", 0},
	{PG_LOGMINER_IMPTSYSCLASS_PGPROC, "pg_proc", 0},
	{PG_LOGMINER_IMPTSYSCLASS_PGDEPEND, "pg_depend", 0},
	{PG_LOGMINER_IMPTSYSCLASS_PGINDEX, "pg_index", 0},
	{PG_LOGMINER_IMPTSYSCLASS_PGATTRIBUTE, "pg_attribute", 0},
	{PG_LOGMINER_IMPTSYSCLASS_PGSHDESC, "pg_shdescription", 0},
	{PG_LOGMINER_IMPTSYSCLASS_PGATTRDEF, "pg_attrdef", 0},
	{PG_LOGMINER_IMPTSYSCLASS_PGTYPE, "pg_type", 0},
	{PG_LOGMINER_IMPTSYSCLASS_PGAUTH_MEMBERS, "pg_auth_members", 0},
	{PG_LOGMINER_IMPTSYSCLASS_PGINHERITS, "pg_inherits", 0},
	{PG_LOGMINER_IMPTSYSCLASS_PGTRIGGER, "pg_trigger", 0},
	{PG_LOGMINER_IMPTSYSCLASS_PGLANGUAGE, "pg_language", 0}
};

static SQLKind sqlkind[] = {
	{"UPDATE",PG_LOGMINER_SQLKIND_UPDATE},
	{"INSERT",PG_LOGMINER_SQLKIND_INSERT},
	{"DELETE",PG_LOGMINER_SQLKIND_DELETE},
	{"CREATE",PG_LOGMINER_SQLKIND_CREATE},
	{"ALTER",PG_LOGMINER_SQLKIND_ALTER},
	{"XACT",PG_LOGMINER_SQLKIND_XACT},
	{"DROP",PG_LOGMINER_SQLKIND_DROP}
};



PG_MODULE_MAGIC;

static bool getNextRecord();
static bool sqlParser(XLogReaderState *record, TimestampTz 	*xacttime);
static bool getBlockImage(XLogReaderState *record, uint8 block_id, char *page);
static bool getImageFromStore(RelFileNode *rnode, ForkNumber forknum, BlockNumber blkno, char* page, int *index);
static bool imageEqueal(ImageStore *image1, ImageStore *image2);
static void flushPage(int index, char* page);
static void cleanStorefile(void);
static void readPage(int index, char* page);
static void appendImage(ImageStore *image, char* page);
static void recordStoreImage(XLogReaderState *record);
static char* getTupleFromImage_insert(XLogReaderState *record, Page page ,Size *len);
//static char* getTupleFromImage_delete(XLogReaderState *record, Page page ,Size *len);
static char* getTupleFromImage_update(XLogReaderState *record, Page page ,Size *len, bool new);
static char* getTupleFromImage_mutiinsert(XLogReaderState *record, Page page ,Size *len, OffsetNumber index);
static void fix_infomask_from_infobits(uint8 infobits, uint16 *infomask, uint16 *infomask2);
static void pageInitInXlog(RelFileNode *rnode, ForkNumber forknum, BlockNumber blkno);
static void minerHeap2Clean(XLogReaderState *record, uint8 info);
static void heap_page_prune_execute_logminer(Page page,
						OffsetNumber *redirected, int nredirected,
						OffsetNumber *nowdead, int ndead,
						OffsetNumber *nowunused, int nunused);
static void closeTempleResult(void);


/*
PG_FUNCTION_INFO_V1(pg_xlog2sql);
*/
PG_FUNCTION_INFO_V1(xlogminer_build_dictionary);
PG_FUNCTION_INFO_V1(xlogminer_load_dictionary);
PG_FUNCTION_INFO_V1(xlogminer_stop);
PG_FUNCTION_INFO_V1(xlogminer_xlogfile_add);
PG_FUNCTION_INFO_V1(xlogminer_xlogfile_list);
PG_FUNCTION_INFO_V1(xlogminer_xlogfile_remove);
PG_FUNCTION_INFO_V1(pg_minerXlog);


SysClassLevel *getImportantSysClass(void)
{
	return ImportantSysClass;
}

void
getWalSegSz(char* path)
{
	FILE	*fp = NULL;
	PGAlignedXLogBlock buf;

	memset(&buf, 0, sizeof(PGAlignedXLogBlock));

	Assert(path);
	fp = fopen(path, "r");
	if(!fp)
	{
		elog(ERROR,"can not open file %s to read", path);
	}
	if (XLOG_BLCKSZ == fread(buf.data, 1, XLOG_BLCKSZ, fp))
	{
		XLogLongPageHeader longhdr = (XLogLongPageHeader) buf.data;

		rrctl.WalSegSz = longhdr->xlp_seg_size;

		if (!IsValidWalSegSize(rrctl.WalSegSz))
			elog(ERROR,"wrong walsegsize %d",rrctl.WalSegSz);
	}
	else
	{
		if (errno != 0)
			elog(ERROR,"could not read file \"%s\": %s", path, strerror(errno));
		else
			elog(ERROR,"not enough data in file \"%s\"", path);
	}
	fclose(fp);
}
/*
void log_printf_long(long para){
if(!tempFileOpen)
	return;
fprintf(tempFileOpen,"%ld\n",para);
fflush(tempFileOpen);


}
void log_printf_str(char *str){
	if(!tempFileOpen)
		return;

fprintf(tempFileOpen,"%s\n",str);
fflush(tempFileOpen);


}

void log_printf_long1(long para){
if(!tempFileOpen)
	return;
fprintf(tempFileOpen,"%ld",para);
fflush(tempFileOpen);

}
void log_printf_str1(char *str){
	if(!tempFileOpen)
		return;

fprintf(tempFileOpen,"%s",str);
fflush(tempFileOpen);


}
*/

void 
outTempleResult(char *str)
{

	char	tempresultfile[MAXPGPATH] = {0};
	char	*path = PG_LOGMINER_PATH;
	char	*filename = PG_LOGMINER_TEMPRESULT_FILENAME;

	if(!str)
		return;
	sprintf(tempresultfile,"%s/temp/%s", path, filename);
	if(!tempFileOpen)
	{
		tempFileOpen = fopen(tempresultfile, "w");
		if(!tempFileOpen)
			elog(ERROR,"can not open file %s to write",filename);
	}
	fprintf(tempFileOpen,"%s\n",str);
	fflush(tempFileOpen);
}

void
outVar(void *var, int kind)
{
	char	str[1024] = {0};

	if(!tempresultout)
		return;
	if(1 == kind)
	{
		RelFileNode *rfn = NULL;

		rfn = (RelFileNode *)var;
		sprintf(str, "RelFileNode=%u/%u/%u", rfn->spcNode, rfn->dbNode, rfn->relNode);
	}
	else if(2 == kind)
	{
		sprintf(str, "BlockNum=%u", *(uint32*)var);
	}
	else if(3 == kind)
	{
		sprintf(str, "OffSetNum=%hu", *(uint32*)var);
	}
	else if(4 ==kind)
	{	
		ImageStore *image = NULL;

		image = (ImageStore *)var;
		sprintf(str, "[appendimage] relfileoid=%u,blkno=%d\n", image->rnode.relNode, image->blkno);
	}
	else if(5 == kind)
	{
		sprintf(str, "loop=%u", *(int32*)var);
	}
	outTempleResult(str);
}

static void 
closeTempleResult(void)
{
	if(tempFileOpen)
	{
		fclose(tempFileOpen);
		tempFileOpen = NULL;
	}
}



static void
fix_infomask_from_infobits(uint8 infobits, uint16 *infomask, uint16 *infomask2)
{
	*infomask &= ~(HEAP_XMAX_IS_MULTI | HEAP_XMAX_LOCK_ONLY |
				   HEAP_XMAX_KEYSHR_LOCK | HEAP_XMAX_EXCL_LOCK);
	*infomask2 &= ~HEAP_KEYS_UPDATED;

	if (infobits & XLHL_XMAX_IS_MULTI)
		*infomask |= HEAP_XMAX_IS_MULTI;
	if (infobits & XLHL_XMAX_LOCK_ONLY)
		*infomask |= HEAP_XMAX_LOCK_ONLY;
	if (infobits & XLHL_XMAX_EXCL_LOCK)
		*infomask |= HEAP_XMAX_EXCL_LOCK;
	/* note HEAP_XMAX_SHR_LOCK isn't considered here */
	if (infobits & XLHL_XMAX_KEYSHR_LOCK)
		*infomask |= HEAP_XMAX_KEYSHR_LOCK;

	if (infobits & XLHL_KEYS_UPDATED)
		*infomask2 |= HEAP_KEYS_UPDATED;
}


/*
*append a string to  XLogMinerSQL
*/
void
appendtoSQL(XLogMinerSQL *sql_simple, char *sqlpara , int spaceKind)
{

	int addsize;

	if(PG_LOGMINER_SQLPARA_OTHER != spaceKind && PG_LOGMINER_XLOG_DBINIT == rrctl.system_init_record)
		return;
	
	if(NULL == sqlpara || 0 == strcmp("",sqlpara) || 0 == strcmp(" ",sqlpara) )
		sqlpara = "NULL";
	
	addsize = strlen(sqlpara);

	while(addsize >= sql_simple->rem_size)
	{
		addSpace(sql_simple,spaceKind);
	}

	memcpy(sql_simple->sqlStr + sql_simple->use_size ,sqlpara ,addsize);
	sql_simple->use_size += addsize;
	sql_simple->rem_size -= addsize;
}

void
appendtoSQL_simquo(XLogMinerSQL *sql_simple, char* ptr, bool quoset)
{
	if(quoset)
		appendtoSQL(sql_simple, "\'", PG_LOGMINER_SQLPARA_OTHER);
	appendtoSQL(sql_simple, ptr, PG_LOGMINER_SQLPARA_OTHER);
	if(quoset)
		appendtoSQL(sql_simple, "\'", PG_LOGMINER_SQLPARA_OTHER);
}

void
appendtoSQL_doubquo(XLogMinerSQL *sql_simple, char* ptr, bool quoset)
{
	if(quoset)
		appendtoSQL(sql_simple, "\"", PG_LOGMINER_SQLPARA_SIMPLE);
	appendtoSQL(sql_simple, ptr, PG_LOGMINER_SQLPARA_SIMPLE);
	if(quoset)
		appendtoSQL(sql_simple, "\"", PG_LOGMINER_SQLPARA_SIMPLE);
}

void
appendtoSQL_atttyptrans(XLogMinerSQL *sql_simple, Oid typoid)
{
	if(POINTOID == typoid || JSONOID == typoid || (POLYGONOID == typoid) || XMLOID == typoid)
		appendtoSQL(sql_simple, "::text", PG_LOGMINER_SQLPARA_SIMPLE);
		
}

void
appendtoSQL_valuetyptrans(XLogMinerSQL *sql_simple, Oid typoid)
{
	if(FLOAT4OID == typoid)
		appendtoSQL(sql_simple, "::float4", PG_LOGMINER_SQLPARA_SIMPLE);
}


/*
* Delete a char from XLogMinerSQL
*/
void
deleteCharFromSQL(XLogMinerSQL *sql_simple)
{
	if(sql_simple->use_size < 1)
		return;
	sql_simple->sqlStr[sql_simple->use_size - 1] = 0;
	sql_simple->use_size--;
	sql_simple->rem_size++;
}


/*
*
* Wipe some string from XLogMinerSQL.
* For example
* sql_simple.sqlStr="delete from t1 where values"
* fromstr="where values"
* checkstr="where "
* then sql_simple.sqlStr become "delete from t1 where "
*/
void
wipeSQLFromstr(XLogMinerSQL *sql_simple,char *fromstr,char *checkstr)
{
	char	*strPtr = NULL;
	int 	length_ptr = 0;

	if(NULL == sql_simple || NULL == sql_simple->sqlStr ||NULL == fromstr)
		return;
	strPtr = strstr(sql_simple->sqlStr,fromstr);
	if(NULL == strPtr)
		return;
	strPtr = strPtr + strlen(checkstr);
	length_ptr = strlen(strPtr);
	memset(strPtr, 0, length_ptr);
	sql_simple->use_size -= length_ptr;
	sql_simple->rem_size += length_ptr;
}

/*
* Append a space to XLogMinerSQL
*/
void
appendBlanktoSQL(XLogMinerSQL *sql_simple)
{

	int addsize;
	char *sqlpara = " ";
	
	addsize = strlen(sqlpara);
	while(addsize >= sql_simple->rem_size)
	{
		addSpace(sql_simple,PG_LOGMINER_SQLPARA_OTHER);
	}
	memcpy(sql_simple->sqlStr + sql_simple->use_size ,sqlpara ,addsize);
	sql_simple->use_size += addsize;
	sql_simple->rem_size -= addsize;
}

static bool
tableIfSysclass(char *tablename, Oid reloid)
{
	int loop;
	if(FirstNormalObjectId < reloid)
		return false;
	for(loop = 0; loop < sysclassNum; loop++)
	{
		if(0 == strcmp(sysclass[loop].classname.data,tablename))
		{
			return true;
		}
	}
	return false;
}

static bool
tableifImpSysclass(char *tablename, Oid reloid)
{
	int loop;
	if(FirstNormalObjectId < reloid)
		return false;
	for(loop = 0; loop < PG_LOGMINER_IMPTSYSCLASS_IMPTNUM; loop++)
	{
		if(0 == strcmp(ImportantSysClass[loop].relname,tablename))
		{
			return true;
		}
	}
	return false;
}


static bool
imageEqueal(ImageStore *image1, ImageStore *image2)
{
	Assert(image1 && image2);

	if(image1->forknum != image2->forknum || image1->blkno != image2->blkno)
		return false;
	if(0 != memcmp(&image1->rnode, &image2->rnode, sizeof(RelFileNode)))
		return false;

	return true;
}

static bool
getImageFromStore(RelFileNode *rnode, ForkNumber forknum, BlockNumber blkno, char* page, int *index)
{
	ImageStore	images;
	int			loop = 0;
	int			listlength = 0;
	ListCell	*lc = NULL;
	ImageStore	*imagesPtr = NULL, *imagesTar = NULL;

	Assert(rnode);
	Assert(page);
	memset(&images, 0, sizeof(ImageStore));

	memcpy(&images.rnode, rnode, sizeof(RelFileNode));
	images.forknum = forknum;
	images.blkno = blkno;


	listlength = list_length(rrctl.imagelist);

	for(; loop < listlength; loop++)
	{
		lc = lc?(lnext(lc)):(list_head(rrctl.imagelist));
		imagesPtr = (ImageStore*)lfirst(lc);
		if(imageEqueal(&images, imagesPtr))
		{
			imagesTar = imagesPtr;
			break;
		}
	}

	if(!imagesTar)
	{
		if(debug_mode)
		{
			memset(globleStrInfo, 0, PG_DEBUG_STRINFO_SIZE);
			sprintf(globleStrInfo, "[getImageFromStore]page:relnode %u, blckno %d NOT FOUND", rnode->relNode, blkno);
			outTempleResult(globleStrInfo);
		}

		return false;
	}

	readPage(loop, page);
	*index = loop;
	if(debug_mode)
	{
		memset(globleStrInfo, 0, PG_DEBUG_STRINFO_SIZE);
		sprintf(globleStrInfo, "[getImageFromStore]page:relnode %u, blckno %d,index %d", rnode->relNode, blkno, *index);
		outTempleResult(globleStrInfo);
	}
	return true;
}

static void
flushPage(int index, char* page)
{
	char	storefile[MAXPGPATH] = {0};
	char	*path = PG_LOGMINER_PATH;
	char	*filename = "soreimage";
	FILE	*fp = NULL;

	Assert(page);
	sprintf(storefile,"%s/%s", path, filename);
	if(!create_dir(path))
	{
		elog(ERROR,"fail to create dir walminer under %s", DataDir);
	}

	fp = fopen(storefile, "rb+");
	if(!fp)
	{
		elog(ERROR,"fail to open file %s to write", storefile);
	}	
	fseek(fp, 0, SEEK_SET);
	Assert(0 == ftell(fp));
	Assert(0 <= index);
	if(0 != index)
		fseek(fp, index * BLCKSZ, SEEK_SET);
	if(BLCKSZ != fwrite(page, 1, BLCKSZ, fp))
	{
		elog(ERROR,"fail to write to %s", storefile);
	}
	fclose(fp);
	fp = NULL;
}

static void
cleanStorefile(void)
{
	char	storefile[MAXPGPATH] = {0};
	char	*path = PG_LOGMINER_PATH;
	char	*filename = PG_LOGMINER_STOREIMAGE_FILENAME;
	FILE	*fp = NULL;

	sprintf(storefile,"%s/%s", path, filename);
	fp = fopen(storefile, "wb");
	if(!fp)
	{
		elog(ERROR,"fail to clean file %s", storefile);
	}
	fclose(fp);
	fp = NULL;
}


static void
readPage(int index, char* page)
{
	char	storefile[MAXPGPATH] = {0};
	char	*path = PG_LOGMINER_PATH;
	char	*filename = PG_LOGMINER_STOREIMAGE_FILENAME;
	FILE	*fp = NULL;

	Assert(page);
	sprintf(storefile,"%s/%s", path, filename);

	fp = fopen(storefile, "rb");
	if(!fp)
	{
		elog(ERROR,"fail to open file %s to read", storefile);
	}
	fseek(fp, 0, SEEK_SET);
	Assert(0 == ftell(fp));
	Assert(0 <= index);
	if(0 != index)
		fseek(fp, index * BLCKSZ, SEEK_SET);
	if(BLCKSZ != fread(page, 1, BLCKSZ, fp))
	{
		elog(ERROR,"fail to read %s", storefile);
	}

	fclose(fp);
	fp = NULL;
}

static void
heap_page_prune_execute_logminer(Page page,
						OffsetNumber *redirected, int nredirected,
						OffsetNumber *nowdead, int ndead,
						OffsetNumber *nowunused, int nunused)
{
	OffsetNumber *offnum;
	int			i;

	/* Update all redirected line pointers */
	offnum = redirected;
	for (i = 0; i < nredirected; i++)
	{
		OffsetNumber fromoff = *offnum++;
		OffsetNumber tooff = *offnum++;
		ItemId		fromlp = PageGetItemId(page, fromoff);

		ItemIdSetRedirect(fromlp, tooff);
	}

	/* Update all now-dead line pointers */
	offnum = nowdead;
	for (i = 0; i < ndead; i++)
	{
		OffsetNumber off = *offnum++;
		ItemId		lp = PageGetItemId(page, off);

		ItemIdSetDead(lp);
	}

	/* Update all now-unused line pointers */
	offnum = nowunused;
	for (i = 0; i < nunused; i++)
	{
		OffsetNumber off = *offnum++;
		ItemId		lp = PageGetItemId(page, off);

		ItemIdSetUnused(lp);
	}

	/*
	 * Finally, repair any fragmentation, and update the page's hint bit about
	 * whether it has free pointers.
	 */
	PageRepairFragmentation(page);
}


static void
appendImage(ImageStore *image, char* page)
{
	int			listlength = 0;
	int			loop = 0;
	ImageStore 	*imagePtr = NULL;
	ListCell	*lc = NULL;
	static int	imageondisklast = 0;
	int			reporstep = (512 * 1024 * 1024) / BLCKSZ;	//512M image on disk
	
	if(!rrctl.logprivate.staptr_reached)
		return;
	listlength = list_length(rrctl.imagelist);

	for(; loop < listlength; loop++)
	{
		lc = lc?(lnext(lc)):(list_head(rrctl.imagelist));
		imagePtr = (ImageStore*)lfirst(lc);
		if(imageEqueal(image,imagePtr))
		{
			pfree(image);
			flushPage(loop, page);
			return;
		}
	}
	flushPage(loop, page);
	outVar((void*)image, 4);
	outVar((void*)&loop, 5);
	rrctl.imagelist = lappend(rrctl.imagelist, image);
	if(imageondisklast + reporstep <= rrctl.imagelist->length)
	{
		imageondisklast = rrctl.imagelist->length;
		elog(NOTICE, "there be %d image pages on disk", imageondisklast);
	}
}

static void
pageInitInXlog(RelFileNode *rnode, ForkNumber forknum, BlockNumber blkno)
{
	ImageStore 		*image = NULL;
	char 			page[BLCKSZ] = {0};
	
	PageInit(page, BLCKSZ, 0);

	image = palloc(sizeof(ImageStore));
	image->rnode.dbNode = rnode->dbNode;
	image->rnode.relNode = rnode->relNode;
	image->rnode.spcNode = rnode->spcNode;
	if(debug_mode)
	{
		memset(globleStrInfo, 0, PG_DEBUG_STRINFO_SIZE);
		sprintf(globleStrInfo, "[pageInitInXlog]initpage:relnode %u, blckno %d",rnode->relNode, blkno);
		outTempleResult(globleStrInfo);
	}

	image->forknum = forknum;
	image->blkno = blkno;
	appendImage(image, page);
}


/*
 * when get a record we check if there be some image in this record,
 * if so we store the image and the location of record.
 */
static void
recordStoreImage(XLogReaderState *record)
{
	uint8			block_id = 0;
	DecodedBkpBlock *bkpb = NULL;
	ImageStore 		*image = NULL;
	char			page[BLCKSZ] = {0};
	char			strinfo[1024] = {0};

	if(tempresultout && record->decoded_record)
	{
		sprintf(strinfo, "\nRMGRID:%d INFO=%x",XLogRecGetRmid(record), XLogRecGetInfo(record) & ~XLR_INFO_MASK);
		outTempleResult(strinfo);
	}
	
	for(block_id = 0; block_id <= XLR_MAX_BLOCK_ID; block_id++)
	{
		bkpb = &record->blocks[block_id];
		if(!bkpb->in_use)
			return;
		if(!bkpb->has_image)
			continue;
		memset(page, 0, BLCKSZ);
		image = palloc0(sizeof(ImageStore));
		image->rnode.dbNode = bkpb->rnode.dbNode;
		image->rnode.relNode = bkpb->rnode.relNode;
		image->rnode.spcNode = bkpb->rnode.spcNode;

		image->forknum = bkpb->forknum;
		image->blkno = bkpb->blkno;
//		image->EndRecPtr = record->EndRecPtr;
//		image->ReadRecPtr = record->ReadRecPtr;

		if(getBlockImage(record, block_id, page))
			appendImage(image, page);
	}
}


static bool
getBlockImage(XLogReaderState *record, uint8 block_id, char *page)
{
	DecodedBkpBlock *bkpb;
	char	   *ptr;
	char		tmp[BLCKSZ];

	if (!record->blocks[block_id].in_use)
		return false;
	if (!record->blocks[block_id].has_image)
		return false;

	bkpb = &record->blocks[block_id];
	ptr = bkpb->bkp_image;

	if (bkpb->bimg_info & BKPIMAGE_IS_COMPRESSED)
	{
		/* If a backup block image is compressed, decompress it */
		if (pglz_decompress(ptr, bkpb->bimg_len, tmp,
							BLCKSZ - bkpb->hole_length) < 0)
		{
			elog(LOG, "invalid compressed image at %X/%X, block %d",
								  (uint32) (record->ReadRecPtr >> 32),
								  (uint32) record->ReadRecPtr,
								  block_id);
			return false;
		}
		ptr = tmp;
	}

	/* generate page, taking into account hole if necessary */
	if (bkpb->hole_length == 0)
	{
		memcpy(page, ptr, BLCKSZ);
	}
	else
	{
		memcpy(page, ptr, bkpb->hole_offset);
		/* must zero-fill the hole */
		MemSet(page + bkpb->hole_offset, 0, bkpb->hole_length);
		memcpy(page + (bkpb->hole_offset + bkpb->hole_length),
			   ptr + bkpb->hole_offset,
			   BLCKSZ - (bkpb->hole_offset + bkpb->hole_length));
	}

	return true;
}


static char*
getTupleFromImage_insert(XLogReaderState *record, Page page ,Size *len)
{
	xl_heap_insert 			*xlrec = NULL;
	ForkNumber				forknum = InvalidForkNumber;
	BlockNumber 			blkno = InvalidBlockNumber;
	uint8 					block_id = 0;
	HeapTupleHeader			tuphdr = NULL;
	ItemId					id;
	RelFileNode 			rnode;
	
	memset(&rnode, 0, sizeof(RelFileNode));

	if (!XLogRecGetBlockTag(record, block_id, &rnode, &forknum, &blkno))
	{
		/* Caller specified a bogus block_id */
		elog(ERROR, "failed to locate backup block with ID %d", block_id);
	}

	if (!XLogRecHasBlockImage(record, block_id))
	{
		int	index = 0;
		if(!getImageFromStore(&rnode, forknum, blkno, (char*)page, &index))
			return NULL;
	}
	else
		getBlockImage(record, block_id, page);
	
	xlrec = (xl_heap_insert*)XLogRecGetData(record);

	id = PageGetItemId(page, xlrec->offnum);
	tuphdr = (HeapTupleHeader)PageGetItem(page, id);
	*len = (Size)id->lp_len;

	return (char*)tuphdr;
}
/*
static char*
getTupleFromImage_delete(XLogReaderState *record, Page page ,Size *len)
{
	xl_heap_delete			*xlrec = NULL;
	ForkNumber				forknum = InvalidForkNumber;
	BlockNumber 			blkno = InvalidBlockNumber;
	uint8 					block_id = 0;
	HeapTupleHeader			tuphdr = NULL;
	ItemId					id;
	RelFileNode 			rnode;
	
	memset(&rnode, 0, sizeof(RelFileNode));

	if (!XLogRecGetBlockTag(record, block_id, &rnode, &forknum, &blkno))
	{
		elog(ERROR, "failed to locate backup block with ID %d", block_id);
	}

	if (!XLogRecHasBlockImage(record, block_id))
	{
		int	index = 0;
		if(!getImageFromStore(&rnode, forknum, blkno, (char*)page, &index))
				return NULL;
	}
	else
		getBlockImage(record, block_id, page);
	
	xlrec = (xl_heap_delete*)XLogRecGetData(record);

	id = PageGetItemId(page, xlrec->offnum);
	tuphdr = (HeapTupleHeader)PageGetItem(page, id);
	*len = (Size)id->lp_len;
	return (char*)tuphdr;
}
*/
static char*
getTupleFromImage_update(XLogReaderState *record, Page page ,Size *len, bool new)
{
	xl_heap_update			*xlrec = NULL;
	ForkNumber				forknum = InvalidForkNumber;
	BlockNumber 			blkno = InvalidBlockNumber;
	uint8					block_id = 0;
	HeapTupleHeader 		tuphdr = NULL;
	ItemId					id;
	RelFileNode 			rnode;
	
	memset(&rnode, 0, sizeof(RelFileNode));

	if(new)
	{
		block_id = 0;
		if (!XLogRecGetBlockTag(record, block_id, &rnode, &forknum, &blkno))
		{
			/* Caller specified a bogus block_id */
			elog(ERROR, "failed to locate backup block with ID %d", block_id);
		}
	}
	else
	{
		block_id = 1;
		if (!XLogRecGetBlockTag(record, block_id, &rnode, &forknum, &blkno))
		{
			block_id  = 0;
			if (!XLogRecGetBlockTag(record, block_id, &rnode, &forknum, &blkno))
			{
				/* Caller specified a bogus block_id */
				elog(ERROR, "failed to locate backup block with ID %d", block_id);
			}
		}
	}
	xlrec = (xl_heap_update*)XLogRecGetData(record);
	if (!XLogRecHasBlockImage(record, block_id))
	{
		int index = 0;
		/*occur when XLH_UPDATE_PREFIX_FROM_OLD or XLH_UPDATE_SUFFIX_FROM_OLD*/
		
		if(!getImageFromStore(&rnode, forknum, blkno, (char*)page, &index))
				return NULL;
	}
	else
	{
		getBlockImage(record, block_id, page);
	}


	if(new)
	{
		id = PageGetItemId(page, xlrec->new_offnum);
		tuphdr = (HeapTupleHeader)PageGetItem(page, id);
	}
	else
	{
		id = PageGetItemId(page, xlrec->old_offnum);
		tuphdr = (HeapTupleHeader)PageGetItem(page, id);
	}

	
	*len = (Size)id->lp_len;
	return (char*)tuphdr;
}

static char*
getTupleFromImage_mutiinsert(XLogReaderState *record, Page page ,Size *len, OffsetNumber offnum)
{
	ForkNumber				forknum = InvalidForkNumber;
	BlockNumber 			blkno = InvalidBlockNumber;
	uint8					block_id = 0;
	HeapTupleHeader 		tuphdr = NULL;
	ItemId					id;
	RelFileNode 			rnode;
	
	memset(&rnode, 0, sizeof(RelFileNode));

	if (!XLogRecGetBlockTag(record, block_id, &rnode, &forknum, &blkno))
	{
		/* Caller specified a bogus block_id */
		elog(ERROR, "failed to locate backup block with ID %d", block_id);
	}

	if (!XLogRecHasBlockImage(record, block_id))
	{
		int index = 0;
		if(!getImageFromStore(&rnode, forknum, blkno, (char*)page, &index))
		{
			return NULL;
		}
		if(debug_mode)
		{
			memset(globleStrInfo, 0, PG_DEBUG_STRINFO_SIZE);
			sprintf(globleStrInfo, "[getTupleFromImage_mutiinsert]page:relnode %u, blckno %d,block_id %d,index%d", rnode.relNode, blkno,block_id,index);
			outTempleResult(globleStrInfo);
		}
	}
	else
	{
		getBlockImage(record, block_id, page);
	}

	id = PageGetItemId(page, offnum);
	tuphdr = (HeapTupleHeader)PageGetItem(page, id);
	*len = (Size)id->lp_len;

	return (char*)tuphdr;
}



/*
*	Get useful data from record,and return insert data by tuple_info. 
*/
static void 
getTupleData_Insert(XLogReaderState *record, char** tuple_info, Oid reloid)
{
	HeapTupleData 			tupledata;
	char					*tuplem = NULL;
	char					*data = NULL;
	TupleDesc				tupdesc = NULL;
	Size					datalen = 0;
	uint32					newlen = 0;
	HeapTupleHeader 		htup;
	xl_heap_header 			xlhdr;
	RelFileNode 			target_node;
	BlockNumber 			blkno;
	ItemPointerData 		target_tid;
	xl_heap_insert 			*xlrec = (xl_heap_insert *) XLogRecGetData(record);
	char					page[BLCKSZ] = {0};

	outVar((void*)&xlrec->offnum, 3);
	if(!rrctl.tupinfo_init)
	{
		memset(&rrctl.tupinfo, 0, sizeof(XLogMinerSQL));
		rrctl.tupinfo_init = true;
	}
	else
		cleanSpace(&rrctl.tupinfo);
	memset(&tupledata, 0, sizeof(HeapTupleData));
	XLogRecGetBlockTag(record, 0, &target_node, NULL, &blkno);
	ItemPointerSetBlockNumber(&target_tid, blkno);
	ItemPointerSetOffsetNumber(&target_tid, xlrec->offnum);

	tuplem = rrctl.tuplem;
	rrctl.reloid = reloid;
	htup = (HeapTupleHeader)tuplem;
	
	data = XLogRecGetBlockData(record, 0, &datalen);
	if(XLogRecHasBlockImage(record, 0))
	{
		data = getTupleFromImage_insert(record, page, &datalen);
		if(!data)
		{
			/*should not arrive here.*/
			elog(ERROR,"can not find imagestore about reloid=%u,blckno=%u", target_node.relNode, blkno);
		}

		newlen = datalen;
		Assert(datalen > SizeOfHeapHeader && newlen <= MaxHeapTupleSize);
		memcpy((char *) htup , data, newlen);
	}
	else
	{
		int index = 0;

		if(XLOG_HEAP_INIT_PAGE & XLogRecGetInfo(record))
		{
			pageInitInXlog(&target_node, MAIN_FORKNUM, blkno);
		}
		do
		{
			newlen = datalen - SizeOfHeapHeader;

			Assert(datalen > SizeOfHeapHeader && newlen <= MaxHeapTupleSize);
			memcpy((char *) &xlhdr, data, SizeOfHeapHeader);
			data += SizeOfHeapHeader;
			memcpy((char *) htup + SizeofHeapTupleHeader,data,newlen);
			htup->t_infomask2 = xlhdr.t_infomask2;
			htup->t_infomask = xlhdr.t_infomask;
			htup->t_hoff = xlhdr.t_hoff;
			htup->t_ctid = target_tid;
			HeapTupleHeaderSetXmin(htup, XLogRecGetXid(record));
			HeapTupleHeaderSetCmin(htup, FirstCommandId);
			newlen += SizeofHeapTupleHeader;
			if(debug_mode)
			{
				memset(globleStrInfo, 0, PG_DEBUG_STRINFO_SIZE);
				sprintf(globleStrInfo, "[getTupleData_Insert]PageAddItem:offsetnum %d,relnode %u, blckno %d",xlrec->offnum, target_node.relNode, blkno);
				outTempleResult(globleStrInfo);
			}
			if(!getImageFromStore(&target_node, MAIN_FORKNUM, blkno, (char*)page, &index))
			{
				continue;
			}
			if(InvalidOffsetNumber == PageAddItem(page, (Item) htup, newlen, xlrec->offnum, true, true))
				elog(ERROR, "failed to add tuple:offsetnum %d,relnode %u, blckno %d", xlrec->offnum, target_node.relNode, blkno);
			flushPage(index, page);
		}while(false);
	}

	if(rrctl.tupdesc)
	{
		freetupdesc(rrctl.tupdesc);
		rrctl.tupdesc = NULL;
	}
		
	rrctl.tupdesc = GetDescrByreloid(reloid);
	tupdesc = rrctl.tupdesc;

	if(NULL != htup)
	{
		tupledata.t_data = htup;
		if(rrctl.toastrel)
		{
			/*if it is insert into a toast table,store it into list*/
			Oid		chunk_id;
			int		chunk_seq;
			char	*chunk_data;
			bool	isnull = false;

			chunk_id = DatumGetObjectId(fastgetattr(&tupledata, 1, tupdesc, &isnull));
			chunk_seq =DatumGetInt32(fastgetattr(&tupledata, 2, tupdesc, &isnull));
			chunk_data = DatumGetPointer(fastgetattr(&tupledata, 3, tupdesc, &isnull));
			toastTupleAddToList(makeToastTuple(VARSIZE(chunk_data) - VARHDRSZ, VARDATA(chunk_data), chunk_id, chunk_seq, rrctl.reloid));
			return;
		}
		else
		{
			rrctl.sqlkind = PG_LOGMINER_SQLKIND_INSERT;
			mentalTup(&tupledata, tupdesc, &rrctl.tupinfo, false);
			*tuple_info = rrctl.tupinfo.sqlStr;
			rrctl.sqlkind = 0;
		}
	}
}

/*
*	Get useful data from record,and return delete data by tuple_info. 
*/

static void 
getTupleData_Delete(XLogReaderState *record, char** tuple_info, Oid reloid)
{
	HeapTupleData			tupledata;
	char					*tuplem = NULL;
//	char					*data = NULL;
	TupleDesc				tupdesc = NULL;
	uint16					newlen = 0;
	HeapTupleHeader 		htup = NULL;
//	xl_heap_header			xlhdr;
	RelFileNode 			target_node;
	BlockNumber 			blkno;
	ItemPointerData 		target_tid;
//	Size					datalen = 0;
	char					page[BLCKSZ] = {0};
	ItemId					lp = NULL;
	int						index = 0;
	xl_heap_delete			*xlrec = (xl_heap_delete *) XLogRecGetData(record);

	outVar((void*)&xlrec->offnum, 3);
	if(!rrctl.tupinfo_init)
	{
		memset(&rrctl.tupinfo, 0, sizeof(XLogMinerSQL));
		rrctl.tupinfo_init = true;
	}
	else
		cleanSpace(&rrctl.tupinfo);
	memset(&tupledata, 0, sizeof(HeapTupleData));

	XLogRecGetBlockTag(record, 0, &target_node, NULL, &blkno);
	ItemPointerSetBlockNumber(&target_tid, blkno);
	ItemPointerSetOffsetNumber(&target_tid, xlrec->offnum);
	rrctl.reloid = reloid;
	do
	{
		if(!getImageFromStore(&target_node, MAIN_FORKNUM, blkno, (char*)page, &index))
		{
			break;
		}
		lp = PageGetItemId(page, xlrec->offnum);
		newlen = lp->lp_len;
		htup = (HeapTupleHeader) PageGetItem(page, lp);

		if (!XLogRecHasBlockImage(record, 0))
		{
			htup->t_infomask &= ~(HEAP_XMAX_BITS | HEAP_MOVED);
			htup->t_infomask2 &= ~HEAP_KEYS_UPDATED;
			HeapTupleHeaderClearHotUpdated(htup);
			fix_infomask_from_infobits(xlrec->infobits_set,
									   &htup->t_infomask, &htup->t_infomask2);
			if (!(xlrec->flags & XLH_DELETE_IS_SUPER))
				HeapTupleHeaderSetXmax(htup, xlrec->xmax);
			else
				HeapTupleHeaderSetXmin(htup, InvalidTransactionId);
			HeapTupleHeaderSetCmax(htup, FirstCommandId, false);

			/* Mark the page as a candidate for pruning */
			PageSetPrunable(page, XLogRecGetXid(record));

			if (xlrec->flags & XLH_DELETE_ALL_VISIBLE_CLEARED)
				PageClearAllVisible(page);

			/* Make sure there is no forward chain link in t_ctid */
			htup->t_ctid = target_tid;
			flushPage(index, page);
		}

		if(newlen + SizeOfHeapDelete > MaxHeapTupleSize)
		{
			rrctl.tuplem_bigold = getTuplemSpace(newlen + SizeOfHeapDelete + SizeofHeapTupleHeader);
			tuplem = rrctl.tuplem_bigold;
		}
		else
			tuplem = rrctl.tuplem;
		memcpy((char *) tuplem, htup, newlen);
		htup = (HeapTupleHeader) tuplem;
		
		if(rrctl.tupdesc)
		{
			freetupdesc(rrctl.tupdesc);
			rrctl.tupdesc = NULL;
		}

		rrctl.tupdesc = GetDescrByreloid(reloid);
		tupdesc = rrctl.tupdesc;
	}while(false);
	
	if(NULL != htup)
	{
		tupledata.t_data = htup;
		if(rrctl.toastrel)
		{
			/*if it is insert into a toast table,store it into list*/
			Oid		chunk_id;
			int		chunk_seq;
			char	*chunk_data;
			bool	isnull = false;

			chunk_id = DatumGetObjectId(fastgetattr(&tupledata, 1, tupdesc, &isnull));
			chunk_seq =DatumGetInt32(fastgetattr(&tupledata, 2, tupdesc, &isnull));
			chunk_data = DatumGetPointer(fastgetattr(&tupledata, 3, tupdesc, &isnull));
			toastTupleAddToList(makeToastTuple(VARSIZE(chunk_data) - VARHDRSZ, VARDATA(chunk_data), chunk_id, chunk_seq, rrctl.reloid));
			return;
		}
		else
		{
			rrctl.sqlkind = PG_LOGMINER_SQLKIND_DELETE;
			mentalTup(&tupledata, tupdesc, &rrctl.tupinfo, false);
			*tuple_info = rrctl.tupinfo.sqlStr;
			rrctl.sqlkind = 0;
		}
	}
}


/*
*	Get useful data from record,and return update data by tuple_info and tuple_info_old. 
*/
static void 
getTupleData_Update(XLogReaderState *record, char** tuple_info, char** tuple_info_old,Oid reloid)
{
	xl_heap_update *xlrec = NULL;
	xl_heap_header *xlhdr = NULL;
	uint32			newlen = 0;
	char			*tuplem;
	char			*tuplem_old;
	HeapTupleHeader htup = NULL;
	HeapTupleHeader htup_old = NULL;
	TupleDesc		tupdesc = NULL;
	HeapTupleData 	tupledata;
	
	Size			datalen = 0;
	Size			tuplen = 0;
	char 			*recdata = NULL;
	char	   		*recdata_end = NULL;
	char	   		*newp = NULL;
	RelFileNode 			target_node;
	ItemPointerData 		target_tid;
	ItemPointerData 		target_tid_old;
	BlockNumber 			newblk;
	BlockNumber 			oldblk;
	bool					oldimage = false;
	int						indexnew = 0, indexold = 0;
	ItemId					lp = NULL;
	char					page[BLCKSZ] = {0};
	
	if(!rrctl.tupinfo_init)
	{
		memset(&rrctl.tupinfo, 0, sizeof(XLogMinerSQL));
		rrctl.tupinfo_init = true;
	}
	else
		cleanSpace(&rrctl.tupinfo);
	if(!rrctl.tupinfo_old_init)
	{
		memset(&rrctl.tupinfo_old, 0, sizeof(XLogMinerSQL));
		rrctl.tupinfo_old_init = true;
	}
	else
		cleanSpace(&rrctl.tupinfo_old);
	memset(&tupledata, 0, sizeof(HeapTupleData));

	xlrec = (xl_heap_update *) XLogRecGetData(record);
	XLogRecGetBlockTag(record, 0, &target_node, NULL, &newblk);
	if (!XLogRecGetBlockTag(record, 1, NULL, NULL, &oldblk))
		oldblk = newblk;
	outVar((void*)&oldblk, 2);
	outVar((void*)&xlrec->new_offnum, 3);
	outVar((void*)&xlrec->old_offnum, 3);
	ItemPointerSet(&target_tid, newblk, xlrec->new_offnum);
	ItemPointerSet(&target_tid_old, oldblk, xlrec->old_offnum);
	rrctl.reloid = reloid;
//log_printf_str1("newoffnum=");log_printf_long(xlrec->new_offnum);
//log_printf_str1("oldoffnum=");log_printf_long(xlrec->old_offnum);
	if(rrctl.tupdesc)
	{
		freetupdesc(rrctl.tupdesc);
		rrctl.tupdesc = NULL;
	}
	rrctl.tupdesc = GetDescrByreloid(reloid);
	tupdesc = rrctl.tupdesc;

	do
	{

		if(XLogRecHasBlockImage(record, 0))
		{
//log_printf_str("getTupleData_Update 1");
			recdata = getTupleFromImage_update(record, page, &datalen, true);
			if(!recdata)
				elog(ERROR,"get data form image failed about reloid=%u,blckno=%u", target_node.relNode, newblk);
			newlen = datalen;
			
			tuplem = rrctl.tuplem;
			
			htup = (HeapTupleHeader)tuplem;
			memcpy((char *) htup , recdata, newlen);
		}
		else
		{
			uint16		prefixlen = 0, suffixlen = 0;
			Size		oldlen = 0;
			char		page_old[BLCKSZ] = {0};
			
			if(XLOG_HEAP_INIT_PAGE & XLogRecGetInfo(record))
			{
				pageInitInXlog(&target_node, MAIN_FORKNUM, newblk);
			}

			if((xlrec->flags & XLH_UPDATE_PREFIX_FROM_OLD) || (xlrec->flags & XLH_UPDATE_SUFFIX_FROM_OLD))
			{
				htup_old = (HeapTupleHeader)getTupleFromImage_update(record, (Page)page_old, &oldlen, false);
				if(!htup_old)
					continue;
			}
			recdata = XLogRecGetBlockData(record, 0, &datalen);
			recdata_end = recdata + datalen;

			if (xlrec->flags & XLH_UPDATE_PREFIX_FROM_OLD)
			{
				Assert(newblk == oldblk);
				memcpy(&prefixlen, recdata, sizeof(uint16));
				recdata += sizeof(uint16);
			}
			if (xlrec->flags & XLH_UPDATE_SUFFIX_FROM_OLD)
			{
				Assert(newblk == oldblk);
				memcpy(&suffixlen, recdata, sizeof(uint16));
				recdata += sizeof(uint16);
			}
			xlhdr = (xl_heap_header*)recdata;
			recdata += SizeOfHeapHeader;

			tuplen = recdata_end - recdata;
//log_printf_str1("datalen=");log_printf_long(datalen);
//log_printf_str1("prefixlen=");log_printf_long(prefixlen);
//log_printf_str1("suffixlen=");log_printf_long(suffixlen);
//log_printf_str1("tuplen=");log_printf_long(tuplen);

			Assert(tuplen <= MaxHeapTupleSize);

			tuplem = rrctl.tuplem;
			htup = (HeapTupleHeader)tuplem;
			memset(htup, 0, SizeofHeapTupleHeader);

			newp = (char *) htup + SizeofHeapTupleHeader;

			
			if (prefixlen > 0)
			{
				int 		len;

				/* copy bitmap [+ padding] [+ oid] from WAL record */
				len = xlhdr->t_hoff - SizeofHeapTupleHeader;
				memcpy(newp, recdata, len);
				recdata += len;
				newp += len;

				/* copy prefix from old tuple */
				if(htup_old)
					memcpy(newp, (char *) htup_old + htup_old->t_hoff, prefixlen);
				else
					memset(newp, 0, prefixlen);
				newp += prefixlen;

				/* copy new tuple data from WAL record */
				len = tuplen - (xlhdr->t_hoff - SizeofHeapTupleHeader);
				memcpy(newp, recdata, len);
				recdata += len;
				newp += len;
			}
			else
			{
				/*
				 * copy bitmap [+ padding] [+ oid] + data from record, all in one
				 * go
				 */
				memcpy(newp, recdata, tuplen);
				recdata += tuplen;
				newp += tuplen;
			}
			Assert(recdata == recdata_end);

			/* copy suffix from old tuple */
			if (suffixlen > 0)
			{
				if(htup_old)
					memcpy(newp, (char *) htup_old + oldlen - suffixlen, suffixlen);
				else
					memset(newp, 0, suffixlen);
			}

			htup->t_infomask2 = xlhdr->t_infomask2;
			htup->t_infomask = xlhdr->t_infomask;
			htup->t_hoff = xlhdr->t_hoff;
			HeapTupleHeaderSetXmin(htup, XLogRecGetXid(record));
			HeapTupleHeaderSetCmin(htup, FirstCommandId);
			HeapTupleHeaderSetXmax(htup, xlrec->new_xmax);
			/* Make sure there is no forward chain link in t_ctid */
			htup->t_ctid = target_tid;
			newlen = SizeofHeapTupleHeader + tuplen + prefixlen + suffixlen;

			if(debug_mode)
			{
				memset(globleStrInfo, 0, PG_DEBUG_STRINFO_SIZE);
				sprintf(globleStrInfo, "[getTupleData_Update]PageAddItem:offsetnum %d,relnode %u, blckno %d",xlrec->new_offnum, target_node.relNode, newblk);
				outTempleResult(globleStrInfo);
			}
			if(!getImageFromStore(&target_node, MAIN_FORKNUM, newblk, (char*)page, &indexnew))
			{
				break;
			}
			if (InvalidOffsetNumber == PageAddItem(page, (Item) htup, newlen, xlrec->new_offnum, true, true))
				elog(ERROR, "failed to add tuple:offsetnum %d,relnode %u, blckno %d", xlrec->new_offnum, target_node.relNode, newblk);

			flushPage(indexnew, page);
			/*If we does not find old tuple, then should not exe mentalTup*/
			if((prefixlen > 0 || suffixlen > 0) && !htup_old)
			{
				htup = NULL;
			}

		}
	}while(false);
	
	htup_old = NULL;
	do
	{
		if(oldblk == newblk)
		{
			oldimage = XLogRecHasBlockImage(record, 0);
		}
		else
		{
			oldimage = XLogRecHasBlockImage(record, 1);
		}
		if(!getImageFromStore(&target_node, MAIN_FORKNUM, oldblk, (char*)page, &indexold))
		{
			break;
		}

		lp = PageGetItemId(page, xlrec->old_offnum);
		datalen = lp->lp_len;
		
		htup_old = (HeapTupleHeader) PageGetItem(page, lp);
		if (!oldimage)
		{
			htup_old->t_infomask &= ~(HEAP_XMAX_BITS | HEAP_MOVED);
			htup_old->t_infomask2 &= ~HEAP_KEYS_UPDATED;
			fix_infomask_from_infobits(xlrec->old_infobits_set, &htup_old->t_infomask,
									   &htup_old->t_infomask2);
			HeapTupleHeaderSetXmax(htup_old, xlrec->old_xmax);
			HeapTupleHeaderSetCmax(htup_old, FirstCommandId, false);
			/* Set forward chain link in t_ctid */
			htup_old->t_ctid = target_tid;

			/* Mark the page as a candidate for pruning */
			PageSetPrunable(page, XLogRecGetXid(record));

			if (xlrec->flags & XLH_UPDATE_OLD_ALL_VISIBLE_CLEARED)
				PageClearAllVisible(page);
			
			flushPage(indexold, page);
		}

		if(datalen + SizeOfHeapUpdate > MaxHeapTupleSize)
		{
			rrctl.tuplem_bigold = getTuplemSpace(datalen + SizeOfHeapUpdate + SizeofHeapTupleHeader);
			tuplem_old = rrctl.tuplem_bigold;
		}
		else
		{
			tuplem_old = rrctl.tuplem_old;
		}
		memcpy((char *) tuplem_old, htup_old, datalen);
		htup_old = (HeapTupleHeader)tuplem_old;
	}while(false);
	if(rrctl.toastrel)
	{
		return;
	}
	if(NULL != htup_old)
	{
		rrctl.sqlkind = PG_LOGMINER_SQLKIND_UPDATE;

		tupledata.t_data = htup_old;
		mentalTup(&tupledata, tupdesc, &rrctl.tupinfo_old, true);
		*tuple_info_old = rrctl.tupinfo_old.sqlStr;
		
		rrctl.sqlkind = 0;
	}
	if(NULL != htup)
	{
		rrctl.sqlkind = PG_LOGMINER_SQLKIND_UPDATE;
		tupledata.t_data = htup;
		mentalTup(&tupledata, tupdesc, &rrctl.tupinfo, false);
		*tuple_info = rrctl.tupinfo.sqlStr;
		
		rrctl.sqlkind = 0;
	}
}

/*Func control that if we reached the valid record*/
void
processContrl(char* relname, int	contrlkind)
{

	if(PG_LOGMINER_XLOG_NOMAL == rrctl.system_init_record)
		return;
	if(PG_LOGMINER_CONTRLKIND_FIND == contrlkind &&
		(0 == strcmp(relname,PG_LOGMINER_DATABASE_HIGHGO) || 0 == strcmp(relname,PG_LOGMINER_DATABASE_POSTGRES)))
	{
		/*We have got "highgo" or "postgres" db create sql.*/
		rrctl.sysstoplocation = PG_LOGMINER_FLAG_FINDHIGHGO;
	}
	else if(PG_LOGMINER_CONTRLKIND_XACT == contrlkind)
	{
		rrctl.sysstoplocation++;
		if(PG_LOGMINER_FLAG_INITOVER == rrctl.sysstoplocation)
		{
			/*We got xact commit just after db create mentioned above*/
			rrctl.system_init_record = PG_LOGMINER_XLOG_NOMAL;
		}
	}
}


static bool
getTupleInfoByRecord(XLogReaderState *record, uint8 info, NameData* relname,char** schname, char** tuple_info, char** tuple_info_old)
{

	RelFileNode 		*node = NULL;
	Oid					reloid = 0;
	Oid					dboid = 0;
	uint8				rmid = XLogRecGetRmid(record);
	BlockNumber 		blknum = 0;

	dboid = getDataDicOid();
	cleanMentalvalues();

	XLogRecGetBlockTag(record, 0, &srctl.rfnode, NULL, &blknum);
	node = &srctl.rfnode;
	outVar((void*)node, 1);
	outVar((void*)&blknum, 2);
	if(dboid != node->dbNode)
		return false;

	/*Get which relation it was belonged to*/
	reloid = getRelationOidByRelfileid(node->relNode);
	if(0 == reloid)
	{
		if(PG_LOGMINER_DICTIONARY_LOADTYPE_SELF == getDatadictionaryLoadType())
			reloid = RelationMapFilenodeToOid(node->relNode, node->spcNode == GLOBALTABLESPACE_OID);
		else
			reloid = getRelidbyRelnodeViaMap(node->relNode, node->spcNode == GLOBALTABLESPACE_OID);
	}
	if(0 == reloid)
	{
		char	strtemp[1024] = {0};
		rrctl.reloid = 0;
		rrctl.nomalrel = false;
		rrctl.hasunanalysetuple = true;
		sprintf(strtemp, "UNANALYSE:Relfilenode %d can not be handled,maybe the datadictionary does not match.", node->relNode);
		if(tempresultout)
			outTempleResult(strtemp);
		return false;
	}
	
	if(-1 == getRelationNameByOid(reloid, relname))
		return false;
	*schname = getnsNameByReloid(reloid);
	rrctl.sysrel = tableIfSysclass(relname->data,reloid);
	rrctl.nomalrel = (!rrctl.sysrel) && (!tableIftoastrel(reloid));
	rrctl.imprel = tableifImpSysclass(relname->data,reloid);
	rrctl.toastrel = tableIftoastrel(reloid);
	rrctl.recordxid = XLogRecGetXid(record);
	if(rrctl.nomalrel)
	{
		rrctl.tbsoid = node->spcNode;
	}
	/*We does not care unuseful catalog relation
		We does not care update toast relation*/
	/*if((rrctl.sysrel && !rrctl.imprel)
		|| (rrctl.toastrel && XLOG_HEAP_UPDATE == info))
		return false;*/	
	if(XLOG_HEAP_INSERT == info && RM_HEAP_ID == rmid)
	{
		getTupleData_Insert(record, tuple_info, reloid);
	}
	else if((XLOG_HEAP_HOT_UPDATE == info || XLOG_HEAP_UPDATE == info) && RM_HEAP_ID == rmid)
	{
		getTupleData_Update(record, tuple_info, tuple_info_old, reloid);
	}
	else if(XLOG_HEAP_DELETE == info && RM_HEAP_ID == rmid)
	{
		getTupleData_Delete(record, tuple_info, reloid);
	}
	return true;
}

static void 
minerHeapInsert(XLogReaderState *record, XLogMinerSQL *sql_simple,uint8 info)
{
	NameData			relname;
	char*				schname;
	bool				sqlFind = false;
	char				*tupleInfo = NULL;
	bool				nomalrel = false;
	bool				sysrel = false;

	memset(&relname, 0, sizeof(NameData));

	sqlFind = getTupleInfoByRecord(record, info, &relname, &schname, &tupleInfo ,NULL);
	if(!sqlFind)
		return;
	nomalrel = rrctl.nomalrel;
	sysrel = rrctl.sysrel;
	/*Assemble "table name","tuple data" and "describe word"*/

	getInsertSQL(sql_simple,tupleInfo,&relname, schname, sysrel);

	if(nomalrel && elemNameFind(relname.data) && 0 == rrctl.prostatu)
	{
		/*Get undo sql*/
		getDeleteSQL(&srctl.sql_undo,tupleInfo,&relname, schname, sysrel, true);
		/*
		*Format delete sql
		*"where values(1,2,3);"-->"where i = 1 AND j = 2 AND k = 3;"
		*/
		reAssembleDeleteSql(&srctl.sql_undo, true);
	}
}

static void
minerHeapDelete(XLogReaderState *record, XLogMinerSQL *sql_simple,uint8 info)
{
	bool				sqlFind = false;
	NameData			relname;
	char*				schname;
	char 				*tupleInfo = NULL;
	bool				nomalrel = false;
	bool				sysrel = false;

	memset(&relname, 0, sizeof(NameData));
	sqlFind = getTupleInfoByRecord(record, info, &relname, &schname, &tupleInfo, NULL);
	if(!sqlFind)
		return;
	nomalrel = rrctl.nomalrel;
	sysrel = rrctl.sysrel;
	/*Assemble "table name","tuple data" and "describe word"*/
	getDeleteSQL(sql_simple,tupleInfo,&relname,schname,sysrel, false);
	if(nomalrel && elemNameFind(relname.data) && 0 == rrctl.prostatu)
	{
		/*Get undo sql*/
		getInsertSQL(&srctl.sql_undo,tupleInfo,&relname,schname,sysrel);
	}	
}

static void
minerHeapUpdate(XLogReaderState *record, XLogMinerSQL *sql_simple, uint8 info)
{
	NameData			relname;
	char*				schname;
	bool				sqlFind = false;
	char 				*tupleInfo = NULL;
	char				*tupleInfo_old = NULL;
	bool				nomalrel = false;
	bool				sysrel = false;
	
	memset(&relname, 0, sizeof(NameData));
	sqlFind = getTupleInfoByRecord(record, info, &relname, &schname, &tupleInfo, &tupleInfo_old);
	if(!sqlFind)
		return;
	nomalrel = rrctl.nomalrel;
	sysrel = rrctl.sysrel;
	/*Assemble "table name","tuple data" and "describe word"*/
	getUpdateSQL(sql_simple, tupleInfo, tupleInfo_old, &relname, schname, sysrel);
	if(nomalrel && elemNameFind(relname.data)  && 0 == rrctl.prostatu)
	{
		/*Get undo sql*/
		getUpdateSQL(&srctl.sql_undo, tupleInfo_old, tupleInfo, &relname, schname, sysrel);
		/*
		*Format update sql
		*"update t1 set values(1,2,4) where values(1,2,3);"
		*-->"update t1 set j = 4 where i = 1 AND j = 2 AND k = 3"
		*/
		reAssembleUpdateSql(&srctl.sql_undo,true);
	}
}

static void
minerHeap2Clean(XLogReaderState *record, uint8 info)
{
	RelFileNode 			rnode;
	HeapTupleData 			tupledata;
	BlockNumber 			blkno = 0;
	xl_heap_clean 			*xlrec = NULL;
	int						index = 0;
	char					page[BLCKSZ] = {0};
	
	memset(&rnode, 0, sizeof(RelFileNode));
	memset(&tupledata, 0, sizeof(HeapTupleData));

	xlrec = (xl_heap_clean *) XLogRecGetData(record);
	
	XLogRecGetBlockTag(record, 0, &rnode, NULL, &blkno);
	if(getDataDicOid() != rnode.dbNode)
		return;

	if(!XLogRecHasBlockImage(record, 0))
	{
		OffsetNumber 			*end = NULL;
		OffsetNumber 			*redirected = NULL;
		OffsetNumber 			*nowdead = NULL;
		OffsetNumber			*nowunused = NULL;
		int						nredirected = 0;
		int						ndead = 0;
		int						nunused = 0;
		Size					datalen = 0;
		
		if(!getImageFromStore(&rnode, MAIN_FORKNUM, blkno, (char*)page, &index))
		{
			return;
		}
		redirected = (OffsetNumber *) XLogRecGetBlockData(record, 0, &datalen);
		nredirected = xlrec->nredirected;
		ndead = xlrec->ndead;
		end = (OffsetNumber *) ((char *) redirected + datalen);
		nowdead = redirected + (nredirected * 2);
		nowunused = nowdead + ndead;
		nunused = (end - nowunused);
		Assert(nunused >= 0);

		heap_page_prune_execute_logminer(page,
								redirected, nredirected,
								nowdead, ndead,
								nowunused, nunused);

		flushPage(index, page);
	}
	
}


static void
minerHeap2MutiInsert(XLogReaderState *record, XLogMinerSQL *sql_simple, uint8 info)	
{
	RelFileNode 			rnode;
	HeapTupleData 			tupledata;
	NameData				relname;
	char					*schname = NULL;
	char					*tuple_info = NULL;
	xl_heap_multi_insert 	*xlrec = NULL;
	xl_multi_insert_tuple 	*xlhdr = NULL;
	char	   				*data = NULL;
	char	   				*tldata = NULL;
	Size					tuplelen = 0;
	BlockNumber 			blkno = 0;
	ItemPointerData 		target_tid;
	Oid						reloid = 0;
	HeapTupleHeader 		htup = NULL;
	Size					datalen = 0, newlen = 0;
	int						index = 0;
	char					page[BLCKSZ] = {0};
	bool					changepagefind = false;
	OffsetNumber			offsetnum = InvalidOffsetNumber;
	

	memset(&rnode, 0, sizeof(RelFileNode));
	memset(&tupledata, 0, sizeof(HeapTupleData));
	memset(&relname, 0, sizeof(NameData));

	XLogRecGetBlockTag(record, 0, &rnode, NULL, &blkno);
	outVar((void*)&rnode, 1);
	outVar((void*)&blkno, 2);
	if(getDataDicOid() != rnode.dbNode)
		return;


	if(!rrctl.tupinfo_init)
	{
		memset(&rrctl.tupinfo, 0, sizeof(XLogMinerSQL));
		rrctl.tupinfo_init = true;
	}
	else
		cleanSpace(&rrctl.tupinfo);
	
	xlrec = (xl_heap_multi_insert *) XLogRecGetData(record);
	
	if(XLogRecHasBlockImage(record, 0))
	{
		tldata = getTupleFromImage_mutiinsert(record, (Page)page, &datalen, xlrec->offsets[srctl.sqlindex]);
		data = tldata;
		if(!data)
		{
			elog(ERROR,"can not find imagestore about reloid=%u,blckno=%u", rnode.relNode, blkno);
		}
		htup = (HeapTupleHeader)rrctl.tuplem;
		memcpy((char *) htup , data, datalen);
	}
	else
	{
		if((XLOG_HEAP_INIT_PAGE & XLogRecGetInfo(record)) && 0 == srctl.sqlindex)
		{
			pageInitInXlog(&rnode, MAIN_FORKNUM, blkno);
			srctl.isinit = true;
		}
		if(getImageFromStore(&rnode, MAIN_FORKNUM, blkno, (char*)page, &index))
		{
			changepagefind = true;
		}
		else
		{
			/*Does not find the page should not add tuple*/
			changepagefind = false;
		}
		
		if(!srctl.mutinsert)
		{
			tldata = XLogRecGetBlockData(record, 0, &tuplelen);
			data = tldata;
		}
		else
			data = srctl.multdata;

		if(srctl.isinit)
		{
			offsetnum =  srctl.sqlindex + FirstOffsetNumber;
		}
		else
		{
			offsetnum = xlrec->offsets[srctl.sqlindex];
		}
		ItemPointerSetBlockNumber(&target_tid, blkno);
		ItemPointerSetOffsetNumber(&target_tid, offsetnum);
		xlhdr = (xl_multi_insert_tuple *) SHORTALIGN(data);
		data = ((char *) xlhdr) + SizeOfMultiInsertTuple;
		datalen = xlhdr->datalen;
		htup = (HeapTupleHeader)rrctl.tuplem;
		memcpy((char*)htup + SizeofHeapTupleHeader, (char*)data, datalen);
		data += datalen;
		srctl.multdata = data;

		htup->t_infomask = xlhdr->t_infomask;
		htup->t_infomask2 = xlhdr->t_infomask2;
		htup->t_hoff = xlhdr->t_hoff;

		HeapTupleHeaderSetXmin(htup, XLogRecGetXid(record));
		HeapTupleHeaderSetCmin(htup, FirstCommandId);
		htup->t_ctid = target_tid;
		if(changepagefind)
		{
			if(debug_mode)
			{
				memset(globleStrInfo, 0, PG_DEBUG_STRINFO_SIZE);
				sprintf(globleStrInfo, "[minerHeap2MutiInsert]PageAddItem:offsetnum %d,relnode %u, blckno %d,index %d", offsetnum, rnode.relNode, blkno, index);
				outTempleResult(globleStrInfo);
			}
			newlen = datalen + SizeofHeapTupleHeader;
			if (InvalidOffsetNumber == PageAddItem(page, (Item) htup, newlen, offsetnum, true, true))
				elog(ERROR, "failed to add tuple:offsetnum %d,relnode %u, blckno %d", offsetnum, rnode.relNode, blkno);
			flushPage(index, page);
		}
	}

	
	/*Get which relation it was belonged to*/
	reloid = getRelationOidByRelfileid(rnode.relNode);
	if(0 == reloid)
	{
		if(PG_LOGMINER_DICTIONARY_LOADTYPE_SELF == getDatadictionaryLoadType())
			reloid = RelationMapFilenodeToOid(rnode.relNode, rnode.spcNode == GLOBALTABLESPACE_OID);
		else
			reloid = getRelidbyRelnodeViaMap(rnode.relNode, rnode.spcNode == GLOBALTABLESPACE_OID);
	}
	if(0 == reloid)
	{
		char	strtemp[1024] = {0};
		rrctl.reloid = 0;
		rrctl.nomalrel = false;
		rrctl.hasunanalysetuple = true;
		sprintf(strtemp, "UNANALYSE:Relfilenode %d can not be handled,maybe the datadictionary does not match.", rnode.relNode);
		if(tempresultout)
			outTempleResult(strtemp);
		
		return;
	}
	rrctl.reloid = reloid;
	if( -1 == getRelationNameByOid(reloid, &relname))
		return;
	schname = getnsNameByReloid(reloid);
	rrctl.sysrel = tableIfSysclass(relname.data,reloid);
	rrctl.nomalrel = (!rrctl.sysrel) && (!tableIftoastrel(reloid));
	rrctl.imprel = tableifImpSysclass(relname.data,reloid);
	rrctl.tbsoid = rnode.spcNode;
	rrctl.recordxid = XLogRecGetXid(record);
	if(rrctl.tupdesc)
	{
		freetupdesc(rrctl.tupdesc);
		rrctl.tupdesc = NULL;
	}
	rrctl.tupdesc = GetDescrByreloid(reloid);
	
	if(NULL != htup)
	{
		rrctl.sqlkind = PG_LOGMINER_SQLKIND_INSERT;
		tupledata.t_data = htup;
		mentalTup(&tupledata, rrctl.tupdesc, &rrctl.tupinfo, false);
		if(!rrctl.tupinfo.sqlStr)
			rrctl.prostatu = LOGMINER_PROSTATUE_INSERT_MISSING_TUPLEINFO;
		rrctl.sqlkind = 0;
		tuple_info = rrctl.tupinfo.sqlStr;
		getInsertSQL(sql_simple,tuple_info,&relname, schname, rrctl.sysrel);
		if(rrctl.nomalrel && elemNameFind(relname.data)  && 0 == rrctl.prostatu)
		{
			/*Get undo sql*/
			getDeleteSQL(&srctl.sql_undo,tuple_info,&relname, schname, rrctl.sysrel, true);
			/*
			*Format delete sql
			*"where values(1,2,3);"-->"where i = 1 AND j = 2 AND k = 3;"
			*/
			reAssembleDeleteSql(&srctl.sql_undo, true);
			srctl.sqlindex++;
			if(srctl.sqlindex >= xlrec->ntuples)
			{
				srctl.sqlindex = 0;
				srctl.multdata = NULL;
				srctl.isinit = false;
				srctl.mutinsert = false;
			}
			else
				srctl.mutinsert = true;
		}
		
	}
}

/*find next xlog record and store into rrctl.xlogreader_state*/
static bool 
getNextRecord()
{
	XLogRecord *record_t;	
	record_t = XLogReadRecord_logminer(rrctl.xlogreader_state, rrctl.first_record, &rrctl.errormsg);
	recordStoreImage(rrctl.xlogreader_state);
	rrctl.first_record = InvalidXLogRecPtr;
	rrctl.recyclecount++;
	if (!record_t)
	{
		return false;
	}
	return true;
}


static int
getsqlkind(char *sqlheader)
{
	int loop,result = -1;
	for(loop = 0 ;loop < PG_LOGMINER_SQLKIND_MAXNUM; loop++)
	{
		if(0 == strcmp(sqlheader,sqlkind[loop].sqlhead))
			result = sqlkind[loop].sqlid;
	}
	return result;
}


/*Parser all kinds of insert sql, and make decide what sql it will form*/
static bool
parserInsertSql(XLogMinerSQL *sql_ori, XLogMinerSQL *sql_opt)
{
	char tarTable[NAMEDATALEN] = {0};
	
	

	getPhrases(sql_ori->sqlStr, LOGMINER_INSERT_TABLE_NAME, tarTable, 0);

	/*It just insert to a user's table*/
	if(rrctl.nomalrel && (elemNameFind(tarTable) || 0 == strcmp("NULL",tarTable)))
	{
		appendtoSQL(sql_opt,sql_ori->sqlStr,PG_LOGMINER_SQLPARA_SIMSTEP);
		/*Here reached,it is not in toat,so try to free tthead*/
		return true;
	}
	freeToastTupleHeadByoid(rrctl.reloid);
	return false;
}

/*Parser all kinds of delete sql, and make decide what sql it will form*/
static bool
parserDeleteSql(XLogMinerSQL *sql_ori, XLogMinerSQL *sql_opt)
{

	char tarTable[NAMEDATALEN] = {0};

	getPhrases(sql_ori->sqlStr, LOGMINER_DELETE_TABLE_NAME, tarTable, 0);

	if(rrctl.nomalrel && (elemNameFind(tarTable) || 0 == strcmp("NULL",tarTable)))
	{
		if(0 == rrctl.prostatu)
			reAssembleDeleteSql(sql_ori, false);
		appendtoSQL(sql_opt,sql_ori->sqlStr,PG_LOGMINER_SQLPARA_SIMSTEP);
	}
	freeToastTupleHeadByoid(rrctl.reloid);
	return true;
}

/*Parser all kinds of update sql, and make decide what sql it will form*/
static bool
parserUpdateSql(XLogMinerSQL *sql_ori, XLogMinerSQL *sql_opt)
{
	char tarTable[NAMEDATALEN] = {0};
	
	

	getPhrases(sql_ori->sqlStr, LOGMINER_ATTRIBUTE_LOCATION_UPDATE_RELNAME, tarTable, 0);
	if(rrctl.nomalrel && (elemNameFind(tarTable) || 0 == strcmp("NULL",tarTable)))
	{
		if(0 == rrctl.prostatu)
			reAssembleUpdateSql(sql_ori, false);
		if(sql_ori->sqlStr)
			appendtoSQL(sql_opt,sql_ori->sqlStr,PG_LOGMINER_SQLPARA_SIMSTEP);
	}
	freeToastTupleHeadByoid(rrctl.reloid);
	return true;
}

static void
parserXactSql(XLogMinerSQL *sql_ori, XLogMinerSQL *sql_opt)
{
	appendtoSQL(sql_opt,sql_ori->sqlStr,PG_LOGMINER_SQLPARA_SIMSTEP);
}


static void
XLogMinerRecord_heap(XLogReaderState *record, XLogMinerSQL *sql_simple)
{
	uint8				info;
	
	info = XLogRecGetInfo(record) & ~XLR_INFO_MASK;
	info &= XLOG_HEAP_OPMASK;
	
	if (XLOG_HEAP_INSERT == info)
	{
		minerHeapInsert(record, sql_simple, info);
	}
	else if(XLOG_HEAP_DELETE == info)
	{
		minerHeapDelete(record, sql_simple, info);
	}
	else if (XLOG_HEAP_UPDATE == info)
	{
		minerHeapUpdate(record, sql_simple, info);
	}
	else if (XLOG_HEAP_HOT_UPDATE == info)
	{
		minerHeapUpdate(record, sql_simple, info);
	}
}

static void
XLogMinerRecord_heap2(XLogReaderState *record, XLogMinerSQL *sql_simple)
{
	uint8		info = XLogRecGetInfo(record) & XLOG_HEAP_OPMASK;

	if(XLOG_HEAP2_MULTI_INSERT == info)
	{
		minerHeap2MutiInsert(record, sql_simple, info);
	}
	else if(XLOG_HEAP2_CLEAN == info)
	{
		minerHeap2Clean(record, info);
	}

}

static void
XLogMinerRecord_dbase(XLogReaderState *record, XLogMinerSQL *sql_simple)
{
	uint8				info;

	info = XLogRecGetInfo(record) & ~XLR_INFO_MASK;
	if(XLOG_DBASE_CREATE == info)
	{
		minerDbCreate(record, sql_simple, info);
	}
}
/*
static void
XLogMinerRecord_xlog(XLogReaderState *record, XLogMinerSQL *sql_simple)
{
	uint8				info;

	info = XLogRecGetInfo(record) & ~XLR_INFO_MASK;
	if(XLOG_CHECKPOINT_ONLINE == info || XLOG_CHECKPOINT_SHUTDOWN == info)
	{
		if(rrctl.imagelist)
		{
			cleanStorefile();
			list_free(rrctl.imagelist);
			rrctl.imagelist = NIL;
		}
		rrctl.getcheckpoint = true;
	}
}
*/

static void
XLogMinerRecord_xact(XLogReaderState *record, XLogMinerSQL *sql_simple, TimestampTz *xacttime)
{
	uint8						info = XLogRecGetInfo(record) & XLOG_XACT_OPMASK;
	xl_xact_parsed_commit		parsed_commit;
	char 						timebuf[MAXDATELEN + 1] = {0};
	TransactionId 				xid = 0;
	bool						commitxact = false;

	if(info == XLOG_XACT_COMMIT)
	{
		xl_xact_commit *xlrec = NULL;
		memset(&parsed_commit, 0, sizeof(xl_xact_parsed_commit));
		xlrec = (xl_xact_commit *) XLogRecGetData(record);
		ParseCommitRecord(XLogRecGetInfo(record), xlrec, &parsed_commit);
		if (!TransactionIdIsValid(parsed_commit.twophase_xid))
			xid = XLogRecGetXid(record);
		else
			xid = parsed_commit.twophase_xid;

		memcpy(timebuf,timestamptz_to_str(xlrec->xact_time),MAXDATELEN + 1);

		*xacttime = xlrec->xact_time;
		commitxact = true;
	}
	else if(info == XLOG_XACT_ABORT)
	{
		xl_xact_abort *xlrec = NULL;
		xl_xact_parsed_abort parsed_abort;

		memset(&parsed_abort, 0, sizeof(xl_xact_parsed_abort));
		xlrec = (xl_xact_abort *) XLogRecGetData(record);
		ParseAbortRecord(XLogRecGetInfo(record), xlrec, &parsed_abort);
		if (!TransactionIdIsValid(parsed_abort.twophase_xid))
			xid = XLogRecGetXid(record);
		else
			xid = parsed_abort.twophase_xid;

		memcpy(timebuf,timestamptz_to_str(xlrec->xact_time),MAXDATELEN + 1);
		*xacttime = xlrec->xact_time;
	}
	
	processContrl(NULL,PG_LOGMINER_CONTRLKIND_XACT);
	if(curXactCheck(*xacttime, xid, commitxact, &parsed_commit))
	{
		xactCommitSQL(timebuf,sql_simple,info);
	}
}

static bool
XLogMinerRecord(XLogReaderState *record, XLogMinerSQL *sql_simple,TimestampTz *xacttime)
{
	uint8				info;
	bool				getxact = false;
	uint8				rmid = 0;

	rmid = XLogRecGetRmid(record);

	info = XLogRecGetInfo(record) & ~XLR_INFO_MASK;
	info &= XLOG_HEAP_OPMASK;

	/*if has not get valid record(input para) and it is not a xact record,parser nothing*/
	if(PG_LOGMINER_XLOG_NOMAL == rrctl.system_init_record && !rrctl.logprivate.staptr_reached && RM_XACT_ID != rmid)
		return false;

	if((RM_DBASE_ID == rmid || PG_LOGMINER_XLOG_DBINIT == rrctl.system_init_record)
		&& (0 == rrctl.sysstoplocation || PG_LOGMINER_FLAG_INITOVER == rrctl.sysstoplocation))
	{
		if(RM_DBASE_ID != rmid)
			return false;
		/*If we find 'postgresql' or 'highgo' db create cade,than it may will reach valid data after a xact commit*/
		XLogMinerRecord_dbase(record, sql_simple);
	}
	else if(RM_XACT_ID == rmid || PG_LOGMINER_FLAG_INITOVER != rrctl.sysstoplocation)
	{
		if(RM_XACT_ID != rmid)
			return false;
		/*
		if first time reach here,then we just find 'postgresql' or 'highgo' db create code,than it may be will reach valid data
		*/
		XLogMinerRecord_xact(record, sql_simple, xacttime);
		getxact = true;
	}
	else if(RM_HEAP_ID == rmid)
	{
		XLogMinerRecord_heap(record, sql_simple);
	}
	else if(RM_HEAP2_ID == rmid)
	{
		XLogMinerRecord_heap2(record, sql_simple);
	}
	else if(RM_XLOG_ID == rmid)
	{
		/*code here for clean image space on disk
		* but it has something wrong because of delay of checkpoint
		* now we doen not clean it.
		*/
		//XLogMinerRecord_xlog(record, sql_simple);
		;
	}
	
	return getxact;
}

bool
sqlParser(XLogReaderState *record, TimestampTz 	*xacttime)
{
	char command_sql[NAMEDATALEN] = {0};
	int				sskind = 0;
	bool			getxact = false;
	bool			getsql = false;
	Oid				server_dboid = 0;
	

	cleanSpace(&sql);
	cleanSpace(&sql_reass);
	/*parsert data that stored in a record to a simple sql */
	getxact = XLogMinerRecord(record, &sql, xacttime);
	/*avoid to parse the initdb  record*/
	if(rrctl.system_init_record != PG_LOGMINER_XLOG_NOMAL)
		return false;
	if(tempresultout && sql.sqlStr)
		outTempleResult(sql.sqlStr);

	getPhrases(sql.sqlStr, LOGMINER_SQL_COMMAND, command_sql, 0);
	sskind = getsqlkind(command_sql);

	if(true)
	{
		/*get a sql nomally*/
		/*Deal with every simple sql*/
		switch(sskind)
		{
			case PG_LOGMINER_SQLKIND_INSERT:
				getsql = parserInsertSql(&sql, &sql_reass);
				break;
			case PG_LOGMINER_SQLKIND_UPDATE:
				getsql = parserUpdateSql(&sql, &sql_reass);
				break;
			case PG_LOGMINER_SQLKIND_DELETE:
				getsql = parserDeleteSql(&sql, &sql_reass);
				break;
			case PG_LOGMINER_SQLKIND_XACT:
				parserXactSql(&sql, &sql_reass);
				break;
			default:
				break;
		}
	}
	rrctl.prostatu = 0;

	if(!isEmptStr(sql_reass.sqlStr) && !getxact)
	{
		/*Now, we get a SQL, it need to be store tempory.*/
		char	*record_schema = NULL;
		char	*record_database = NULL;
		char	*record_user = NULL;
		char	*record_tablespace = NULL;
		Oid		useroid = 0, schemaoid = 0;
		
		
		if(getsql)
		{
			/*It is a simple sql,DML*/
			server_dboid = getDataDicOid();
			record_database = getdbNameByoid(server_dboid, false);
			record_tablespace = gettbsNameByoid(rrctl.tbsoid);
			if(0 != rrctl.reloid)
			{
				useroid = gettuserOidByReloid(rrctl.reloid);
				if(0 != useroid)
					record_user = getuserNameByUseroid(useroid);

				schemaoid = getnsoidByReloid(rrctl.reloid);
				if(0 != schemaoid)
					record_schema = getnsNameByOid(schemaoid);
			}
			padingminerXlogconts(record_schema, 0, Anum_xlogminer_contents_record_schema, schemaoid);
			padingminerXlogconts(record_user, 0, Anum_xlogminer_contents_record_user, useroid);
			padingminerXlogconts(record_database, 0, Anum_xlogminer_contents_record_database, server_dboid);
			padingminerXlogconts(record_tablespace, 0, Anum_xlogminer_contents_record_tablespace, rrctl.tbsoid);
			padingminerXlogconts(srctl.sql_undo.sqlStr, 0, Anum_xlogminer_contents_op_undo, -1);
		}
		else
		{
			/*It is a assemble sql,DDL*/
			server_dboid = getDataDicOid();
			record_database = getdbNameByoid(server_dboid,false);
			padingminerXlogconts(record_database, 0, Anum_xlogminer_contents_record_database, server_dboid);
		}
		getPhrases(sql_reass.sqlStr, LOGMINER_SQL_COMMAND, command_sql, 0);
		padingminerXlogconts(sql_reass.sqlStr, 0, Anum_xlogminer_contents_op_text, -1);
		padingminerXlogconts(command_sql, 0, Anum_xlogminer_contents_op_type, -1);
		padingminerXlogconts(NULL,rrctl.recordxid, Anum_xlogminer_contents_xid, -1);
		
		srctl.xcfcurnum++;
		padNullToXC();
		cleanAnalyseInfo();
	}
	cleanTuplemSpace(rrctl.tuplem);
	cleanTuplemSpace(rrctl.tuplem_old);
	if(rrctl.tuplem_bigold)
		pfree(rrctl.tuplem_bigold);
	rrctl.tuplem_bigold = NULL;
	rrctl.nomalrel = false;
	rrctl.imprel = false;
	rrctl.sysrel = false;
	rrctl.toastrel = false;
	return getxact;
}

Datum
xlogminer_build_dictionary(PG_FUNCTION_ARGS)
{
	text	*dictionary = NULL;
	cleanSystableDictionary();
	checkLogminerUser();
	if(!PG_GETARG_DATUM(0))
		ereport(ERROR,(errmsg("Please enter a file path or directory.")));
	dictionary = PG_GETARG_TEXT_P(0);
	outputSysTableDictionary(text_to_cstring(dictionary), ImportantSysClass,false);
	PG_RETURN_TEXT_P(cstring_to_text("Dictionary build success!"));
}

Datum
xlogminer_load_dictionary(PG_FUNCTION_ARGS)
{
	text	*dictionary = NULL;
	cleanSystableDictionary();
	checkLogminerUser();
	if(!PG_GETARG_DATUM(0))
		ereport(ERROR,(errmsg("Please enter a file path or directory.")));
	dictionary = PG_GETARG_TEXT_P(0);
	if(DataDictionaryCache)
		ereport(ERROR,(errmsg("Dictionary has already been loaded.")));
	loadSystableDictionary(text_to_cstring(dictionary), ImportantSysClass, false);
	writeDicStorePath(text_to_cstring(dictionary));
	cleanSystableDictionary();
	PG_RETURN_TEXT_P(cstring_to_text("Dictionary load success!"));
}

Datum
xlogminer_stop(PG_FUNCTION_ARGS)
{
	checkLogminerUser();
	cleanSystableDictionary();
	cleanXlogfileList();
	dropAnalyseFile();
	PG_RETURN_TEXT_P(cstring_to_text("walminer cleaned!"));
}

Datum
xlogminer_xlogfile_add(PG_FUNCTION_ARGS)
{
	text	*xlogfile = NULL;
	char	backstr[100] = {0};
	int		addnum = 0;
	int		dicloadtype = 0;
	char	dic_path[MAXPGPATH] = {0};
	
	if(!PG_GETARG_DATUM(0))
		ereport(ERROR,(errmsg("Please enter a file path or directory.")));
	xlogfile = PG_GETARG_TEXT_P(0);
	cleanSystableDictionary();
	checkLogminerUser();
	loadXlogfileList();

	loadDicStorePath(dic_path);
	if(0 == dic_path[0])
	{
		dicloadtype = PG_LOGMINER_DICTIONARY_LOADTYPE_NOTHING;
	}
	else
	{
		loadSystableDictionary(dic_path, ImportantSysClass,true);
		dicloadtype = getDatadictionaryLoadType();
	}
	if(PG_LOGMINER_DICTIONARY_LOADTYPE_NOTHING == dicloadtype)
	{
		outputSysTableDictionary(NULL, ImportantSysClass, true);
		loadSystableDictionary(NULL, ImportantSysClass,true);
		writeDicStorePath(dictionary_path);
		ereport(NOTICE,(errmsg("Get data dictionary from current database.")));
	}
	addnum = addxlogfile(text_to_cstring(xlogfile));
	writeXlogfileList();
	cleanXlogfileList();
	cleanSystableDictionary();
	snprintf(backstr, 100, "%d file add success",addnum);
	PG_RETURN_TEXT_P(cstring_to_text(backstr));
}

Datum
xlogminer_xlogfile_remove(PG_FUNCTION_ARGS)
{
	text	*xlogfile = NULL;
	char	backstr[100] = {0};
	int		removenum = 0;
	char	dic_path[MAXPGPATH] = {0};

	cleanSystableDictionary();
	checkLogminerUser();

	if(!PG_GETARG_DATUM(0))
		ereport(ERROR,(errmsg("Please enter a file path or directory.")));
	xlogfile = PG_GETARG_TEXT_P(0);
	loadXlogfileList();

	loadDicStorePath(dic_path);
	if(0 != dic_path[0])
	{
		loadSystableDictionary(dic_path, ImportantSysClass,true);
	}
	
	if(PG_LOGMINER_DICTIONARY_LOADTYPE_NOTHING == getDatadictionaryLoadType())
		ereport(ERROR,(errmsg("DataDictionary has not been loaded.")));
	removenum = removexlogfile(text_to_cstring(xlogfile));
	writeXlogfileList();
	cleanXlogfileList();
	snprintf(backstr, 100, "%d file remove success",removenum);
	PG_RETURN_TEXT_P(cstring_to_text(backstr));
}

Datum
xlogminer_xlogfile_list(PG_FUNCTION_ARGS)
{
	FuncCallContext *funcctx = NULL;
	logminer_fctx	*temp_fctx = NULL;
	if (SRF_IS_FIRSTCALL())
	{
		logminer_fctx	*fctx = NULL;
		MemoryContext 	oldcontext = NULL;
		TupleDesc		tupdesc = NULL;
		cleanSystableDictionary();
		checkLogminerUser();
		loadXlogfileList();
		if(!is_xlogfilelist_exist())
			ereport(ERROR,(errmsg("Xlogfilelist has not been loaded or has been removed.")));
		
		funcctx = SRF_FIRSTCALL_INIT();
		oldcontext = MemoryContextSwitchTo(funcctx->multi_call_memory_ctx);
		fctx = (logminer_fctx *)logminer_palloc(sizeof(logminer_fctx),0);
		fctx->hasnextxlogfile= true;
		funcctx->user_fctx = fctx;
		
		tupdesc = makeOutputXlogDesc();
		funcctx->attinmeta = TupleDescGetAttInMetadata(tupdesc);
		MemoryContextSwitchTo(oldcontext);
	}
	funcctx = SRF_PERCALL_SETUP();
	temp_fctx = (logminer_fctx*)funcctx->user_fctx;
	while(temp_fctx->hasnextxlogfile)
	{
		HeapTuple	tuple;
		char		*values[1];
		char		*xlogfile = NULL;
		
		xlogfile = getNextXlogFile(funcctx->user_fctx, true);
		
		values[0] = xlogfile;
		tuple = BuildTupleFromCStrings(funcctx->attinmeta, values);
		SRF_RETURN_NEXT(funcctx, HeapTupleGetDatum(tuple));
	}
	cleanXlogfileList();
	SRF_RETURN_DONE(funcctx);
}

/*
xlog analyse begin here
*/
Datum pg_minerXlog(PG_FUNCTION_ARGS)
{
	TimestampTz				xacttime = 0;
	bool					getrecord = true;
	char					*starttimestamp = NULL;
	char					*endtimestamp = NULL;
	int32					startxid = 0;
	int32					endxid = 0;
	XLogSegNo				segno;
	char					*directory = NULL;
	char					*fname = NULL;
	char					*firstxlogfile = NULL;
	int						dicloadtype = 0;
	char					dictionary[MAXPGPATH] = {0};

	memset(&rrctl, 0, sizeof(RecordRecycleCtl));
	memset(&sysclass, 0, sizeof(SystemClass)*PG_LOGMINER_SYSCLASS_MAX);
	memset(&srctl, 0, sizeof(SQLRecycleCtl));
	memset(&sql_reass, 0, sizeof(XLogMinerSQL));
	memset(&sql, 0, sizeof(XLogMinerSQL));

	sqlnoser = 0;
	cleanSystableDictionary();
	checkLogminerUser();
	logminer_createMemContext();
	rrctl.tuplem = getTuplemSpace(0);
	rrctl.tuplem_old = getTuplemSpace(0);
	rrctl.lfctx.sendFile = -1;

	if(!PG_GETARG_DATUM(0) || !PG_GETARG_DATUM(1))
		ereport(ERROR,(errmsg("The time parameter can not be null.")));
	starttimestamp = text_to_cstring((text*)PG_GETARG_CSTRING(0));
	endtimestamp = text_to_cstring((text*)PG_GETARG_CSTRING(1));
	startxid = PG_GETARG_INT32(2);
	endxid = PG_GETARG_INT32(3);
	tempresultout = PG_GETARG_BOOL(4);
	if(0 > startxid || 0 > endxid)
		ereport(ERROR,(errmsg("The XID parameters cannot be negative.")));

	if(!tempresultout)
	{
		outTempleResult("NO OUT TEMP RESULT!");
		closeTempleResult();
	}

	rrctl.logprivate.parser_start_xid = startxid;
	rrctl.logprivate.parser_end_xid = endxid;
	/*parameter check*/
	inputParaCheck(starttimestamp, endtimestamp);
		
	loadDicStorePath(dictionary);
	if(0 == dictionary[0])
		ereport(ERROR,(errmsg("Xlogfilelist must be loaded first.")));
	loadSystableDictionary(dictionary, ImportantSysClass,false);
	dicloadtype = getDatadictionaryLoadType();

	if(PG_LOGMINER_DICTIONARY_LOADTYPE_SELF == dicloadtype)
	{
		char *datadic = NULL;
		cleanSystableDictionary();
		datadic = outputSysTableDictionary(NULL, ImportantSysClass, true);
		loadSystableDictionary(NULL, ImportantSysClass,true);
		if(datadic)
			remove(datadic);
	}

	loadXlogfileList();
	if(!is_xlogfilelist_exist())
		ereport(ERROR,(errmsg("Xlogfilelist must be loaded first.")));
	checkXlogFileList();

	searchSysClass(sysclass,&sysclassNum);
	relkind_miner = getRelKindInfo();

	
	firstxlogfile = getNextXlogFile((char*)(&rrctl.lfctx),false);
	rrctl.lfctx.xlogfileptr = NULL;
	split_path_fname(firstxlogfile, &directory, &fname);
	XLogFromFileName(fname, &rrctl.logprivate.timeline, &segno, rrctl.WalSegSz);
	if(fname)
		logminer_pfree(fname,0);
	if(directory)
		logminer_pfree(directory,0);

	/* if this wal file include catalog relation info*/
	if(1 == segno)
		rrctl.system_init_record = PG_LOGMINER_XLOG_DBINIT;
	else
	{
		rrctl.system_init_record = PG_LOGMINER_XLOG_NOMAL;
		rrctl.sysstoplocation = PG_LOGMINER_FLAG_INITOVER;
	}
	cleanStorefile();
	/*configure call back func*/
	rrctl.xlogreader_state = XLogReaderAllocate(rrctl.WalSegSz, XLogMinerReadPage, &rrctl.logprivate);
	XLogSegNoOffsetToRecPtr(segno, 0, rrctl.WalSegSz, rrctl.logprivate.startptr);
	if(!rrctl.xlogreader_state)
		ereport(ERROR,(errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED),errmsg("Out of memory")));
	rrctl.first_record = XLogFindFirstRecord(rrctl.xlogreader_state, rrctl.logprivate.startptr);
	while(!rrctl.logprivate.endptr_reached)
	{
		/*if in a mutiinsert now, avoid to get new record*/
		if(!srctl.mutinsert)
		{
			getrecord = getNextRecord();
		}
		if(getrecord)
			sqlParser(rrctl.xlogreader_state, &xacttime);
		else if(!rrctl.logprivate.serialwal)
		{
			rrctl.logprivate.serialwal = true;
			rrctl.logprivate.changewal = false;
			rrctl.first_record = XLogFindFirstRecord(rrctl.xlogreader_state, rrctl.logprivate.startptr);
		}
	}
	cleanSystableDictionary();
	cleanXlogfileList();
	dropAnalyseFile();
	freeSQLspace();
	freeSpace(&srctl.sql_simple);
	freeSpace(&srctl.sql_undo);
	XLogReaderFree(rrctl.xlogreader_state);
	pfree(rrctl.tuplem);
	pfree(rrctl.tuplem_old);
	cleanStorefile();
	closeTempleResult();
	if(rrctl.imagelist)
		list_free(rrctl.imagelist);
	logminer_switchMemContext();
	if(rrctl.hasunanalysetuple)
		PG_RETURN_TEXT_P(cstring_to_text("walminer sucessful with some tuple missed!"));
	else
		PG_RETURN_TEXT_P(cstring_to_text("walminer sucessful!"));
}
