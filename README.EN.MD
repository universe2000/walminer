WalMiner
=====
an Open-Source SQL miner on PostgreSQL WAL log

# What is WalMiner
WalMiner is used for parsing the SQL statement out from the PostgreSQL WAL (write ahead logs) logs, and it can also generate the corresponding "undo SQL".

# Configuration requirements
Need to configure the database log level to be larger than minimal.

# Supported PostgreSQL versions
- The version before PG9.5.0 has not been tested.
- PG9.5.0~PG10.X version uses WalMiner_10_X
- After PG11, use WalMiner_11_X
- If you find problems during use, please feel free to give us feedback.

# Compile and install
1. Copy the walminer directory into the "../contrib/" directory of the PostgreSQL source code location
2. Enter "contrib/walminer" directory
3. Execute commands
```shell
make && make install
```

# User Guide
## Scenario I: Execute the parsing in the owner database of the WAL files
### 1. Create extension walminer
```sql
create extension walminer;
```

### 2. Add target WAL files
```sql
-- Add WAL file or directory
select walminer_wal_add('/opt/test/wal');
-- Note: the parameter can be file name or directory name.
```

### 3. Remove WAL files
```sql
-- remove WAL file or directory
select walminer_wal_remove('/opt/test/wal');
-- Note: the parameter can be file name or directory name.
```

### 4. List WAL files
```sql
-- List WAL files
select walminer_wal_list();
```

### 5. Execute the walminer
```sql
select walminer_start(’START_TIMSTAMP’,’STOP_TIMESTAMP’,’START_XID’,’STOP_XID’)
-- Run below sql to parse all the WAL logs
select walminer_start('null','null',0,0);
-- Run below will analyse the system table record and store in '$PGDATA/walminer/temp'
select walminer_start('null','null',0,0,true);
```
* **START_TIMESTAMP**：Specify the start time condition of the records in the return results, the walminer will start parsing from this time value. If this value is NULL, the earlist records will be displayed from the WAL lists. If this time value is not included in the xlog lists, a.k.a all the records are ealier then this value, the NULL will be returned.
* **STOP_TIMESTAMP**：Specify the ending time condition of the records in the results, the walminer will stop parsing when the result is later than this time. If this parameter is NULL, then all the records after **START_TIMESTAMP** will parsed and displyed in the WAL logs.
* **START_XID**：Similiar with **START_TIMESTAMP**, specify the starting **XID** value
* **STOP_XID**：Similiar with **STOP_TIMESTAMP**，specify the ending **XID** value	

:warning: **Only one of these two group parameters can be provided, or error will be retured**
	
### 6. Check the parsing result
```sql
select * from walminer_contents;
```

### 7. Stop the walminer
This function is used to free the memory and stop the WAL parsing. No parameters available.
```sql
select walminer_stop();
```


## Scenario II: Parsing the WAL logs from the database which is not the owner of these WAL logs
:warning: The target PostgreSQL database and the source database must have the same version

### On production database

#### 1.创建walminer的extension
```sql
create extension walminer;
```

#### 2. Build the dictionary
```sql
select walminer_build_dictionary('/opt/proc/store_dictionary');
-- Note: the parameter can be file name or directory name.
```

### On testing database

#### 1. 创建walminer的extension
```sql
create extension walminer;
```

#### 2. Load database dictionary
```sql
select walminer_load_dictionary('/opt/test/store_dictionary');
-- Note: the parameter can be file name or directory name.
```
:bulb:	the parameter can be file name or directory name.
	
#### 3. Add WAL files
```sql
-- Add WAL files
select walminer_wal_add('/opt/test/wal');
-- Note: the parameter can be file name or directory name.
```

#### 4. remove xlog WAL files
```sql
-- Remove WAL files
select walminer_wal_remove('/opt/test/wal');
-- Note:the parameter can be file name or directory name.
```

#### 5. List xlog WAL files	
```sql
-- list WAL files
select walminer_wal_list();
-- Note:the parameter can be file name or directory name.
```

#### 6. Execute the parsing
```sql
select walminer_start(’START_TIMSTAMP’,’STOP_TIMESTAMP’,’START_XID’,’STOP_XID’)
```

* **START_TIMESTAMP**：Specify the start time condition of the records in the return results, the walminer will start parsing from this time value. If this value is NULL, the earlist records will be displayed from the WAL lists. If this time value is not included in the xlog lists, a.k.a all the records are ealier then this value, the NULL will be returned.
* **STOP_TIMESTAMP**：Specify the ending time condition of the records in the results, the walminer will stop parsing when the result is later than this time. If this parameter is NULL, then all the records after **START_TIMESTAMP** will parsed and displyed in the WAL logs.
* **START_XID**：Similiar with **START_TIMESTAMP**, specify the starting **XID** value
* **STOP_XID**：Similiar with **STOP_TIMESTAMP**，specify the ending **XID** value	

#### 7. Check the parsing result
```sql
select * from walminer_contents;
```

### 8.Stop the walminer
This function is used to free the memory and stop the WAL parsing. No parameters available.
```sql
select walminer_stop();
```

:warning: **NOTE**：For the security considerations, walminer_contents is a temporary table generated by walminer automatically, it is not visible when session disconnected and then re-connect, and it is also not visible to other sessions. 
     If you want to keep the paring result, you can use the below SQL to write the results into a regular table.
```sql	 
create xxx as select * from  walminer_contents;
```

# Limitations
1. Only DML statements will be parsed in this version, DDL statement not supported.
2. The DML statemes would **NOT** be parsed out when the below DDL related operations were executed:
   Deleting/Truncating table, table space modification and column type modification etcs.
3. The parsing result is depending on the latest database dictionary. For example, after user1 created table t1, the table owner was modified to user2, then all the parsing results related to table t1 will be marked with user2.
4. If WAL logs are missed in a time stage, the SQL statements executed in that time stage would **NOT** be parsed out.
5. The "ctid" attribute is the value of the change "at that time". If there are "ctid" changes due to vacuum or other operations, this value will be **inaccurate**. We need use this value to determine the corresponding undo tuples when the rows of data are duplicate, it does not mean that you can execute such undo statements directly.
6. If the DDL statement "drop" was executed, all related column value will be decoded as "encode('AD976BC56F',hex)" before this DDL execution.
7.8. It is not recommended to use walminer to parse the wal log generated by a large copy statement (insert a large number of data rows in the same transaction), which will result in inefficiency and memory usage during parsing.

# Contact me
Please contact me with lchch1990@sina.cn if you have any comments or find any bugs, thanks!

