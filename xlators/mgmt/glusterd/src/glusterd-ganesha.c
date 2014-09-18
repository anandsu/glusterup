#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "common-utils.h"
#include "cli1-xdr.h"
#include "xdr-generic.h"
#include "glusterd.h"
#include "glusterd-op-sm.h"
#include "glusterd-store.h"
#include "glusterd-utils.h"
#include "glusterd-volgen.h"
#include "run.h"
#include "syscall.h"
#include "byte-order.h"
#include "compat-errno.h"

#include <sys/wait.h>
#include <dlfcn.h>


int glusterd_handle_ganesha_op(dict_t *dict, char **op_errstr)
{
        GF_ASSERT(dict);
        GF_ASSERT(op_errstr);
       gf_asprintf(op_errstr, "ganesha volume failed");
       return -1;
}


