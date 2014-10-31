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

enum _deleg_type {
        NONE,
        READ_DELEGATION,
        READ_WRITE_DELEGATION,
        RECALL_DELEGATION
};
typedef enum _deleg_type deleg_type;

enum upcall_event_type_t {
        CACHE_INVALIDATION,
        READ_DELEG,
        READ_WRITE_DELEG
};
typedef enum upcall_event_type_t upcall_event_type;

struct _upcall_client_entry_t {
        struct list_head client_list;
        /* while storing client_uid, strdup and later free while freeing this struct */
        char *client_uid;
        rpc_transport_t *trans;
        rpcsvc_t *rpc;
        time_t timestamp; /* time last accessed */
        deleg_type deleg;
        /*
         *  Maybe maintain a deleg_list as well for fast traversal or lookup
         *  of deleg states/clients list
         */
};
typedef struct _upcall_client_entry_t upcall_client_entry;
        
struct _upcall_entry_t {
        struct list_head list;
        uuid_t gfid;
        upcall_client_entry client; /* list of clients */
};
typedef struct _upcall_entry_t upcall_entry;

struct _notify_event_data {
        uuid_t gfid;
        upcall_client_entry *client_entry;
        upcall_event_type event_type;
        /* any extra data needed, like inode flags
         *  to be invalidated incase of cache invalidation,
         *  may be fd for delegations */
        void *extra;
};
typedef struct _notify_event_data notify_event_data;

/* For now lets maintain a linked list of upcall entries.
 * But for faster lookup we should be switching any of the 
 * tree data structures
 */
upcall_entry upcall_entry_list;

upcall_entry * get_upcall_entry (uuid_t gfid);
int upcall_deleg_check (call_frame_t *frame, client_t *client, uuid_t gfid, void *extra);
int upcall_cache_invalidate (call_frame_t *frame, client_t *client, uuid_t gfid, void *extra);
//int add_upcall_entry (upcall_entry * up_entry);
//int get_client_entry (client_t *client, uuid_t gfid, upcall_client_entry * up_client_entry);
//int add_client_entry (upcall_entry * up_entry, upcall_client_entry * up_client_entry);

#endif /* __UPCALLS_INFRA_H__ */
