# contrib/walminer/Makefile

MODULE_big	= walminer
OBJS		= pg_logminer.o logminer.o organizsql.o xlogreader_logminer.o datadictionary.o xlogminer_contents.o

EXTENSION = walminer
DATA = walminer--1.0.sql

ifdef USE_PGXS
PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)
else
subdir = contrib/walminer
top_builddir = ../..
include $(top_builddir)/src/Makefile.global
include $(top_srcdir)/contrib/contrib-global.mk
endif
