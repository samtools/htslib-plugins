# Makefile for HTSlib plugins.
#
#    Copyright (C) 2016-2018 Genome Research Ltd.
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

srcdir ?= .

CC       = gcc
CPPFLAGS =
CFLAGS   = -g -Wall -O2
LDFLAGS  =
LIBS     =

prefix      = /usr/local
exec_prefix = $(prefix)
libexecdir  = $(exec_prefix)/libexec
plugindir   = $(libexecdir)/htslib

INSTALL         = install -p
INSTALL_DIR     = mkdir -p -m 755
INSTALL_PROGRAM = $(INSTALL)

.PHONY: all clean install plugins tags
all: plugins

# By default, plugins are compiled against an already-installed HTSlib.
# To compile against an HTSlib development tree, uncomment and adjust
# $(HTSDIR) (or use 'make HTSDIR=...') to point to the top-level directory
# of your HTSlib source tree.
#HTSDIR = ../htslib

# Version number for plugins is the Git description of the working tree,
# or the date of compilation if built outwith a Git repository.
VERSION := $(shell $(if $(wildcard $(srcdir)/.git),cd $(srcdir) && git describe --always --dirty,date +%Y%m%d))
VERSION_CPPFLAGS = -DPLUGINS_VERSION=\"$(VERSION)\"

ALL_CPPFLAGS = $(CPPFLAGS)
ALL_CFLAGS   = $(CFLAGS)
ALL_LDFLAGS  = $(LDFLAGS)
ALL_LIBS     = $(LIBS)

%.o: %.c
	$(CC) $(ALL_CFLAGS) $(ALL_CPPFLAGS) -c -o $@ $<

PLATFORM := $(shell uname -s)
ifeq "$(PLATFORM)" "Darwin"
PLUGIN_EXT = .bundle
CRYPTO_CFLAGS = -DHAVE_COMMONCRYPTO
CRYPTO_LIBS =

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
CRYPTO_CFLAGS = -DHAVE_OPENSSL
CRYPTO_LIBS = -lcrypto
ALL_CFLAGS += -fpic

%.so: %.o
	$(CC) -shared -Wl,-E -pthread $(ALL_LDFLAGS) -o $@ $^ $(ALL_LIBS)
endif

ifdef HTSDIR
ALL_CPPFLAGS += -I$(HTSDIR)
endif

# Override $(PLUGINS) to build or install a different subset of the available
# plugins.  In particular, hfile_irods_wrapper is not in the default list as
# it is not needed with recent HTSlib (though it does no particular harm).
PLUGINS = hfile_cip$(PLUGIN_EXT) hfile_irods$(PLUGIN_EXT) hfile_mmap$(PLUGIN_EXT)

plugins: $(PLUGINS)

install: $(PLUGINS)
	$(INSTALL_DIR) $(DESTDIR)$(plugindir)
	$(INSTALL_PROGRAM) $(PLUGINS) $(DESTDIR)$(plugindir)

clean:
	-rm -f *.o *$(PLUGIN_EXT)

tags TAGS:
	ctags -f TAGS *.[ch]


#### EGA-style encrypted (.cip) files ####

hfile_cip.o: ALL_CFLAGS += $(CRYPTO_CFLAGS)
hfile_cip$(PLUGIN_EXT): ALL_LIBS += $(CRYPTO_LIBS)

hfile_cip$(PLUGIN_EXT): hfile_cip.o
hfile_cip.o: hfile_cip.c hfile_internal.h


#### Memory-mapped local files ####

hfile_mmap$(PLUGIN_EXT): hfile_mmap.o
hfile_mmap.o: hfile_mmap.c hfile_internal.h


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
ifneq "$(wildcard $(IRODS_HOME)/lib/development_libraries/lib*)" ""
IRODS_LDFLAGS += -L$(IRODS_HOME)/lib/development_libraries
else ifneq "$(wildcard $(IRODS_HOME)/lib/core/obj/lib*)" ""
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

else ifneq "$(findstring rods4.2.,$(IRODS_VERSION))" ""

# iRODS 4.2.x has its own plugins (eg libtcp_client.so) and a complete set 
# of shared libraries. However, iRODS uses dynamic_cast to cast some 
# classes defined in plugins and this does not work with some c++ ABIs
# Therefore we use RTLD_GLOBAL for hfile_irods until iRODS fixes the 
# underlying issue (https://github.com/irods/irods/issues/3752)
IRODS_CPPFLAGS += -Dhfile_plugin_init=hfile_plugin_init_hfile_irods

IRODS_LIBS = -lirods_client -lirods_plugin_dependencies -lirods_common

else

IRODS_LIBS = -lirods_client -lirods_plugin_dependencies -lirods_common

endif


hfile_irods.o: ALL_CPPFLAGS += $(VERSION_CPPFLAGS) $(IRODS_CPPFLAGS)
hfile_irods$(PLUGIN_EXT): ALL_LDFLAGS += $(IRODS_LDFLAGS)
hfile_irods$(PLUGIN_EXT): ALL_LIBS += $(IRODS_LIBS)

hfile_irods$(PLUGIN_EXT): hfile_irods.o
hfile_irods.o: hfile_irods.c hfile_internal.h


#### iRODS 4.1.x wrapper (for HTSlib prior to 1.3.2) ####

hfile_irods_wrapper$(PLUGIN_EXT): ALL_LDFLAGS += -Wl,-rpath,'$$ORIGIN'
hfile_irods_wrapper$(PLUGIN_EXT): ALL_LIBS += -ldl

hfile_irods_wrapper$(PLUGIN_EXT): hfile_irods_wrapper.o
hfile_irods_wrapper.o: hfile_irods_wrapper.c hfile_internal.h
