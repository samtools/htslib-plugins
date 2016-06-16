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

%.o: %.c
	$(CC) $(CFLAGS) $(CPPFLAGS) $(EXTRA_CFLAGS) -c -o $@ $<

PLATFORM := $(shell uname -s)
ifeq "$(PLATFORM)" "Darwin"
PLUGIN_EXT = .bundle
EXTRA_CFLAGS =

%.bundle: %.o
	$(CC) -bundle -Wl,-undefined,dynamic_lookup $(LDFLAGS) -o $@ $< $(LIBS)

else ifeq "$(findstring CYGWIN,$(PLATFORM))" "CYGWIN"
PLUGIN_EXT = .cygdll
EXTRA_CFLAGS =

%.cygdll: %.o
	$(CC) -shared $(LDFLAGS) -o $@ $< libhts.dll.a $(LIBS)

ifdef HTSDIR
LDFLAGS += -L$(HTSDIR)
endif

else
PLUGIN_EXT = .so
EXTRA_CFLAGS = -fpic

%.so: %.o
	$(CC) -shared -Wl,-E -pthread $(LDFLAGS) -o $@ $< $(LIBS)
endif

ifdef HTSDIR
CPPFLAGS += -I$(HTSDIR)
endif

plugins: hfile_irods$(PLUGIN_EXT)

clean:
	-rm -f *.o *$(PLUGIN_EXT)

tags TAGS:
	ctags -f TAGS *.[ch]


#### iRODS http://irods.org/ ####

# By default, compile iRODS plugins against a system-installed iRODS.
# To compile against a run-in-place installation, set IRODS_HOME to the
# base directory of such an installation.
IRODS_HOME ?= /usr/include/irods

ifneq "$(wildcard $(IRODS_HOME)/lib/core/include/*)" ""
IRODS_CPPFLAGS = \
	-I$(IRODS_HOME)/lib/api/include \
	-I$(IRODS_HOME)/lib/core/include \
	-I$(IRODS_HOME)/lib/md5/include \
	-I$(IRODS_HOME)/lib/sha1/include \
	-I$(IRODS_HOME)/server/core/include \
	-I$(IRODS_HOME)/server/drivers/include \
	-I$(IRODS_HOME)/server/icat/include
else
IRODS_CPPFLAGS = $(error no iRODS headers found (set IRODS_HOME))
endif

IRODS_LDFLAGS = -L$(IRODS_HOME)/lib/core/obj
IRODS_LIBS = -lRodsAPIs -lgssapi_krb5

hfile_irods.o: CPPFLAGS += $(IRODS_CPPFLAGS)
hfile_irods$(PLUGIN_EXT): LDFLAGS += $(IRODS_LDFLAGS)
hfile_irods$(PLUGIN_EXT): LIBS += $(IRODS_LIBS)

hfile_irods.o: hfile_irods.c hfile_internal.h
