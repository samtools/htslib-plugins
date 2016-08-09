/*  hfile_irods_wrapper.c -- RTLD_GLOBAL wrapper for iRODS plugin.

    Copyright (C) 2016 Genome Research Ltd.

    Author: John Marshall <jm18@sanger.ac.uk>

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
DEALINGS IN THE SOFTWARE.  */

#include <stdio.h>
#include <dlfcn.h>

#include "hfile_internal.h"
#include "htslib/hts.h"  // for hts_verbose

static void *lib = NULL;
static void (*lib_destroy)(void) = NULL;

static void wrapper_exit()
{
    if (lib_destroy) lib_destroy();
    lib_destroy = NULL;

    if (lib) dlclose(lib);
    lib = NULL;
}

int hfile_plugin_init(struct hFILE_plugin *self)
{
    lib = dlopen("hfile_irods.so", RTLD_NOW | RTLD_GLOBAL);
    if (lib == NULL) goto error;

    int (*init)(struct hFILE_plugin *) = dlsym(lib, "hfile_plugin_init_hfile_irods");
    if (init == NULL) goto error;

    init(self);
    self->name = "iRODS wrapper";
    lib_destroy = self->destroy;
    self->destroy = wrapper_exit;
    return 0;

error:
    if (hts_verbose >= 4)
        fprintf(stderr, "[W::%s] can't load plugin \"%s\": %s\n",
                "hfile_irods_wrapper.init", "./hfile_irods.so", dlerror());
    if (lib) dlclose(lib);
    return -1;
}
