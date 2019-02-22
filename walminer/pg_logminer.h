/*
*
*contrib/xlogminer/pg_logminer.h
*/

#ifndef PG_LOGMINER_H
#define PG_LOGMINER_H
#include "postgres.h"
#include "access/xlog.h"
#include "access/xlogreader.h"
#include "access/heapam_xlog.h"
#include "access/xact.h"
#include "utils/relfilenodemap.h"
#include "utils/syscache.h"
#include "commands/dbcommands.h"
#include "commands/tablespace.h"
#include "common/fe_memutils.h"
#include "catalog/pg_class.h"
#include "access/htup_details.h"
#include "utils/rel.h"
#include "funcapi.h"
#include "miscadmin.h"
#include "utils/memutils.h"
#include "xlogminer_contents.h"


#define XLogRecGetData_Logminer(record)	((char*) (record) + SizeOfXLogRecord)

#define PG_LOGMINER_FLAG_FINDHIGHGO		-10
#define PG_LOGMINER_FLAG_INITOVER		-8
#define PG_LOGMINER_CONTRLKIND_FIND		1 
#define PG_LOGMINER_CONTRLKIND_XACT		2 

#define PG_LOGMINER_OID_PGCATALOG		11
#define	PG_LOGMINER_INITCHECK_STEP		10
#define	PG_LOGMINER_INITCHECK_START		1
#define	PG_LOGMINER_INITCHECK_END		10

#define PG_LOGMINER_SQLKIND_UPDATE		1
#define PG_LOGMINER_SQLKIND_INSERT		2
#define PG_LOGMINER_SQLKIND_DELETE		3
#define	PG_LOGMINER_SQLKIND_CREATE		4
#define	PG_LOGMINER_SQLKIND_ALTER		5
#define	PG_LOGMINER_SQLKIND_XACT		6
#define	PG_LOGMINER_SQLKIND_DROP		7
#define PG_LOGMINER_SQLKIND_MAXNUM		7

#define	PG_LOGMINER_TABLEKIND_SYSCLASS		1
#define	PG_LOGMINER_TABLEKIND_IMPTSYS		2
#define	PG_LOGMINER_TABLEKIND_NORMAL		3

#define	PG_LOGMINER_WALFILE_ERROR_NOFIND	-1	/*did not find the wal file*/
#define	PG_LOGMINER_WALFILE_ENDALL	-2	/*arrive at num that user refer to*/
#define	PG_LOGMINER_WALFILE_NOTSERIAL	-3	/*serial wal only*/
#define	PG_LOGMINER_WALFILE_ERROR_COUNT		-10


#define	PG_LOGMINER_XLOG_UNKNOWN	0
#define	PG_LOGMINER_XLOG_DBINIT		1
#define	PG_LOGMINER_XLOG_NOMAL		2

#define PG_LOGMINER_SYSCLASS_MAX		80
#define	PG_LOGMINER_XLOGMINERSQL_MAXSIZE	100000

#define PG_LOGMINER_IMPTSYSCLASS_IMPTNUM		18
#define PG_LOGMINER_IMPTSYSCLASS_PGCLASS		0
#define PG_LOGMINER_IMPTSYSCLASS_PGDATABASE		1
#define PG_LOGMINER_IMPTSYSCLASS_PGEXTENSION	2
#define PG_LOGMINER_IMPTSYSCLASS_PGNAMESPACE	3
#define PG_LOGMINER_IMPTSYSCLASS_PGTABLESPACE	4
#define	PG_LOGMINER_IMPTSYSCLASS_PGCONSTRAINT	5
#define	PG_LOGMINER_IMPTSYSCLASS_PGAUTHID		6
#define	PG_LOGMINER_IMPTSYSCLASS_PGPROC			7
#define	PG_LOGMINER_IMPTSYSCLASS_PGDEPEND		8
#define	PG_LOGMINER_IMPTSYSCLASS_PGINDEX		9
#define	PG_LOGMINER_IMPTSYSCLASS_PGATTRIBUTE	10
#define	PG_LOGMINER_IMPTSYSCLASS_PGSHDESC		11
#define	PG_LOGMINER_IMPTSYSCLASS_PGATTRDEF		12
#define	PG_LOGMINER_IMPTSYSCLASS_PGTYPE			13
#define	PG_LOGMINER_IMPTSYSCLASS_PGAUTH_MEMBERS	14
#define	PG_LOGMINER_IMPTSYSCLASS_PGINHERITS		15
#define	PG_LOGMINER_IMPTSYSCLASS_PGTRIGGER		16
#define	PG_LOGMINER_IMPTSYSCLASS_PGLANGUAGE		17

#define SYSLEL_A					1
#define SYSLEL_B					2
#define SYSLEL_C					3


#define PG_LOGMINER_DATABASE_POSTGRES	"postgres"
#define PG_LOGMINER_DATABASE_HIGHGO		"highgo"
#define PG_LOGMINER_TABLE_DATABASE		"pg_database"
#define PG_LOGMINER_TABLE_SHDESC		"pg_shdescription"
#define PG_LOGMINER_TABLE_DEPEND		"pg_depend"
#define PG_LOGMINER_TABLE_ATTRIBUTE		"pg_attribute"
#define PG_LOGMINER_TABLE_INDEX			"pg_index"
#define PG_LOGMINER_TABLE_CONSTRAINT	"pg_constraint"
#define PG_LOGMINER_TABLE_CLASS			"pg_class"

#define	PG_LOGMINER_PATH				"walminer"
#define	PG_LOGMINER_STOREIMAGE_FILENAME "soreimage"
#define	PG_LOGMINER_TEMPRESULT_FILENAME "tempresult"

#define	PG_DEBUG_STRINFO_SIZE			1000



typedef struct SystemClass{
	NameData classname;
	int		 attnum;
}SystemClass;

typedef SystemClass *SysClass;

typedef struct SysClassLevel{
	int	 tabid;
	char *relname;
	int	 datasgsize;
}SysClassLevel;

typedef struct SQLKind{
	char *sqlhead;
	int	 sqlid;
}SQLKind;

typedef struct S_Constraint{
	char *conname;
	char contype;
}S_Constraint;

typedef struct XLogMinerSQL
{
	char	*sqlStr;
	int		tot_size;
	int		rem_size;
	int		use_size;
}XLogMinerSQL;

typedef struct XLsqList *XLsqlptr;
typedef struct XLsqList
{
	XLogMinerSQL	xlsql;
	XLsqlptr		next;
}XLsqList;

typedef struct XLsqlhead
{
	XLsqlptr head;
	XLsqlptr tail;
	int		 xlnum;
}XLsqlhead;

typedef struct ToastTuple
{
	Oid		chunk_id;
	int		chunk_seq;
	char	*chunk_data;
	int		datalength;
	struct ToastTuple* next;
}ToastTuple;


typedef struct XLogMinerPrivate
{
	TimeLineID		timeline;
	XLogRecPtr		startptr;
	int				analynum;
	bool			staptr_reached;
	bool			endptr_reached;
	bool			timecheck;
	bool			xidcheck;
	bool			changewal;
	bool			serialwal;


	XLogRecPtr		limitstartptr;
	XLogRecPtr		limitendptr;
	TimestampTz		parser_start_time;
	TimestampTz		parser_end_time;
	TransactionId 	parser_start_xid;
	TransactionId 	parser_end_xid;

	Oid				xlogminer_contents_oid;
}XLogMinerPrivate;

typedef struct
{
	long	offset;
	bool	hasnextbuff;
	bool	hasnextxlogfile;
	char	*xlogfileptr;

	int	sendFile;
	XLogSegNo sendSegNo;
	uint32 sendOff;

	
}logminer_fctx;

typedef struct ImageStore
{
	RelFileNode rnode;
	ForkNumber	forknum;
	BlockNumber blkno;

	XLogRecPtr	ReadRecPtr;		/* start of store record read */
	XLogRecPtr	EndRecPtr;		/* end+1 of store record read */
	
}ImageStore;

typedef struct RecordRecycleCtl
{
	XLogReaderState		*xlogreader_state;
	XLogRecPtr			first_record;
	XLogMinerPrivate	logprivate;
	bool				hasstore;

	char				*tuplem;
	char				*tuplem_old;
	char				*tuplem_bigold;
	Oid					reloid;
	Oid					tbsoid;
	bool				nomalrel;
	bool				sysrel;
	bool				imprel;
	bool				toastrel;
	int					sqlkind;
	uint8				prostatu;
	TransactionId		recordxid;
	

	XLogMinerSQL 		tupinfo;
	bool				tupinfo_init;
	XLogMinerSQL 		tupinfo_old;
	bool				tupinfo_old_init;
	char	  			*errormsg;
	int					system_init_record;
	int					recyclecount;
	int					insertcount;
	int					sysstoplocation;
	logminer_fctx		lfctx;
	int					temp_size;
	
	Datum	   			*values;
	bool	   			*nulls;
	Datum	   			*values_old;
	bool	   			*nulls_old;
	TupleDesc			tupdesc;
	ToastTuple			*tthead;

	MemoryContext		oldcxt;
	MemoryContext		mycxt;
	MemoryContext		tupledesccxt;

	bool				getcheckpoint;
	List				*imagelist;

	bool				hasunanalysetuple;
}RecordRecycleCtl;

typedef struct Attribute *AttPtr;

typedef struct Attribute{
	NameData attname;
	int		 atttypid;
	int		 attndims;
	int		 atttypmod;
	bool	 attnotnull;

	NameData atttypname;
	AttPtr next;
}Attribute;

typedef struct ConstraintMiner *ConsPtr;

typedef struct ConstraintMiner{
	NameData		conname;
	NameData		contypename;
	NameData		contablename;
	NameData		refkeytablename;
	Oid				conoid;
	Oid				conkeytableoid;
	Oid				refkeytableoid;
	int				conattnum;
	int				*foratt;
	int				*refatt;
	NameData		*forattName;
	NameData		*refattName;
	char			*consrc;
	char			conkind;
	bool			connoinhert;
	ConsPtr			next;
}ConstraintMiner;

typedef struct Funct{
	NameData		funname;
	NameData		funcrettype;
	NameData		funclang;
	XLogMinerSQL	funcpara;
	XLogMinerSQL	funcstruct;
	XLogMinerSQL	funcbin;
	XLogMinerSQL	funcdefault;
}Funct;

typedef struct CreateInfo
{
	NameData createtar;     /*TABLE,INDEX,EXTENTION,CONSTRAINT*/
	NameData createname;    /*TABLE name,INDEX name,EXTENTION name,CONSTRAINT name*/
	NameData targettable;   /*TABLE name after ON*/
	Oid		 relfilenode;
	
	Attribute *createatt_head;
	Attribute *createatt_tail;
	int		  attnum;
	int		  attnumtotal;
	
	XLogMinerSQL	impsql;
	bool			inseg;
	int				segkind1;
	int				segkind2;

	ConsPtr			cons_head;
	ConsPtr			cons_tail;
	int		 		consnum;
	
	int				sqlgetflag;
	
	Oid				indexrelId;
	Oid				indrelId;
	Oid				schemaOid;
	Funct			func;

	XLogMinerSQL	defval;

	/*create table*/
	char			persistence;
}CreateTableInfo;


typedef struct SQLRecycleCtl
{
	XLogMinerSQL	sql_simple;
	XLsqlhead		xlhead;
	XLsqlhead 		sqlbuf;
	XLsqlptr		sqlptr;
	/*process contrl*/
	CreateTableInfo createinfo;
	CreateTableInfo dropinfo;

	/*for  toast index remove when create*/
	Oid				toastoid;

	/*for record to xlogminer_contents*/
	XLogMinerSQL	sql_undo;
	RelFileNode		rfnode;

	/*for mutiinsert*/
	bool			mutinsert;
	bool			isinit;
	int				sqlindex;
	char			*multdata;
	/*For store sqls in a xact*/
	char			*xcf;
	int				xcftotnum;
	int				xcfcurnum;
	
	bool			getanysql;
}SQLRecycleCtl;

typedef struct XlogResult{
	XLogMinerSQL 	sql;
	char			ct[25];
}XlogResult;

extern RecordRecycleCtl rrctl;
extern SQLRecycleCtl	srctl;
extern uint32	sqlnoser;
extern bool				log_mod;
extern bool				debug_mode;

void appendtoSQL(XLogMinerSQL *sql_simple, char *sqlpara , int spaceKind);

char* getConsKindNameByKind(char kind, char *contypname);
XLogRecord *XLogReadRecord_logminer(XLogReaderState *state, XLogRecPtr RecPtr, char **errormsg);
XLogRecPtr XLogFindFirstRecord(XLogReaderState *state, XLogRecPtr RecPtr);
int XLogMinerReadPage(XLogReaderState *state, XLogRecPtr targetPagePtr, int reqLen,
				 XLogRecPtr targetPtr, char *readBuff, TimeLineID *curFileTLI);
int XLogMinerXLogRead(const char *directory, TimeLineID *timeline_id,
				 XLogRecPtr startptr, char *buf, Size count);
SysClassLevel *getImportantSysClass(void);
void processContrl(char* relname, int contrlkind);
void appendtoSQL_atttyptrans(XLogMinerSQL *sql_simple, Oid typoid);
void appendtoSQL_valuetyptrans(XLogMinerSQL *sql_simple, Oid typoid);
void appendtoSQL_simquo(XLogMinerSQL *sql_simple, char* ptr, bool quoset);
void appendtoSQL_doubquo(XLogMinerSQL *sql_simple, char* ptr, bool quoset);
void deleteCharFromSQL(XLogMinerSQL *sql_simple);
void wipeSQLFromstr(XLogMinerSQL *sql_simple,char *fromstr,char *checkstr);
void appendBlanktoSQL(XLogMinerSQL *sql_simple);

void outTempleResult(char *str);

#endif
