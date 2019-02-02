/*
*
*contrib/xlogminer/logminer.h
*/

#ifndef LOGMINER_H
#define LOGMINER_H
#include "postgres.h"
#include "pg_logminer.h"
#include "utils/timestamp.h"
#include "utils/datetime.h"
#include "access/xlog_internal.h"



#define bbr_Judge(ch) ((PG_LOGMINER_BBRACK_R == ch)?true:false)
#define bbl_Judge(ch) ((PG_LOGMINER_BBRACK_L == ch)?true:false)
#define sbr_Judge(ch) ((PG_LOGMINER_SBRACK_R == ch)?true:false)
#define sbl_Judge(ch) ((PG_LOGMINER_SBRACK_L == ch)?true:false)

#define	LOGMINERDIR_DIC					"dic"
#define	LOGMINERDIR_LIST				"list"

#define PG_LOGMINER_SPACE				' '
#define PG_LOGMINER_SBRACK_L			'('
#define PG_LOGMINER_SBRACK_R			')'
#define PG_LOGMINER_COMMA				','
#define PG_LOGMINER_BBRACK_L			'{'
#define PG_LOGMINER_BBRACK_R			'}'
#define PG_LOGMINER_ENDSTR				'\n'

#define	PG_LOGMINER_RELNULL				"NULL"

#define PG_LOMINER_ALLROLEPRIV				"arwdDxt"

#define LOGMINER_PRIVKIND_MAXNUM	15

#define PG_LOGMINER_SQLPARA_TOTSTEP 10000
#define PG_LOGMINER_SQLPARA_SIMSTEP 100
#define PG_LOGMINER_SQLPARA_TOTLE 	1
#define PG_LOGMINER_SQLPARA_SIMPLE 	2
#define PG_LOGMINER_SQLPARA_OTHER 	3

#define	LOGMINER_INSERTSTEP						4
#define	LOGMINER_DELETESTEP						5


/*-----------sysclass  attribute location-----------*/
#define	LOGMINER_STEP							1
#define LOGMINER_ATTRIBUTE_LOCATION_UPDATE_RELNAME			2
#define	LOGMINER_ATTRIBUTE_LOCATION_UPDATE_NEWDATA			5
#define	LOGMINER_ATTRIBUTE_LOCATION_UPDATE_OLDDATA			8
#define LOGMINER_ATTRIBUTE_LOCATION_CREATE_KIND						2
#define LOGMINER_ATTRIBUTE_LOCATION_CREATE_NAME						3
#define	LOGMINER_INSERT_TABLE_NAME						3
#define	LOGMINER_SQL_COMMAND							1
#define	LOGMINER_DELETE_TABLE_NAME						3
/*--------------------------------------------*/


#define	LOGMINER_RELKINDID_TABLE				1
#define	LOGMINER_RELKINDID_INDEX				2
#define	LOGMINER_RELKINDID_SEQUENCE				3
#define	LOGMINER_RELKINDID_VIEW					4
#define	LOGMINER_RELKINDID_TOAST				5
#define	LOGMINER_RELKINDID_COMPLEX				6
#define LOGMINER_RELATIONKIND_NUM 				7

#define LOGMINER_CONSTRAINT_CONNUM			6
#define LOGMINER_CONSTRAINT_CHECK			'c'
#define LOGMINER_CONSTRAINT_FOREIGN			'f'
#define LOGMINER_CONSTRAINT_PRIMARY			'p'
#define LOGMINER_CONSTRAINT_UNIQUE			'u'
#define LOGMINER_CONSTRAINT_TRIGGER			't'
#define LOGMINER_CONSTRAINT_EXCLUSION		'x'



#define 	LOGMINER_SQLGET_DDL_CREATE_TABLE			1
#define 	LOGMINER_SQLGET_DDL_CREATE_INDEX			2
#define 	LOGMINER_SQLGET_DDL_CREATE_SEQUENCE			3
#define 	LOGMINER_SQLGET_DDL_CREATE_VIEW				4
#define 	LOGMINER_SQLGET_DDL_CREATE_TOAST			5
#define 	LOGMINER_SQLGET_DDL_CREATE_COMPLEX			6
#define 	LOGMINER_SQLGET_DDL_CREATE_CONSTRAINT		7
#define 	LOGMINER_SQLGET_DDL_CREATE_MATERIALIZEDVIEW			8

#define 	LOGMINER_SQLGET_DML_INSERT					8
#define 	LOGMINER_SQLGET_DML_UPDATE					9
#define 	LOGMINER_SQLGET_DML_DELETE					10
#define		LOGMINER_SQLGET_DML_ALTERTABLE_ADDCOLUMN					11
#define		LOGMINER_SQLGET_DML_ALTERTABLE_ALTERCOLTYP_SEG1				12
#define		LOGMINER_SQLGET_DML_ALTERTABLE_ALTERCOLTYP_SEG2				13
#define		LOGMINER_SQLGET_DML_ALTERTABLE_COLUMNDEFAULT				14
#define		LOGMINER_SQLGET_DDL_CREATE_DATABASE							15
#define 	LOGMINER_SQLGET_DDL_CREATE_PARTTABLE		16




#define		LOGMINER_SIMPLESQL_BUFSIZE					1024

#define		LOGMINER_TEMPFILE_SIMPLESQL		"simple.sql"
#define		LOGMINER_TEMPFILE_DBSQL		"db.sql"
#define		LOGMINER_PASSKIND_SPECIALLOC_MAX			5
#define		LOGMINER_PASSKIND_SPECIALCH_CUT				1	/*a special char as Separator only*/
#define		LOGMINER_PASSKIND_SPECIALCH_AVOID			2	/*a special char escape a Separator*/

#define		LOGMINER_WARNING_WORD_FULLPAGEWRITE			"data missing. \n"

#define		LOGMINER_PROSTATUE_INSERT_MISSING_TUPLEINFO			0x01
#define		LOGMINER_PROSTATUE_DELETE_MISSING_TUPLEINFO			0x02
#define		LOGMINER_PROSTATUE_UPDATE_MISSING_NEW_TUPLEINFO		0x03
#define		LOGMINER_PROSTATUE_UPDATE_MISSING_OLD_TUPLEINFO		0x04




typedef struct RelationKind{
	int		sqlkind;
	int		relkindid;
	char	*relname;
	char	relkind;
	bool	show;
}RelationKind;

typedef struct SQLPassOver{
	/*special location*/
	int		passloc[LOGMINER_PASSKIND_SPECIALLOC_MAX];
	/*special location num*/
	int		passlocnum;
	
	int		passkind;
	/*special char*/
	char	specialch[2];
	/*special char num*/
	int		specialnum;
}SQLPassOver;

typedef struct OperaPriv{
	char elemname[NAMEDATALEN];
	char privkind[LOGMINER_PRIVKIND_MAXNUM];
}OperaPriv;

typedef struct PrivKind{
	char	privch;
	char	*privstr;
}PrivKind;


typedef struct XlogminerContentsFirst{
	/*int 					sqlno*/
	TransactionId			xid;
	/*uint32					virtualxid;*/
	/*Timestamp				timestamp;*/
	XLogMinerSQL			record_database;
	XLogMinerSQL			record_user;
	XLogMinerSQL			record_tablespace;
	XLogMinerSQL			record_schema;
	XLogMinerSQL			op_type;
	XLogMinerSQL			op_text;
	XLogMinerSQL			op_undo;
}XlogminerContentsFirst;


bool getPhrases(char *sql,int loc, char *term, int ignoresbrackph);
void addSpace(XLogMinerSQL *sql_simple, int spaceKind);
void cleanSpace(XLogMinerSQL *minersql);
void freeSpace(XLogMinerSQL *minersql);
void split_path_fname(const char *path, char **dir, char **fname);
char* getRelkindnameByKind(char kind);
int getRelkindIdByKind(char kind);
bool makeAttribute(XLogMinerSQL *sql_ori, AttPtr *att, int step,bool createseg);
bool makeConstraint(XLogMinerSQL *sql_ori, ConsPtr *cons, int step);
RelationKind* getRelKindInfo(void);
void getConstraintSQL_unique(XLogMinerSQL *sql_opt,ConsPtr conptr);
void getConstraintSQL_primarykey(XLogMinerSQL *sql_opt,ConsPtr conptr);
void getConstraintSQL_check(XLogMinerSQL *sql_opt,ConsPtr conptr);
void getConstraintSQL_foreign(XLogMinerSQL *sql_opt,ConsPtr conptr);
void getPgextensionSQL(XLogMinerSQL *sql_ori, XLogMinerSQL *sql_opt);
void getcreateSQL(XLogMinerSQL *sql_opt,CreateTableInfo *createinfo);
void deleteConstraintSQL_foreign(XLogMinerSQL *sql_opt,ConsPtr conptr);
void deleteConstraintSQL_unique(XLogMinerSQL *sql_opt,ConsPtr conptr);
void deleteConstraintSQL_check(XLogMinerSQL *sql_opt,ConsPtr conptr);
void xactCommitSQL(char* timestr,XLogMinerSQL *sql_opt,uint8 info);
void getPgtablespaceSQL (XLogMinerSQL *sql_ori, XLogMinerSQL *sql_opt,bool ifcreate);
void tbsNameReplace(XLogMinerSQL *sql_ori, char* temp);
void getPgnamespaceSQL(XLogMinerSQL *sql_ori, XLogMinerSQL *sql_opt,bool idcreate);
void getPgauthidSQL(XLogMinerSQL *sql_ori, XLogMinerSQL *sql_opt);
void getPgdatabaseSQL(XLogMinerSQL *sql_ori, XLogMinerSQL *sql_opt,bool ifcreate);
void getTruncateSQL(XLogMinerSQL *sql_ori, XLogMinerSQL *sql_opt);
void getAlterTableAddColumnSQL(XLogMinerSQL *sql_opt, char *relname, Attribute *att);
void getAlterTableDropColumnSQL(XLogMinerSQL *sql_opt, char *relname, char *attname, Oid reloid);
void getAlterTableAlterColumnTypeSQL(XLogMinerSQL *sql_opt, char *relname, char *attname, char *atttyp, Oid reloid);
void getAlterTableAlterColumnNameSQL(XLogMinerSQL *sql_opt, char *relname, char *attname_old, char *attname_new, Oid reloid);
void getAlterTableName(XLogMinerSQL *sql_opt, char *attname_old, char *attname_new);
void getAlterTableNotnull(XLogMinerSQL *sql_opt, char *relname, char *attname, bool setnotnoll, Oid reloid);
int xlog_file_open(const char *directory, const char *fname);
bool isEmptStr(char *str);
void clean_xlsqlhead(XLsqlhead *xlhead);
void makeXLsql(char* sqlptr, XLsqList** xlsql);
bool parserPrivChg(char* newsql, char* oldsql, OperaPriv **priv, int *chgnum);
TupleDesc makeOutputXlogDesc(void);
bool inputParaCheck(char *st, char *et);
bool curXactCheck(TimestampTz xact_time ,TransactionId xid, bool xactcommit,xl_xact_parsed_commit *parsed_commit);
char* logminer_palloc(int size,int checkflag);
void logminer_free(char* ptr,int checkflag);
char* logminer_malloc(int size,int checkflag);
void logminer_free(char* ptr,int checkflag);
void logminer_createMemContext(void);
void logminer_switchMemContext(void);
bool checkLogminerUser(void);
bool padingminerXlogconts(char* elemname, TransactionId xid,int loc,long elemoid);
bool elemNameFind(char* elenname);
void cleanAnalyseInfo(void);
void padNullToXC(void);
char* getTuplemSpace(int addsize);
void cleanTuplemSpace(char* tuplem);
bool ifquoneed(Form_pg_attribute attrs);
char* OutputToByte(text* attrpter, int attlen);
ToastTuple* makeToastTuple(int datalength,char* data, Oid id, int seq);
void freeToastTupleHead(void);
void toastTupleAddToList(ToastTuple *tt);
text* cstringToTextWithLen(const char *s, int len);
bool getTypeOutputFuncFromDb(Oid type, Oid *typOutput, bool *typIsVarlena);
char* convertAttrToStr(Form_pg_attribute fpa,Oid typoutput, Datum attr);
void fixPathEnd(char *path);
char* logminer_palloc(int size,int checkflag);
void logminer_pfree(char* ptr,int checkflag);
char* logminer_malloc(int size,int checkflag);
void logminer_free(char* ptr,int checkflag);
void changeBigbrack(char *str, int size);
int	getPhrasesNum(char *sql,int ignoresbrackph);
void getpara_new(char *tuple, int loc, XLogMinerSQL *para, Oid reloid);
bool getPhrases_autolen(char *sql,int loc, XLogMinerSQL *autolen_sql,SQLPassOver *spo,int spoNum);
int getSqlkindIdByKind(char kind);
void cleanMentalvalues(void);
void getUpdateTupleData(XLogMinerSQL *sql_ori, XLogMinerSQL *sqlTuple, bool fromnewtup);
void getUpdateAttData(XLogMinerSQL *sql_ori,char *term,int loc,bool fromnewtup);
void  checkVarlena(Datum attr,struct varlena** att_return);
void mentalTup(HeapTuple tuple, TupleDesc typeinfo ,XLogMinerSQL *sql_simple, bool olddata);
void getInsertSQL(XLogMinerSQL *sql_simple, char *tupleInfo, NameData *relname, char* schname, bool sysrel);
void getDeleteSQL(XLogMinerSQL *sql_simple, char *tupleInfo, NameData *relname, char* schname, bool sysrel, bool undo);
void getUpdateSQL(XLogMinerSQL *sql_simple, char *tupleInfo, char *tupleInfo_old,NameData *relname, char* schname, bool sysrel);
void reAssembleDeleteSql(XLogMinerSQL *sql_ori, bool undo);
void reAssembleUpdateSql(XLogMinerSQL *sql_ori, bool undo);
void getGrantGroupSQL(XLogMinerSQL *sql_opt, char *rolename, char *membername, bool ifgrant);
bool getLangNameByOid(Oid langoid, NameData* langname);
void getfuncpara(XLogMinerSQL *sql_ori, Funct	*func);
void getFunctionSQL(XLogMinerSQL *sql_opt, Funct	 *func, bool langissql);
void getdropSQL(XLogMinerSQL *sql_opt,CreateTableInfo *dropinfo);
void getAlterTableDefault(XLogMinerSQL *sql_opt, char *relname, char *attname, char *defaultstr,bool setdefault, Oid reloid);
void getAlterConstraintName(XLogMinerSQL *sql_opt, char *relname, char *conname_old, char *conname_new, Oid reloid);
void getaltersettbs(XLogMinerSQL *sql_ori, XLogMinerSQL *sql_opt, char* tbsname);
void getAlterTableChschema(XLogMinerSQL *sql_opt, char *relname, char *nspname, Oid	schemaoid);
void getAlterTableOwner(XLogMinerSQL *sql_opt, char *relname, char *ownername);
void getGrantPrivSQL(XLogMinerSQL *sql_opt, char *relname, OperaPriv *priv, int chgnum, bool ifgrant);
void minerDbCreate(XLogReaderState *record, XLogMinerSQL *sql_simple,uint8 info);
void minerDbDrop(XLogReaderState *record, XLogMinerSQL *sql_simple,uint8 info);


#endif

