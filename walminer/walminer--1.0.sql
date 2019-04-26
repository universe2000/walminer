/* contrib/xlogminer/xlogminer--1.0.sql */
CREATE OR REPLACE FUNCTION pg_minerwal(starttime text, endtime text, startxid int, endxid int, tempresult bool, reloid  Oid)
RETURNS text AS
'MODULE_PATHNAME','pg_minerXlog'
LANGUAGE C CALLED ON NULL INPUT;

CREATE OR REPLACE FUNCTION walminer_temp_table_check()
RETURNS void AS
$BODY$
DECLARE
	rd "varchar";
	checksql "varchar";
	temptablename "varchar";
	tp "varchar";
BEGIN
	temptablename := 'walminer_contents';
	tp :='t';
	SELECT * into rd FROM pg_catalog.pg_class WHERE relname = 'walminer_contents' AND relpersistence = 't';
	IF FOUND THEN
		TRUNCATE TABLE walminer_contents;
	ELSE
		CREATE temp TABLE walminer_contents(xid bigint,virtualxid int,timestampTz timestampTz,record_database text, record_user text,record_tablespace text,record_schema text, op_type text,op_text text,op_undo text);
	END IF;
END;
$BODY$
LANGUAGE 'plpgsql' VOLATILE; 


CREATE OR REPLACE FUNCTION walminer_start(starttime text, endtime text, startxid int, endxid int, tempresult bool DEFAULT 'false')
RETURNS text AS 
$BODY$
	select walminer_temp_table_check();
	select pg_minerwal($1,$2,$3,$4,$5,0);
$BODY$
LANGUAGE 'sql';

CREATE OR REPLACE FUNCTION walminer_start_simpletable(reloid Oid,tempresult bool DEFAULT 'false')
RETURNS text AS 
$BODY$
	select walminer_temp_table_check();
	select pg_minerwal('NULL', 'NULL', 0, 0, $2, $1);
$BODY$
LANGUAGE 'sql';

CREATE OR REPLACE FUNCTION walminer_build_dictionary(in path text)
RETURNS text AS
'MODULE_PATHNAME','xlogminer_build_dictionary'
LANGUAGE C CALLED ON NULL INPUT;

CREATE OR REPLACE FUNCTION walminer_load_dictionary(in path text)
RETURNS text AS
'MODULE_PATHNAME','xlogminer_load_dictionary'
LANGUAGE C CALLED ON NULL INPUT;

CREATE OR REPLACE FUNCTION walminer_stop()
RETURNS text AS
'MODULE_PATHNAME','xlogminer_stop'
LANGUAGE C VOLATILE STRICT;

CREATE OR REPLACE FUNCTION walminer_wal_add(in path text)
RETURNS text AS
'MODULE_PATHNAME','xlogminer_xlogfile_add'
LANGUAGE C CALLED ON NULL INPUT;

CREATE OR REPLACE FUNCTION walminer_wal_remove(in path text)
RETURNS text AS
'MODULE_PATHNAME','xlogminer_xlogfile_remove'
LANGUAGE C CALLED ON NULL INPUT;

CREATE OR REPLACE FUNCTION walminer_wal_list()
RETURNS setof record  AS
'MODULE_PATHNAME','xlogminer_xlogfile_list'
LANGUAGE C VOLATILE STRICT;

