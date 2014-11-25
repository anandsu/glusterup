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

#define LEASE_PERIOD 60

/* Defines for the flags in callback_arg, keep up to date with CXIUP_xxx */
#define UP_NLINK        0x00000001   /* update nlink */
#define UP_MODE         0x00000002   /* update mode and ctime */
#define UP_OWN          0x00000004   /* update mode,uid,gid and ctime */
#define UP_SIZE         0x00000008   /* update fsize */
#define UP_SIZE_BIG     0x00000010   /* update fsize if bigger */
#define UP_TIMES        0x00000020   /* update all times */
#define UP_ATIME        0x00000040   /* update atime only */
#define UP_PERM         0x00000080   /* update fields needed for permission checking*/
#define UP_RENAME       0x00000100   /* this is a rename op */
#define UP_DESTROY_FLAG 0x00000200   /* clear destroyIfDelInode flag */
#define UP_GANESHA      0x00000400   /* this is a ganesha op */

struct _upcalls_private_t {
	int client_id;
};
typedef struct _upcalls_private_t upcalls_private_t;

enum _deleg_type {
        NONE,
        READ_DELEG,
        READ_WRITE_DELEG,
        RECALL_DELEG_IN_PROGRESS
};
typedef enum _deleg_type deleg_type;

enum upcall_event_type_t {
        CACHE_INVALIDATION,
        RECALL_READ_DELEG,
        RECALL_READ_WRITE_DELEG
};
typedef enum upcall_event_type_t upcall_event_type;

struct _upcall_client_entry_t {
        struct list_head client_list;
        /* while storing client_uid, strdup and later free while freeing this struct */
        char *client_uid;
        rpc_transport_t *trans;
        rpcsvc_t *rpc;
        time_t access_time; /* time last accessed */
        deleg_type deleg;
        /* maybe club this with access_time */
        time_t recall_time; /* time recall has been sent */
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
        int    deleg_cnt;
        pthread_mutex_t u_client_mutex; /* mutex for clients list of this upcall entry */
};
typedef struct _upcall_entry_t upcall_entry;

pthread_mutex_t u_mutex; /* mutex for upcall entries list */

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
upcall_client_entry* get_upcall_client_entry (call_frame_t *frame, uuid_t gfid,
                                              client_t* client, upcall_entry **up_entry);
int upcall_deleg_check (call_frame_t *frame, client_t *client, uuid_t gfid,
                    gf_boolean_t is_write, upcall_entry **up_entry);
int remove_deleg (call_frame_t *frame, client_t *client, uuid_t gfid);
int add_deleg (call_frame_t *frame, client_t *client, uuid_t gfid, gf_boolean_t is_write);
int upcall_cache_invalidate (call_frame_t *frame, client_t *client, uuid_t gfid, void *extra);
//int add_upcall_entry (upcall_entry * up_entry);
//int get_client_entry (client_t *client, uuid_t gfid, upcall_client_entry * up_client_entry);
//int add_client_entry (upcall_entry * up_entry, upcall_client_entry * up_client_entry);

#endif /* __UPCALLS_INFRA_H__ */
