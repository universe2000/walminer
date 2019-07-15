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

ifeq ($(MAJORVERSION), 12)
	PG_CPPFLAGS = -DPG_VERSION_12
else ifeq ($(MAJORVERSION), 11)
	PG_CPPFLAGS = -DPG_VERSION_11
else ifeq ($(MAJORVERSION), 10)
	PG_CPPFLAGS = -DPG_VERSION_10
else ifeq ($(MAJORVERSION), 9.6)
	PG_CPPFLAGS = -DPG_VERSION_9_6
else ifeq ($(MAJORVERSION), 9.5)
	PG_CPPFLAGS = -DPG_VERSION_9_5
else
	$(error unsupport PG version)
endif

include $(top_srcdir)/contrib/contrib-global.mk
endif
