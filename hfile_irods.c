/*  hfile_irods.c -- iRODS backend for low-level file streams.

    Copyright (C) 2013, 2015, 2016 Genome Research Ltd.

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

#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "hfile_internal.h"
#include "htslib/hts.h"  // for hts_verbose
#include "htslib/kstring.h"

#include <rodsClient.h>

#ifndef IRODS_VERSION_INTEGER
// This version macro was introduced in iRODS 4.1.0.  Plugins were introduced
// in 4.0.0, so we can distinguish the major version numbers of interest.
#ifdef PLUGIN_ERROR
#define IRODS_VERSION_INTEGER 4000000  // 4.0.x
#else
#define IRODS_VERSION_INTEGER 3000000  // 3.x
#endif
#endif

#if IRODS_VERSION_INTEGER >= 4000000 && IRODS_VERSION_INTEGER < 4001000
#error hfile_irods.c does not support this iRODS version
// 4.0.x is unsupported as rcDataObjRead() mallocs its own buffer instead
// of using the one supplied (https://github.com/irods/irods/issues/2503).
#endif

// PRIORITY ensures that if multiple hfile_irods plugins built against
// different iRODS versions are installed, the plain "irods" scheme will
// be claimed by the most modern one.
#if IRODS_VERSION_INTEGER >= 4000000
#define PRIORITY (10 * IRODS_VERSION_MAJOR) + IRODS_VERSION_MINOR
#else
#define PRIORITY 30
// For iRODS 3.x, there is no init_client_api_table() to call and clientLogin()
// has fewer parameters.  Define wrappers so the 4.x-style code below compiles.
static void init_client_api_table() { }
#define clientLogin(conn, x, y) (clientLogin((conn)))
#endif


typedef struct {
    hFILE base;
    int descriptor;
} hFILE_irods;

static int status_errno(int status)
{
    switch (status) {
    case SYS_INVALID_INPUT_PARAM: return EINVAL;
    case SYS_NO_API_PRIV: return EACCES;
    case SYS_MALLOC_ERR: return ENOMEM;
    case SYS_OUT_OF_FILE_DESC: return ENFILE;
    case SYS_BAD_FILE_DESCRIPTOR: return EBADF;
#ifdef PLUGIN_ERROR
    case PLUGIN_ERROR: return ENOEXEC;
#endif
#ifdef PLUGIN_ERROR_MISSING_SHARED_OBJECT
    case PLUGIN_ERROR_MISSING_SHARED_OBJECT: return ENOEXEC;
#endif
    case USER_RODS_HOST_EMPTY: return EHOSTUNREACH;
    case CAT_NO_ACCESS_PERMISSION: return EACCES;
    case CAT_INVALID_AUTHENTICATION: return EACCES;
    case CAT_INVALID_USER: return EACCES;
    case CAT_NO_ROWS_FOUND: return ENOENT;
    case CATALOG_ALREADY_HAS_ITEM_BY_THAT_NAME: return EEXIST;
    default: return EIO;
    }
}

static void set_errno(int status)
{
    int err = abs(status) % 1000;
    errno = err? err : status_errno(status);
}

static struct {
    rcComm_t *conn;
    rodsEnv env;
} irods = { NULL };

static void irods_exit()
{
    if (irods.conn) { (void) rcDisconnect(irods.conn); }
    irods.conn = NULL;
}

static int irods_init()
{
    struct sigaction pipehandler;
    rErrMsg_t err;
    int ret, pipehandler_ret;

    if (hts_verbose >= 5) {
        fputs("[M::hfile_irods.init] version " PLUGINS_VERSION
              " built against " RODS_REL_VERSION "(" RODS_API_VERSION ")\n",
              stderr);
        rodsLogLevel(hts_verbose);
    }

    ret = getRodsEnv(&irods.env);
    if (ret < 0) goto error;

    // Set iRODS User-Agent, if our caller hasn't already done so.
    (void) setenv(SP_OPTION, "htslib-irods/" PLUGINS_VERSION, 0);

    // Prior to iRODS 4.1, rcConnect() (even if it fails) installs its own
    // SIGPIPE handler, which just prints a message and otherwise ignores the
    // signal.  Most actual SIGPIPEs encountered will pertain to e.g. stdout
    // rather than iRODS's connection, so we save and restore the existing
    // state (by default, termination; or as already set by our caller).
    pipehandler_ret = sigaction(SIGPIPE, NULL, &pipehandler);

    init_client_api_table();
    irods.conn = rcConnect(irods.env.rodsHost, irods.env.rodsPort,
                           irods.env.rodsUserName, irods.env.rodsZone,
                           NO_RECONN, &err);
    if (pipehandler_ret == 0) sigaction(SIGPIPE, &pipehandler, NULL);
    if (irods.conn == NULL) { ret = err.status; goto error; }

    if (hts_verbose >= 5) {
        fprintf(stderr, "[M::hfile_irods.init] connected to %s(%s)",
                irods.conn->svrVersion->relVersion,
                irods.conn->svrVersion->apiVersion);
        if (strncmp(irods.env.rodsHost, "", 1) != 0 && irods.env.rodsPort)
            fprintf(stderr, " at %s:%d\n",
                    irods.env.rodsHost, irods.env.rodsPort);
        else
            fprintf(stderr, "\n");
    }

    if (strcmp(irods.env.rodsUserName, PUBLIC_USER_NAME) != 0) {
        ret = clientLogin(irods.conn, NULL, NULL);
        if (ret != 0) goto error;
    }

    // Register irods_exit() here rather than via hFILE_plugin::destroy
    // so that it is invoked while iRODS is still up, i.e., before any
    // atexit()-functions or C++ static object destructors invoked due
    // to iRODS initialisation during the iRODS calls above.
    (void) atexit(irods_exit);

    return 0;

error:
    if (irods.conn) { (void) rcDisconnect(irods.conn); }
    irods.conn = NULL;
    set_errno(ret);
    return -1;
}

static ssize_t irods_read(hFILE *fpv, void *buffer, size_t nbytes)
{
    hFILE_irods *fp = (hFILE_irods *) fpv;
    openedDataObjInp_t args;
    bytesBuf_t buf;
    int ret;

    memset(&args, 0, sizeof args);
    args.l1descInx = fp->descriptor;
    args.len = nbytes;

#if IRODS_VERSION_INTEGER >= 4001000
    // Work around iRODS bug: these versions write one extra byte regardless of
    // the supplied buffer length (https://github.com/irods/irods/issues/3255).
    args.len--;
#endif

    buf.buf = buffer;
    buf.len = nbytes;

    ret = rcDataObjRead(irods.conn, &args, &buf);
    if (ret < 0) set_errno(ret);
    return ret;
}

static ssize_t irods_write(hFILE *fpv, const void *buffer, size_t nbytes)
{
    hFILE_irods *fp = (hFILE_irods *) fpv;
    openedDataObjInp_t args;
    bytesBuf_t buf;
    int ret;

    memset(&args, 0, sizeof args);
    args.l1descInx = fp->descriptor;
    args.len = nbytes;

    buf.buf = (void *) buffer; // ...the iRODS API is not const-correct here
    buf.len = nbytes;

    ret = rcDataObjWrite(irods.conn, &args, &buf);
    if (ret < 0) set_errno(ret);
    return ret;
}

static off_t irods_seek(hFILE *fpv, off_t offset, int whence)
{
    hFILE_irods *fp = (hFILE_irods *) fpv;
    openedDataObjInp_t args;
    fileLseekOut_t *out = NULL;
    int ret;

    memset(&args, 0, sizeof args);
    args.l1descInx = fp->descriptor;
    args.offset = offset;
    args.whence = whence;

    ret = rcDataObjLseek(irods.conn, &args, &out);

    if (out) { offset = out->offset; free(out); }
    else offset = -1;
    if (ret < 0) { set_errno(ret); return -1; }
    return offset;
}

static int irods_close(hFILE *fpv)
{
    hFILE_irods *fp = (hFILE_irods *) fpv;
    openedDataObjInp_t args;
    int ret;

    memset(&args, 0, sizeof args);
    args.l1descInx = fp->descriptor;

    ret = rcDataObjClose(irods.conn, &args);
    if (ret < 0) set_errno(ret);
    return ret;
}

static const struct hFILE_backend irods_backend =
{
    irods_read, irods_write, irods_seek, NULL, irods_close
};

static hFILE *hopen_irods(const char *filename, const char *mode)
{
    hFILE_irods *fp;
    rodsPath_t path;
    dataObjInp_t args;
    int ret;

    // Initialise the iRODS connection if this is the first use.
    if (irods.conn == NULL) { if (irods_init() < 0) return NULL; }

    // Check that the URL scheme is "irods" or "irods[0-9.]*".
    if (strncmp(filename, "irods", 5) == 0
        && filename[5 + strspn(&filename[5], "0123456789.")] == ':')
        filename = strchr(filename, ':') + 1;
    else { errno = EINVAL; return NULL; }

    fp = (hFILE_irods *) hfile_init(sizeof (hFILE_irods), mode, 0);
    if (fp == NULL) return NULL;

    strncpy(path.inPath, filename, MAX_NAME_LEN-1);
    path.inPath[MAX_NAME_LEN-1] = '\0';

    ret = parseRodsPath(&path, &irods.env);
    if (ret < 0) goto error;

    memset(&args, 0, sizeof args);
    strcpy(args.objPath, path.outPath);
    args.openFlags = hfile_oflags(mode);
    args.oprType = (args.openFlags & O_RDONLY)? GET_OPR : PUT_OPR;
    if (args.openFlags & O_CREAT) {
        args.createMode = 0666;
        addKeyVal(&args.condInput, DEST_RESC_NAME_KW,irods.env.rodsDefResource);
    }

    ret = rcDataObjOpen(irods.conn, &args);
    if (ret < 0) goto error;
    fp->descriptor = ret;

    fp->base.backend = &irods_backend;
    return &fp->base;

error:
    hfile_destroy((hFILE *) fp);
    set_errno(ret);
    return NULL;
}

int hfile_plugin_init(struct hFILE_plugin *self)
{
    static const struct hFILE_scheme_handler handler =
        { hopen_irods, hfile_always_remote, "iRODS", PRIORITY };

    self->name = "iRODS";
    hfile_add_scheme_handler("irods", &handler);
    // At present RODS_REL_VERSION looks like "rodsX.Y[.Z]".
    hfile_add_scheme_handler("i"RODS_REL_VERSION, &handler);
    return 0;
}
