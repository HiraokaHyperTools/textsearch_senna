MODULE_big = textsearch_senna
DATA_built = textsearch_senna.sql
DATA = uninstall_textsearch_senna.sql
OBJS = textsearch_senna.o pgut/pgut-be.o
REGRESS = init textsearch_senna update
SHLIB_LINK = -lsenna
EXTRA_CLEAN = textsearch_senna.sql.in

ifndef USE_PGXS
top_builddir = ../..
makefile_global = $(top_builddir)/src/Makefile.global
ifeq "$(wildcard $(makefile_global))" ""
USE_PGXS = 1	# use pgxs if not in contrib directory
endif
endif

ifdef USE_PGXS
PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)
else
subdir = contrib/$(MODULE_big)
include $(makefile_global)
include $(top_srcdir)/contrib/contrib-global.mk
endif

# remove dependency to libxml2 and libxslt
LIBS := $(filter-out -lxml2, $(LIBS))
LIBS := $(filter-out -lxslt, $(LIBS))

textsearch_senna.o: textsearch_senna.sql.in

ifndef MAJORVERSION
MAJORVERSION := $(basename $(VERSION))
endif

textsearch_senna.sql.in:
	cp textsearch_senna-$(MAJORVERSION).sql.in textsearch_senna.sql.in
