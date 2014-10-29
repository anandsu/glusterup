/*
   Copyright (c) 2006-2014 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/
#ifndef __UPCALLS_INFRA_H__
#define __UPCALLS_INFRA_H__

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "compat-errno.h"
#include "stack.h"
#include "call-stub.h"
#include "upcalls-mem-types.h"
#include "client_t.h"
#include "rpcsvc.h"

#include "lkowner.h"

struct _upcalls_private_t {
	int client_id;
};
typedef struct _upcalls_private_t upcalls_private_t;

struct _upcall_entry_t {
        struct list_head list;
        struct list_head xprt_list;
        rpc_transport_t *trans;
        rpcsvc_t *rpc;
        uuid_t gfid;
        int entry_type; /* cache_inode_invalidation or READ or RW delegation */
        int flags; /* for cache_inode_invalidation */
        time_t timestamp;
        // may need fd as well for delegations
};

typedef struct _upcall_entry_t upcall_entry;

/* For now lets maintain a linked list of upcall entries.
 * But for faster lookup we should be switching any of the 
 * tree data structures
 */
upcall_entry upcall_entry_list;

#endif /* __UPCALLS_INFRA_H__ */
