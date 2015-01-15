/*
   Copyright (c) 2006-2014 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/
#include <unistd.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "glusterfs.h"
#include "compat.h"
#include "xlator.h"
#include "inode.h"
#include "logging.h"
#include "common-utils.h"

#include "statedump.h"
#include "syncop.h"

#include "upcalls.h"
#include "upcalls-mem-types.h"
#include "glusterfs3-xdr.h"
#include "protocol-common.h"
#include "defaults.h"

#ifndef LLONG_MAX
#define LLONG_MAX LONG_LONG_MAX /* compat with old gcc */
#endif /* LLONG_MAX */

/*
 * Create an upcall entry given a gfid
 */
upcall_entry*
add_upcall_entry (uuid_t gfid)
{
        upcall_entry * up_entry = NULL;

        up_entry = GF_CALLOC (1, sizeof(*up_entry), gf_upcalls_mt_upcall_entry_t);
        if (!up_entry) {
                gf_log (THIS->name, GF_LOG_WARNING,
                         "upcall_entry Memory allocation failed");
                return NULL;
        }
        gf_log (THIS->name, GF_LOG_INFO, "In get_upcall_entry ");
        INIT_LIST_HEAD (&up_entry->list);
        uuid_copy(up_entry->gfid, gfid);
        INIT_LIST_HEAD(&up_entry->client.client_list);
        pthread_mutex_init (&up_entry->u_client_mutex, NULL); 
        up_entry->deleg_cnt = 0;

        /* Have to do it atomically or take lock */
        pthread_mutex_lock (&u_mutex);
        list_add_tail (&up_entry->list, &upcall_entry_list.list);
        pthread_mutex_unlock (&u_mutex);
        return up_entry;
}

/* For now upcall entries are maintained in a linked list.
 * Given a gfid, traverse through that list and lookup for an entry
 * with that gfid. If none found, create an entry with the gfid given.
 */
upcall_entry*
get_upcall_entry (uuid_t gfid)
{
        upcall_entry * up_entry = NULL;

        /* for now its a linked list. Need to be changed to a better data structure */
        pthread_mutex_lock (&u_mutex);
        list_for_each_entry (up_entry, &upcall_entry_list.list, list) {
                if (memcmp(up_entry->gfid, gfid, 16) == 0) {
                        pthread_mutex_unlock (&u_mutex);
                        /* found entry */
                        return up_entry;
                }
//                pthread_mutex_unlock (&u_mutex);
  //              pthread_mutex_lock (&u_mutex);
        }
        pthread_mutex_unlock (&u_mutex);

        /* entry not found. create one */
        up_entry = add_upcall_entry(gfid);
        return up_entry;
}

/*
 * Allocate and add a new client entry to the given upcall entry
 */
upcall_client_entry*
add_upcall_client_entry (call_frame_t *frame, uuid_t gfid, client_t* client,
                         upcall_entry **up_entry)
{
        upcall_client_entry * up_client_entry = NULL;

        up_client_entry = GF_CALLOC (1, sizeof(**up_entry),
                                     gf_upcalls_mt_upcall_entry_t);
        if (!up_client_entry) {
                gf_log (THIS->name, GF_LOG_WARNING,
                         "upcall_entry Memory allocation failed");
                return NULL;
        }
        INIT_LIST_HEAD (&up_client_entry->client_list);
        up_client_entry->client_uid = gf_strdup(client->client_uid);
        up_client_entry->rpc = (rpcsvc_t *)(client->rpc);
        up_client_entry->trans = (rpc_transport_t *)client->trans; 
        up_client_entry->access_time = time(NULL);

        /* Have to do it atomically or take lock */
        pthread_mutex_lock (&(*up_entry)->u_client_mutex);
        list_add_tail (&up_client_entry->client_list,
                       &(*up_entry)->client.client_list);
        pthread_mutex_unlock (&(*up_entry)->u_client_mutex);
        gf_log (THIS->name, GF_LOG_INFO, "upcall_entry client added - %s",
                up_client_entry->client_uid);
                
}

/*
 * Given gfid and client->uid, retrieve the corresponding upcall client entry.
 * If none found, create a new entry.
 */
upcall_client_entry*
get_upcall_client_entry (call_frame_t *frame, uuid_t gfid, client_t* client,
                         upcall_entry **up_entry)
{
        upcall_client_entry * up_client_entry = NULL;
        upcall_client_entry * up_client = NULL;
        gf_boolean_t found_client = _gf_false;

        if (!*up_entry)
                *up_entry = get_upcall_entry(gfid);
        if (!*up_entry) { /* cannot reach here */
                return NULL;
        }
        up_client = &(*up_entry)->client;

        pthread_mutex_lock (&(*up_entry)->u_client_mutex);
        list_for_each_entry (up_client_entry, &up_client->client_list, client_list) {
                if (up_client_entry->client_uid) {
                        if(strcmp(client->client_uid,
                                  up_client_entry->client_uid) == 0) {
                                /* found client entry. Update the access_time */
                                up_client_entry->access_time = time(NULL);
                                found_client = _gf_true;
                                gf_log (THIS->name, GF_LOG_INFO,
                                        "upcall_entry client found - %s",
                                        up_client_entry->client_uid);
                                break;
                        }
                }
//                pthread_mutex_unlock (&(*up_entry)->u_client_mutex);
  //              pthread_mutex_lock (&(*up_entry)->u_client_mutex);
        }
        pthread_mutex_unlock (&(*up_entry)->u_client_mutex);

        if (!found_client) { /* create one */
                up_client_entry = add_upcall_client_entry (frame, gfid, client,
                                                           up_entry);
        }
        ((upcall_local_t *)(frame->local))->client = up_client_entry;

        return up_client_entry;
}

/*
 * Given a gfid, client, first fetch upcall_entry based on gfid.
 * Later traverse through the client list of that upcall entry. If this client
 * is not present in the list, create one client entry with this client info.
 * And store that client entry in the frame->local.
 * Also verify if there is any client entry with delegations set. If yes
 * send notify call. - done
 *
 * If we add 'deleg_list', we can traverse through clients holding delegation
 * using that list. We may need to pass the access type to check for conflict with
 * delegation. - done
 *
 * Like in nfs-ganesha code, may be for each upcall entry, store a variable
 * which denotes if/what delegation exists for faster lookup (incase of delegations
 * not present). Also for each upcall/upcall_client entries, change deleg  state to
 * RECALL_DELEG_IN_PROGRESS, store recall_time so that if that deleg doesnt get
 * cleared, we can check if recall_time is > lease_time or 2*lease_time and
 * make a call to lock xlator to cleanup that lock entry. -done
 * Mostly this code/logic is already present in ganesha code. Incase if it cant
 * recall it revokes that delegation after certain time.
 * Cross check that. But even if ganesha handles and libgfapi client dies,
 * we need a way to cleanup these states. Hence store the recall_time.
 * Mostly as like normal locks, lock xlator might be cleaning up these lease
 * locks as well when the client dies. So we may not need a call to lock xlator
 * after deleg expiry. Can there be cases where in libgfapi
 * client still present but unable to reclaim/revoke delegs?
 *
 * Now, while granting delegation, like in ganesha_code we can maintain,
 * last_recall_time.
 * See the logic in "should_we_grant_deleg". If its already been recalled once
 * recently, then its ok not to grant delegations to this conflicting deleg request.
 *
 * routines to be checked "should_We_grant_deleg", "deleg_conflict" etc..
 *
 * In ganesha code, "deleg_conflict" is called for 
 *      nfsv3 - write, read, setattr ops
 *      nfsv4 - open, read, setattr, write, lock ops.
 * We may not handle open_op, since gluster_server 
 * process get deleg request only
 * via lock request but not via open request.
 *
 * So since deleg_conflict is first checked in ganesha layer, we need to check for
 * the conflicts only if the conflicting request is from another client.
 *
 * TODO: looks like, nlm locks do not check for delegation conflicts. Fix that
 * either in common layer or in FSAL_GLUSTER.
 *
 * Return : 0; if no delegations present. continue processing fop
 *        : 1; conflict delegation found; sent recall, send EDELAY error
 *        : -1' for any other processing error.
 */
int
upcall_deleg_check (call_frame_t *frame, client_t *client, uuid_t gfid,
                    gf_boolean_t is_write, upcall_entry **up_entry)
{
        upcall_client_entry * up_client_entry = NULL;
        upcall_client_entry * up_client = NULL;
        int found_client = 0;
        gf_boolean_t recall_sent = _gf_false;
        notify_event_data n_event_data;
        time_t t = time(NULL);

        *up_entry = get_upcall_entry(gfid);
        if (!*up_entry) { /* cannot reach here */
                return -1;
        }

        if (!((*up_entry)->deleg_cnt > 0 )) { /* No delegations present */
                 gf_log (THIS->name, GF_LOG_INFO, "No delegations ");
                goto out;
        }

        up_client = get_upcall_client_entry (frame, gfid, client, up_entry);
        if (!up_client) {
                goto err;
        }


        /* verify the current client entry delgations list
         * If the current_client has READ_DELEG set and if the request access
         * is write, send recall_request to recall the read_deleg. Also scan other
         * clients for other read_delegations based on deleg_cnt.
         * But incase if its write deleg, both read & write access are allowed
         * for this client.
         */

        /* If there is any request from same client, mostly, its the same
         *  application client sending these fops. For example, when we sent 
         *  recall_delegation request, it could issuing all the ops
         * to server WRITE, LOCK / OPEN etc...Not sure if we need to check for 
         * only RECALL_DELEG_IN_PROGRESS state. Also hopefully, other application
         * clients will be rejected by application itself and hence not
         * expecting those requests.
         */
        if (up_client->deleg == READ_WRITE_DELEG) {
                GF_ASSERT ((*up_entry)->deleg_cnt == 1);
                goto out;
        } else if (up_client->deleg == READ_DELEG) {
                if (is_write) {
                        /* usually same application client will not send for
                         * write access when taken read_delegation. But if its
                         * different application client, it should have got resolved
                         * at apllication level itself. Still for sanity check,
                         * we will try to recall this delegation
                         */
                        /* send notify */
                        uuid_copy(n_event_data.gfid, gfid);
                        n_event_data.client_entry = up_client;
                        n_event_data.event_type =  up_client->deleg;
                        n_event_data.extra = NULL; /* Need to send inode flags */
                        gf_log (THIS->name, GF_LOG_WARNING,
                                "upcall Delegation recall - %s",
                                up_client->client_uid);
                        up_client->deleg = RECALL_DELEG_IN_PROGRESS;
                        up_client->recall_time = time(NULL);
                        frame->this->notify (frame->this, GF_EVENT_UPCALL,
                                             &n_event_data);
                        recall_sent = _gf_true;
                        if ((*up_entry)->deleg_cnt == 1) {
                                 /* No More READ_DELEGS present */
                                recall_sent = _gf_true;
                                goto out; 
                        }
                }
        } else if (up_client->deleg == RECALL_DELEG_IN_PROGRESS) {
                /* Check if recall_time exceeded lease_time
                 * XXX: As per rfc, server shoudnt recall
                 * delegation if CB_PATH_DOWN.
                 */

                /* If its from the same client, grant access, mostly these could be 
                 * fops before returning the delegation. */
                if ((*up_entry)->deleg_cnt == 1) {
                        /* grant access */
                         /* XXX: do we need to check if its read_deleg that was
                          * getting recalled
                          * and the requested access now is write? Assuming those
                          * conflicts are
                         * taken care of by the application.
                         * If not we need 2 states - RECALL_READ_DELEG
                         * and RECALL_WRITE_DELEG */
                        goto out;
                } else { /* READ_DELEGs recall in progress, deny write access */
                        if (!is_write) {
                                goto out;
                        }
                        if (time(NULL) > (up_client->recall_time + LEASE_PERIOD)) {
                                /* Delete the delegation */
                                up_client->deleg = 0;
                                (*up_entry)->deleg_cnt--;
                                gf_log (THIS->name, GF_LOG_WARNING, "upcall Delegation recall time expired, deleting it - %s", up_client->client_uid);
                                if ((*up_entry)->deleg_cnt == 0) { /* No More READ_DELEGS present */
                                        goto out;
                                }
                        } else {
                                recall_sent = _gf_true;
                                goto out;
                        }
                }
        }

        pthread_mutex_lock (&(*up_entry)->u_client_mutex);
        list_for_each_entry (up_client_entry, &up_client->client_list, client_list) {
                if (up_client_entry->client_uid) {
                        if(strcmp(client->client_uid, up_client_entry->client_uid)) {
                                /* any other client */
                                if (up_client_entry->deleg == READ_WRITE_DELEG){
                                        /* there can only be one READ_WRITE_DELEG */
                                        /* If not found_client, continue loop to
                                         * lookup for that entry so that we can store it
                                         * in frame->root->client to be used in cbk path.*/
                                        /* send notify */
                                        GF_ASSERT ((*up_entry)->deleg_cnt == 1);
                                        uuid_copy(n_event_data.gfid, gfid);
                                        n_event_data.client_entry = up_client_entry;
                                        n_event_data.event_type =  up_client_entry->deleg;
                                        n_event_data.extra = NULL; /* Need to send inode flags */
                                        gf_log (THIS->name, GF_LOG_WARNING, "upcall Delegation recall - %s", up_client_entry->client_uid);
                                        frame->this->notify (frame->this, GF_EVENT_UPCALL, &n_event_data);
                                        recall_sent = _gf_true;
                                        up_client_entry->deleg = RECALL_DELEG_IN_PROGRESS;
                                        up_client_entry->recall_time = time(NULL);
                                        goto unlock;
                                } else if ((up_client_entry->deleg == READ_DELEG) && is_write) {
                                        /* send notify */
                                        uuid_copy(n_event_data.gfid, gfid);
                                        n_event_data.client_entry = up_client_entry;
                                        n_event_data.event_type =  up_client_entry->deleg;
                                        n_event_data.extra = NULL; /* Need to send inode flags */
                                        gf_log (THIS->name, GF_LOG_WARNING, "upcall Delegation recall - %s", up_client_entry->client_uid);
                                        frame->this->notify (frame->this, GF_EVENT_UPCALL, &n_event_data);
                                        recall_sent = _gf_true;
                                } else if (up_client_entry->deleg == RECALL_DELEG_IN_PROGRESS) {
                                        /* Check if recall_time exceeded lease_time
                                         * XXX: As per rfc, server shoudnt recall
                                         * delegation if CB_PATH_DOWN.
                                         */
                                        if (time(NULL) > (up_client_entry->recall_time + LEASE_PERIOD)) {
                                                /* Delete the delegation */
                                                up_client_entry->deleg = 0;
                                                (*up_entry)->deleg_cnt--;
                                                gf_log (THIS->name, GF_LOG_WARNING, "upcall Delegation recall time expired, deleting it - %s", up_client_entry->client_uid);
                                                if ((*up_entry)->deleg_cnt == 0) {
                                                 /* No More READ_DELEGS present */
                                                        goto unlock;
                                                }
                                        } else {
                                                recall_sent = _gf_true;
                                                goto unlock;
                                        }
                                }
                        }
                }
//                pthread_mutex_unlock (&(*up_entry)->u_client_mutex);
  //              pthread_mutex_lock (&(*up_entry)->u_client_mutex);
        }
unlock:
        pthread_mutex_unlock (&(*up_entry)->u_client_mutex);

out :
        return ((recall_sent == _gf_true) ?  1 : 0);

err:
        return -1;
}

int
remove_deleg (call_frame_t *frame, client_t *client, uuid_t gfid)
{
        upcall_client_entry * up_client_entry = NULL;
        upcall_entry * up_entry = NULL;

        if (((upcall_local_t *)frame->local)->client) {
                up_client_entry = ((upcall_local_t *)frame->local)->client;
        } else {
                up_client_entry = get_upcall_client_entry (frame, gfid, client, &up_entry);
                if (!up_client_entry) {
                        return -1;
                }
        }
        /* do we need to check if there were delegations?? */
        up_client_entry->deleg = 0;
        up_client_entry->access_time = time(NULL);
        gf_log (THIS->name, GF_LOG_WARNING, "upcall Delegation removed - %s",
                client->client_uid);
        return 0;
}

int
add_deleg (call_frame_t *frame, client_t *client, uuid_t gfid, gf_boolean_t is_write)
{
        upcall_client_entry * up_client_entry = NULL;
        upcall_entry * up_entry = NULL;

        if (((upcall_local_t *)frame->local)->client) {
                up_client_entry = ((upcall_local_t *)frame->local)->client;
        } else {
                up_client_entry = get_upcall_client_entry (frame, gfid, client,
                                                           &up_entry);
                if (!up_client_entry) {
                        return -1;
                }
        }

        if (is_write) { /* write_delegation */
                if (up_client_entry->deleg == READ_WRITE_DELEG)
                        return -1;
                up_client_entry->deleg = READ_WRITE_DELEG;
        } else {
                if (up_client_entry->deleg == READ_DELEG )
                        return -1;
                up_client_entry->deleg = READ_DELEG; 
        }

        up_client_entry->access_time = time(NULL);
        gf_log (THIS->name, GF_LOG_WARNING, "upcall Delegation added - %s",
                client->client_uid);
        return 0;
}

/* Currently used in cache_invalidation case.
 * Given a gfid, client, first fetch upcall_entry based on gfid.
 * Later traverse through the client list of that upcall entry. If this client
 * is not present in the list, create one client entry with this client info.
 * Also check if there are other clients which need to be notified of this
 * op. If yes send notify calls to them
 * Return '1' if notify need to be sent to other clients
 *  '0' if this is the only client
 *  '-1' incase of error. Not sure how we can handle errors here.
 *  Should we just ignore them?
 */
int
upcall_cache_invalidate (call_frame_t *frame, client_t *client, uuid_t gfid, void *extra)
{
        upcall_entry * up_entry = NULL;
        upcall_client_entry * up_client_entry = NULL;
        upcall_client_entry * up_client = NULL;
        notify_event_data n_event_data;
        time_t t = time(NULL);

        if (((upcall_local_t *)frame->local)->client) {
                up_client = ((upcall_local_t *)frame->local)->client;
                up_entry = get_upcall_entry(gfid);
        } else {
                up_client = get_upcall_client_entry (frame, gfid, client, &up_entry);
                if (!up_client) {
                        return -1;
                }
        }

        pthread_mutex_lock (&up_entry->u_client_mutex);
        list_for_each_entry (up_client_entry, &up_client->client_list, client_list) {
                if (up_client_entry->client_uid) {
                       if(strcmp(client->client_uid, up_client_entry->client_uid)) {
                                /* any other client */
                                /* TODO: check if that client entry is still valid.
                                 * It could have gone down or may be restarted in which case
                                 * client_uid may change and we end up in new client entry here?
                                 * If yes we are good, otherwise need to check if rpc/trans
                                 * objects are still valid
                                 */
                                if ((t-up_client_entry->access_time) < LEASE_PERIOD) { /* Send notify call */
                                        /* default cache_invalidation time is 60sec. Need to read that option
                                         * dynamically. */
                                        uuid_copy(n_event_data.gfid, gfid);
                                        n_event_data.client_entry = up_client_entry;
                                        n_event_data.event_type =  CACHE_INVALIDATION;
                                        n_event_data.extra = extra; /* Need to send inode flags */
                                        gf_log (THIS->name, GF_LOG_WARNING, "upcall cache invalidation sent - %s", up_client_entry->client_uid);
                                        frame->this->notify (frame->this, GF_EVENT_UPCALL, &n_event_data);
                                 } else {
                                        /* delete this entry ?? */
                                        /* del_entry = up_client_entry;
                                         * rpc_transport_unref (del_entry->trans);
                                        list_del_init (&del_entry->client_list);
                                        GF_FREE (del_entry);
                                        del_entry = NULL;*/
                                 }
                        }
                }
//                pthread_mutex_unlock (&up_entry->u_client_mutex);
  //              pthread_mutex_lock (&up_entry->u_client_mutex);
        }
        pthread_mutex_unlock (&up_entry->u_client_mutex);
        return 0;
}

