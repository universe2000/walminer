/*
*
*contrib/xlogminer/datadictionary.h
*/
#ifndef PG_DATADICTIONARY_H
#define PG_DATADICTIONARY_H
#include "postgres.h"

#include "logminer.h"

#define PG_LOGMINER_DICTIONARY_DEFAULTNAME			"dictionary.d"
#define PG_LOGMINER_DICTIONARY_STOREPATH			"pgdictory.store"
#define	PG_LOGMINER_DICTIONARY_XLOGFILELIST			"xloglist.list"
#define PG_LOGMINER_DICTIONARY_TEMPTABLE			"walminer_contents"

#define PG_LOGMINER_DICTIONARY_PATHCHECK_NULL			0
#define PG_LOGMINER_DICTIONARY_PATHCHECK_FILE			1
#define PG_LOGMINER_DICTIONARY_PATHCHECK_DIR			2
#define PG_LOGMINER_DICTIONARY_PATHCHECK_INVALID		3
#define PG_LOGMINER_DICTIONARY_PATHCHECK_SINGLE			4

#define	PG_LOGMINER_XLOGFILE_REPEAT_CHECK_DIFFERENT		1
#define	PG_LOGMINER_XLOGFILE_REPEAT_CHECK_SAME			2
#define	PG_LOGMINER_XLOGFILE_REPEAT_CHECK_SEGSAME		3


#define	PG_LOGMINER_DICTIONARY_LOADTYPE_SELF			1
#define	PG_LOGMINER_DICTIONARY_LOADTYPE_OTHER			2
#define	PG_LOGMINER_DICTIONARY_LOADTYPE_NOTHING			3

#define	PG_LOGMINER_DICTIONARY_SYSDATACACHE_SIZE		200 * 1024
#define RELMAPPER_FILENAME		"pg_filenode.map"
#define RELMAPPER_FILEMAGIC		0x592717	/* version ID value */
#define MAX_MAPPINGS			62	/* 62 * 8 + 16 = 512 */


extern char* DataDictionaryCache;
extern char* XlogfileListCache;
extern char	dictionary_path[MAXPGPATH];

typedef struct SysDataCache{
	char*	data;
	char*	curdata;
	int64	usesize;
	int64	totsize;
	int		elemnum;
}SysDataCache;

typedef struct DataDicHead{
	NameData relname;
	int		 elemnum;
}DataDicHead;

typedef struct RelMapping
{
	Oid			mapoid;			/* OID of a catalog */
	Oid			mapfilenode;	/* its filenode number */
} RelMapping;


typedef struct RelMapFile
{
	int32		magic;			/* always RELMAPPER_FILEMAGIC */
	int32		num_mappings;	/* number of valid RelMapping entries */
	RelMapping	mappings[MAX_MAPPINGS];
	pg_crc32c	crc;			/* CRC of all above */
	int32		pad;			/* to make the struct size be 512 exactly */
} RelMapFile;


typedef struct PgDataDic{
	uint64			sysid;
	Oid				dboid;
	int				dicloadtype;
	TimeLineID		maxtl;
	TimeLineID		datadictl;
	bool			mutitimeline;
	RelMapFile		shared_map;
	RelMapFile		local_map;
	SysDataCache	sdc[PG_LOGMINER_IMPTSYSCLASS_IMPTNUM];
}PgDataDic;

typedef struct XlogFile	
{			
	char filepath[MAXPGPATH];
	struct XlogFile* tail;
	struct XlogFile* next;
}XlogFile;

typedef XlogFile* XlogFileList;			


char* outputSysTableDictionary(char *path, SysClassLevel *scl, bool self);
void loadSystableDictionary(char *path, SysClassLevel *scl, bool self);
int addxlogfile(char *path);
int removexlogfile(char *path);
void cleanSystableDictionary(void);
void cleanXlogfileList(void);
bool getRelationOidByName(char* relname, Oid* reloid, bool gettemptable);
uint64 getDataDicSysid(void);
int getDatadictionaryLoadType(void);
bool is_xlogfilelist_exist(void);
int getXlogFileNum(void);
int getRelationNameByOid(Oid reloid, NameData* relname);
TupleDesc GetDescrByreloid(Oid reloid);
bool tableIftoastrel(Oid reloid);
bool getTypeOutputFuncFromDic(Oid type, Oid *typOutput, bool *typIsVarlena);
void writeXlogfileList(void);
void writeDicStorePath(char* dicstorepath);
void dropAnalyseFile(void);
TimeLineID getDataDictl(void);
bool loadXlogfileList(void);
bool loadDicStorePath(char *dicstorepath);
void checkXlogFileList(void);
char* getNextXlogFile(char *fctx, bool show);
void  searchSysClass( SystemClass *sys_class,int	*sys_classNum);
char* getdbNameByoid(Oid dboid, bool createdb);
char* gettbsNameByoid(Oid tbsoid);
Oid getTbsoidByreloid(Oid reloid);
char* gettbsNameByoid(Oid tbsoid);
Oid gettuserOidByReloid(Oid reloid);
char* getuserNameByUseroid(Oid useroid);
Oid getuserOidByUsername(char *user_name);
Oid getnsoidByReloid(Oid reloid);
Oid gettoastRelidByReloid(Oid reloid);
char* getnsNameByOid(Oid schoid);
char* getnsNameByReloid(Oid reloid);
bool getLangNameByOid(Oid langoid, NameData* langname);
bool getTypnameBytypid(Oid typid, char* typname);
Oid getRelationOidByRelfileid(Oid relNodeid);
bool getRelAttNameByReloidAttid(Oid attoid, int attid, char* attname);
void freetupdesc(TupleDesc tupdesc);
bool is_xlogfilelist_exist(void);
uint64 getDataDicSysid(void);
Oid getDataDicOid(void);
TimeLineID getDataDictl(void);
bool create_dir(char *path);
Oid getRelidbyRelnodeViaMap(Oid filenode, bool shared);



#endif
