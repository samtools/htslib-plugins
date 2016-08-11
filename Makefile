# Makefile for HTSlib plugins.
#
#    Copyright (C) 2016 Genome Research Ltd.
#
#    Author: John Marshall <jm18@sanger.ac.uk>
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
# THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
# FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
# DEALINGS IN THE SOFTWARE.

# This Makefile uses GNU Make-specific constructs, including conditionals
# and target-specific variables.  You will need to use GNU Make.

CC       = gcc
CPPFLAGS =
CFLAGS   = -g -Wall -O2
LDFLAGS  =
LIBS     =

.PHONY: all clean plugins tags
all: plugins

# By default, plugins are compiled against an already-installed HTSlib.
# To compile against an HTSlib development tree, uncomment and adjust
# $(HTSDIR) (or use 'make HTSDIR=...') to point to the top-level directory
# of your HTSlib source tree.
#HTSDIR = ../htslib

ALL_CPPFLAGS = $(CPPFLAGS)
ALL_CFLAGS   = $(CFLAGS)
ALL_LDFLAGS  = $(LDFLAGS)
ALL_LIBS     = $(LIBS)

%.o: %.c
	$(CC) $(ALL_CFLAGS) $(ALL_CPPFLAGS) -c -o $@ $<

PLATFORM := $(shell uname -s)
ifeq "$(PLATFORM)" "Darwin"
PLUGIN_EXT = .bundle

%.bundle: %.o
	$(CC) -bundle -Wl,-undefined,dynamic_lookup $(ALL_LDFLAGS) -o $@ $^ $(ALL_LIBS)

else ifeq "$(findstring CYGWIN,$(PLATFORM))" "CYGWIN"
PLUGIN_EXT = .cygdll

%.cygdll: %.o
	$(CC) -shared $(ALL_LDFLAGS) -o $@ $^ libhts.dll.a $(ALL_LIBS)

ifdef HTSDIR
ALL_LDFLAGS += -L$(HTSDIR)
endif

else
PLUGIN_EXT = .so
ALL_CFLAGS += -fpic

%.so: %.o
	$(CC) -shared -Wl,-E -pthread $(ALL_LDFLAGS) -o $@ $^ $(ALL_LIBS)
endif

ifdef HTSDIR
ALL_CPPFLAGS += -I$(HTSDIR)
endif

plugins: hfile_irods$(PLUGIN_EXT)

clean:
	-rm -f *.o *$(PLUGIN_EXT)

tags TAGS:
	ctags -f TAGS *.[ch]


#### iRODS http://irods.org/ ####

# By default, compile iRODS plugins against a system-installed iRODS.
# To compile against a run-in-place installation, set IRODS_HOME to the
# base directory of such an installation.  If there is an additional
# separate build tree, set IRODS_BUILD to its base directory.
IRODS_HOME ?= /usr

IRODS_CPPFLAGS =
IRODS_LDFLAGS  =

ifneq "$(wildcard $(IRODS_HOME)/include/irods/rcConnect.h)" ""
IRODS_CPPFLAGS += -I$(IRODS_HOME)/include/irods
IRODS_VERFILE = $(IRODS_HOME)/include/irods/rodsVersion.h

else ifneq "$(wildcard $(IRODS_HOME)/rcConnect.h)" ""
IRODS_CPPFLAGS += -I$(IRODS_HOME)
IRODS_VERFILE = $(IRODS_HOME)/rodsVersion.h

else ifneq "$(wildcard $(IRODS_HOME)/lib/core/include/*)" ""
IRODS_CPPFLAGS += \
	-I$(IRODS_HOME)/lib/api/include \
	-I$(IRODS_HOME)/lib/core/include \
	-I$(IRODS_HOME)/lib/md5/include \
	-I$(IRODS_HOME)/lib/sha1/include \
	-I$(IRODS_HOME)/server/core/include \
	-I$(IRODS_HOME)/server/drivers/include \
	-I$(IRODS_HOME)/server/icat/include \
	-I$(IRODS_HOME)/server/re/include
IRODS_VERFILE = $(IRODS_HOME)/lib/core/include/rodsVersion.h

else
IRODS_CPPFLAGS = $(error no iRODS headers found (set IRODS_HOME))
IRODS_VERFILE  = /dev/null
endif

ifneq "$(wildcard $(IRODS_HOME)/lib/irods/externals/lib*)" ""
IRODS_LDFLAGS += -L$(IRODS_HOME)/lib/irods/externals
endif
ifneq "$(wildcard $(IRODS_HOME)/lib/core/obj/lib*)" ""
IRODS_LDFLAGS += -L$(IRODS_HOME)/lib/core/obj
else ifneq "$(wildcard $(IRODS_HOME)/lib)" ""
ifneq "$(IRODS_HOME)" "/usr"
IRODS_LDFLAGS += -L$(IRODS_HOME)/lib
endif
endif

ifdef IRODS_BUILD
IRODS_CPPFLAGS += -I$(IRODS_BUILD)/lib/core/include
IRODS_VERFILE = $(IRODS_BUILD)/lib/core/include/rodsVersion.h
IRODS_LDFLAGS += -L$(IRODS_BUILD)
endif


IRODS_VERSION := $(shell grep RODS_REL_VERSION $(IRODS_VERFILE))
ifneq "$(findstring rods3.,$(IRODS_VERSION))" ""

IRODS_LIBS = -lRodsAPIs -lgssapi_krb5

else ifneq "$(findstring rods4.1.,$(IRODS_VERSION))" ""

# iRODS 4.1.x has its own plugins (eg libtcp.so) but no pervasive shared
# libraries.  So its plugins need to be able to see iRODS code linked into
# the hfile_irods plugin, so we must be a RTLD_GLOBAL-loaded plugin.
IRODS_CPPFLAGS += -Dhfile_plugin_init=hfile_plugin_init_hfile_irods

IRODS_LIBS = -lirods_client -lirods_client_api_table -lirods_client_plugins -lirods_client_api -lirods_client_core \
	-ljansson -lboost_program_options -lboost_filesystem -lboost_chrono -lboost_regex -lboost_thread -lboost_system -lssl -lcrypto -lrt -lstdc++

else

IRODS_LIBS = -lirods_client_api_table -lirods_client_core -lirods_plugin_dependencies -lirods_common

endif


hfile_irods.o: ALL_CPPFLAGS += $(IRODS_CPPFLAGS)
hfile_irods$(PLUGIN_EXT): ALL_LDFLAGS += $(IRODS_LDFLAGS)
hfile_irods$(PLUGIN_EXT): ALL_LIBS += $(IRODS_LIBS)

hfile_irods$(PLUGIN_EXT): hfile_irods.o
hfile_irods.o: hfile_irods.c hfile_internal.h


#### iRODS 4.1.x wrapper (for HTSlib prior to 1.4) ####

hfile_irods_wrapper$(PLUGIN_EXT): ALL_LDFLAGS += -Wl,-rpath,'$$ORIGIN'
hfile_irods_wrapper$(PLUGIN_EXT): ALL_LIBS += -ldl

hfile_irods_wrapper$(PLUGIN_EXT): hfile_irods_wrapper.o
hfile_irods_wrapper.o: hfile_irods_wrapper.c hfile_internal.h
