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

rpcsvc_cbk_program_t upcall_cbk_prog = {
        .progname  = "Gluster Callback",
        .prognum   = GLUSTER_CBK_PROGRAM,
        .progver   = GLUSTER_CBK_VERSION,
};

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
                pthread_mutex_unlock (&u_mutex);
                pthread_mutex_lock (&u_mutex);
        }
        pthread_mutex_unlock (&u_mutex);

        /* entry not found. create one */
        up_entry = GF_CALLOC (1, sizeof(*up_entry), gf_upcalls_mt_upcall_entry_t);
        if (!up_entry) {
                gf_log (THIS->name, GF_LOG_WARNING, "upcall_entry Memory allocation"
                        " failed");
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

upcall_client_entry*
get_upcall_client_entry (call_frame_t *frame, uuid_t gfid, client_t* client, upcall_entry **up_entry)
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
                        if(strcmp(client->client_uid, up_client_entry->client_uid) == 0) {
                                /* found client entry. Update the access_time */
                                up_client_entry->access_time = time(NULL);
                                found_client = _gf_true;
                 gf_log (THIS->name, GF_LOG_INFO, "upcall_entry client found - %s", up_client_entry->client_uid);
                                break;
                        }
                }
                pthread_mutex_unlock (&(*up_entry)->u_client_mutex);
                pthread_mutex_lock (&(*up_entry)->u_client_mutex);
        }
        pthread_mutex_unlock (&(*up_entry)->u_client_mutex);

        if (!found_client) { /* create one */
                up_client_entry = NULL;
                up_client_entry = GF_CALLOC (1, sizeof(**up_entry), gf_upcalls_mt_upcall_entry_t);
                if (!up_client_entry) {
                        gf_log (THIS->name, GF_LOG_WARNING, "upcall_entry Memory allocation"
                                " failed");
                        return NULL;
                }
                INIT_LIST_HEAD (&up_client_entry->client_list);
                up_client_entry->client_uid = gf_strdup(client->client_uid);
                up_client_entry->rpc = (rpcsvc_t *)(client->rpc);
                up_client_entry->trans = (rpc_transport_t *)client->trans; 
                up_client_entry->access_time = time(NULL);

                /* Have to do it atomically or take lock */
                pthread_mutex_lock (&(*up_entry)->u_client_mutex);
                list_add_tail (&up_client_entry->client_list, &(*up_entry)->client.client_list);
                pthread_mutex_unlock (&(*up_entry)->u_client_mutex);
                 gf_log (THIS->name, GF_LOG_INFO, "upcall_entry client added - %s", up_client_entry->client_uid);
                
        }
        frame->local = up_client_entry;
        return up_client_entry;
}

/*
 * Given a gfid, client, first fetch upcall_entry based on gfid.
 * Later traverse through the client list of that upcall entry. If this client
 * is not present in the list, create one client entry with this client info.
 * And store that client entry in the frame->local.
 * Also verify if there is any client entry with delegations set. If yes
 * send notify call.
 *
 * If we add 'deleg_list', we can traverse through clients holding delegation
 * using that list. We may need to pass the access type to check for conflict with
 * delegation.
 *
 * Like in nfs-ganesha code, may be for each upcall entry, store a variable
 * which denotes if/what delegation exists for faster lookup (incase of delegations
 * not present). Also for each upcall/upcall_client entries, change deleg  state to
 * RECALL_DELEG_IN_PROGRESS, store recall_time so that if that deleg doesnt get
 * cleared, we can check if recall_time is > lease_time or 2*lease_time and make a call
 * to lock xlator to cleanup that lock entry. Mostly this code/logic is already present
 * in ganesha code. Incase if it cant recall it revokes that delegation after certain time.
 * Cross check that. But even if ganesha handles and libgfapi client dies, we need a way to
 * cleanup these states. Hence store the recall_time. Mostly as like normal locks, lock
 * xlator might be cleaning up these lease locks as well when the client dies. So we may
 * not need a call to lock xlator after deleg expiry. Can there be cases where in libgfapi
 * client still present but unable to reclaim/revoke delegs?
 *
 * Now, while granting delegation, like in ganesha_code we can maintain, last_recall_time.
 * See the logic in "should_we_grant_deleg". If its already been recalled once recently,
 * then its ok not to grant delegations to this conflicting deleg request.
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
        /* If there is any request from same client, mostly, its the same application client sending
         * these fops. For example, when we sent recall_delegation request, it could issuing all the ops
         * to server WRITE, LOCK / OPEN etc...Not sure if we need to check for only RECALL_DELEG_IN_PROGRESS
         * state. Also hopefully, other application clients will be rejected by application itself and hence not
         * expecting those requests.
         */
        if (up_client->deleg == READ_WRITE_DELEG) {
                GF_ASSERT ((*up_entry)->deleg_cnt == 1);
                goto out;
        } else if (up_client->deleg == READ_DELEG) {
                if (is_write) {
                        /* usually same application client will not send for write access when
                         * taken read_delegation. But if its different application client, it should 
                         * have got resolved at apllication level itself. Still for sanity check,
                         * we will try to recall this delegation
                         */
                          /* send notify */
                        uuid_copy(n_event_data.gfid, gfid);
                        n_event_data.client_entry = up_client;
                        n_event_data.event_type =  up_client->deleg;
                        n_event_data.extra = NULL; /* Need to send inode flags */
                        gf_log (THIS->name, GF_LOG_WARNING, "upcall Delegation recall - %s", up_client->client_uid);
                        up_client->deleg = RECALL_DELEG_IN_PROGRESS;
                        up_client->recall_time = time(NULL);
                        frame->this->notify (frame->this, GF_EVENT_UPCALL, &n_event_data);
                        recall_sent = _gf_true;
                        if ((*up_entry)->deleg_cnt == 1) { /* No More READ_DELEGS present */
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
                         /* XXX: do we need to check if its read_deleg that was getting recalled
                        * and the requested access now is write? Assuming those conflicts are
                         * taken care of by the application. If not we need 2 states - RECALL_READ_DELEG
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
                pthread_mutex_unlock (&(*up_entry)->u_client_mutex);
                pthread_mutex_lock (&(*up_entry)->u_client_mutex);
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

        if (frame->local) {
                up_client_entry = (upcall_client_entry *)frame->local;
        } else {
                up_client_entry = get_upcall_client_entry (frame, gfid, client, &up_entry);
                if (!up_client_entry) {
                        return -1;
                }
        }
        /* do we need to check if there were delegations?? */
        up_client_entry->deleg = 0;
        up_client_entry->access_time = time(NULL);
        gf_log (THIS->name, GF_LOG_WARNING, "upcall Delegation removed - %s", client->client_uid);
        return 0;
}

int
add_deleg (call_frame_t *frame, client_t *client, uuid_t gfid, gf_boolean_t is_write)
{
        upcall_client_entry * up_client_entry = NULL;
        upcall_entry * up_entry = NULL;

        if (frame->local) {
                up_client_entry = (upcall_client_entry *)frame->local;
        } else {
                up_client_entry = get_upcall_client_entry (frame, gfid, client, &up_entry);
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
        gf_log (THIS->name, GF_LOG_WARNING, "upcall Delegation added - %s", client->client_uid);
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
 *  '-1' incase of error. Not sure how we can handle errors here. Should we just ignore them?
 */
int
upcall_cache_invalidate (call_frame_t *frame, client_t *client, uuid_t gfid, void *extra)
{
        upcall_entry * up_entry = NULL;
        upcall_client_entry * up_client_entry = NULL;
        upcall_client_entry * up_client = NULL;
        notify_event_data n_event_data;
        time_t t = time(NULL);

        if (frame->local) {
                up_client = (upcall_client_entry *)frame->local;
                frame->local = NULL;
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
                pthread_mutex_unlock (&up_entry->u_client_mutex);
                pthread_mutex_lock (&up_entry->u_client_mutex);
        }
        pthread_mutex_unlock (&up_entry->u_client_mutex);
        return 0;
}

int
up_open_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
             int32_t op_ret, int32_t op_errno, fd_t *fd, dict_t *xdata)
{

        if (op_ret < 0)
                goto unwind;

unwind:
        STACK_UNWIND_STRICT (open, frame, op_ret, op_errno, fd, xdata);

        return 0;
}


int
up_open (call_frame_t *frame, xlator_t *this, loc_t *loc, int32_t flags,
         fd_t *fd, dict_t *xdata)
{
        STACK_WIND (frame, up_open_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->open,
                    loc, flags, fd, xdata);

        return 0;
}


int
up_writev_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                int op_ret, int op_errno, struct iatt *prebuf,
                struct iatt *postbuf, dict_t *xdata)
{
        client_t         *client        = NULL;
        upcall_entry     *up_entry = NULL;
        uint32_t        flags;
        int notify = 0;
        upcall_client_entry * up_client_entry = NULL;
        notify_event_data n_event_data;

        client = frame->root->client;

        if (op_ret < 0) {
                goto out;
        }
        gf_log (this->name, GF_LOG_INFO, "In writev_cbk ");
        flags = (UP_SIZE | UP_TIMES) ;
        upcall_cache_invalidate (frame, client, postbuf->ia_gfid, &flags);
#ifdef NOT_REQ
        up_entry->rpc = (rpcsvc_t *)(client->rpc);
        up_entry->trans = (rpc_transport_t *)client->trans; 

        /* Have to do it atomically or take lock */
        list_add_tail (&up_entry->list, &upcall_entry_list.list);
        this->notify (frame->this, GF_EVENT_UPCALL, &postbuf->ia_gfid);
#endif

out:
        frame->local = NULL;
        STACK_UNWIND_STRICT (writev, frame, op_ret, op_errno, prebuf, postbuf, xdata);
        return 0;
}


int
up_writev (call_frame_t *frame, xlator_t *this, fd_t *fd,
            struct iovec *vector, int count, off_t off, uint32_t flags,
            struct iobref *iobref, dict_t *xdata)
{
        int ret = -1;
        client_t            *client        = NULL;
        dict_t              *dict                = NULL;
        char                 key[1024]            = {0};
        upcall_client_entry *up_client_entry = NULL;
        upcall_entry *up_entry = NULL;
        int32_t         op_errno = ENOMEM;

        client = frame->root->client;
        frame->local = NULL;
        
#ifdef USE_XDATA_FOR_UPCALLS
        upcall_entry * up_entry = NULL;

        up_entry = GF_CALLOC (1, sizeof(*up_entry), gf_upcalls_mt_upcall_entry_t);
        INIT_LIST_HEAD (&up_entry->list);
        INIT_LIST_HEAD (&up_entry->xprt_list);
        snprintf (key, sizeof (key), "rpc");
        ret = dict_get_ptr (xdata, key, (void **)&up_entry->rpc);
        if (ret) {
                errno = EINVAL;
                goto out;
        }
        dict_del (xdata, key);

        snprintf (key, sizeof (key), "trans");
        ret = dict_get_ptr (xdata, key, (void **)&up_entry->trans);
        if (ret) {
                errno = EINVAL;
                goto out;
        }

        dict_del (xdata, key);

        /* Have to do it atomically or take lock */
        list_add_tail (&up_entry->list, &upcall_entry_list.list);
#endif
#ifdef deleg
        upcall_client_entry * up_client_entry = NULL;

        client = frame->root->client;

        notify  = process_client_entry_deleg (client, ia_gfid);
        if (notify) {
               /* there are clients who were sent deleg_recall
                * wait till you get back response from them
                * till then send EDELAY error;
                */
                goto err;
        }
#endif
        gf_log (this->name, GF_LOG_INFO, "In writev ");
        ret = upcall_deleg_check (frame, client, fd->inode->gfid,
                                  _gf_true, &up_entry);

        if (ret == 1) {
                /* conflict delegation found and recall has been sent.
                 * Send ERR_DELAY */
                gf_log (this->name, GF_LOG_INFO, "Delegation conflict.sending EDELAY ");
                op_errno = EAGAIN; /* ideally should have been EDELAY */
                goto err;
        } else if (ret == 0) {
                gf_log (this->name, GF_LOG_INFO, "No Delegation conflict. continuing with fop ");
                /* No conflict delegation. Go ahead with the fop */
        } else { /* erro */
                op_errno = EINVAL;
                goto err;
        }

        STACK_WIND (frame, up_writev_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->writev,
                    fd, vector, count, off, flags, iobref, xdata);

        return 0;

#ifdef USE_XDATA_FOR_UPCALLS
out:
       return ret;
#endif
err:
//        STACK_UNWIND_STRICT (writev, frame, -1, op_errno, NULL, NULL, NULL);
        frame->local = NULL;
        up_writev_cbk (frame, NULL, frame->this, -1, op_errno, NULL, NULL, NULL);
        return 0;
}


int
up_readv_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
               int op_ret, int op_errno,
               struct iovec *vector, int count, struct iatt *stbuf,
               struct iobref *iobref, dict_t *xdata)
{
        client_t         *client        = NULL;
        upcall_entry     *up_entry = NULL;
        uint32_t        flags;
        int notify = 0;
        upcall_client_entry * up_client_entry = NULL;
        notify_event_data n_event_data;

        client = frame->root->client;

        if (op_ret < 0) {
                goto out;
        }
        gf_log (this->name, GF_LOG_INFO, "In readv_cbk ");
        flags = (UP_ATIME) ;
        upcall_cache_invalidate (frame, client, stbuf->ia_gfid, &flags);

out:
        frame->local = NULL;
        STACK_UNWIND_STRICT (readv, frame, op_ret, op_errno, vector, count, stbuf,
                          iobref, xdata);

        return 0;
}

int
up_readv (call_frame_t *frame, xlator_t *this,
             fd_t *fd, size_t size, off_t offset, uint32_t flags, dict_t *xdata)
{
        int ret = -1;
        client_t            *client        = NULL;
        dict_t              *dict                = NULL;
        char                 key[1024]            = {0};
        upcall_client_entry *up_client_entry = NULL;
        upcall_entry *up_entry = NULL;
        int32_t         op_errno = ENOMEM;

        client = frame->root->client;
        frame->local = NULL;

        gf_log (this->name, GF_LOG_INFO, "In readv ");
        ret = upcall_deleg_check (frame, client, fd->inode->gfid,
                                  _gf_false, &up_entry);

        if (ret == 1) {
                /* conflict delegation found and recall has been sent.
 *                  * Send ERR_DELAY */
                gf_log (this->name, GF_LOG_INFO, "Delegation conflict.sending EDELAY ");
                op_errno = EAGAIN; /* ideally should have been EDELAY */
                goto err;
        } else if (ret == 0) {
                gf_log (this->name, GF_LOG_INFO, "No Delegation conflict. continuing with fop ");
                /* No conflict delegation. Go ahead with the fop */
        } else { /* erro */
                op_errno = EINVAL;
                goto err;
        }

        STACK_WIND (frame, up_readv_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->readv,
                    fd, size, offset, flags, xdata);

        return 0;

err:
        frame->local = NULL;
        op_errno = (op_errno == -1) ? errno : op_errno;
        STACK_UNWIND_STRICT (readv, frame, -1, op_errno, NULL, 0, NULL, NULL, NULL);

        return 0;
}

int32_t
up_lk_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                int32_t op_ret, int32_t op_errno, struct gf_flock *lock,
                dict_t *xdata)
{
        if (op_ret < 0) {
                goto out;
        }

out:
        gf_log (this->name, GF_LOG_INFO, "In lk_cbk ");
        frame->local = NULL;
        STACK_UNWIND_STRICT (lk, frame, op_ret, op_errno, lock, xdata);
        return 0;
}

int
up_lk (call_frame_t *frame, xlator_t *this,
       fd_t *fd, int32_t cmd, struct gf_flock *flock, dict_t *xdata)
{
        int ret = -1;
        client_t            *client        = NULL;
        dict_t              *dict                = NULL;
        gf_boolean_t            delegations_enabled = 0;
        char                 key[1024]            = {0};
        upcall_client_entry *up_client_entry = NULL;
        upcall_entry *up_entry = NULL;
        int32_t op_errno = ENOMEM;
        gf_boolean_t is_write_deleg = _gf_false;

        gf_log (this->name, GF_LOG_INFO, "In up_lk ");
        client = frame->root->client;
        frame->local = NULL;
        snprintf (key, sizeof (key), "set_delegation");
        ret = dict_get_ptr (xdata, key, (void **)&delegations_enabled);
        if (ret) {
                errno = EINVAL;
                goto err;
        }
        dict_del (xdata, key);

        /* Either lock/delegation request, first check if there
         * are any conflicting delegation present.
         * The reason we pass write as "true" is that in ganesha code, for even
         * lock op (could be read/write), as we add new state, its considered
         * equivalent to write access. Confirm if its correct.
         * Skip this if its unlock request. Reason being
         *      if its DELEG_RETURN request we not send recalls
         *      if its unlock request, the reason there is a lock present means that
         *      there are delegations on that file taken from other client;
         *
         * XXX: Check if all this delegation conflict resolution is correct as per RFC
         */
        if (flock->l_type != GF_LK_F_UNLCK) {
                ret = upcall_deleg_check (frame, client, fd->inode->gfid, _gf_true, &up_entry);

                if (ret == 1) {
                        /* conflict delegation found and recall has been sent.
                         * Send ERR_DELAY */
                        op_errno = EAGAIN;
                        gf_log (this->name, GF_LOG_INFO, "Delegation conflict.sending EDELAY ");
                        goto err;
                } else if (ret == 0) {
                        gf_log (this->name, GF_LOG_INFO, "No Delegation conflict. continuing with fop ");
                        /* No conflict delegation. Go ahead with the fop */
                } else { /* erro */
                        op_errno = EINVAL;
                        goto err;
                }
        }

        if (delegations_enabled) {
                /* Add the delegation
                 * Client must have been created. So frame->root->client
                 * must contain the client entry.
                 */
                if (!up_entry) {
                        up_entry = get_upcall_entry (fd->inode->gfid);
                }
                switch (flock->l_type) {
                case GF_LK_F_RDLCK:
                        is_write_deleg = _gf_false;
                        ret = add_deleg (frame, client, fd->inode->gfid, is_write_deleg);
                        up_entry->deleg_cnt++;
                        break;
                case GF_LK_F_WRLCK:
                        is_write_deleg = _gf_true;
                        add_deleg (frame, client, fd->inode->gfid, is_write_deleg);
                        up_entry->deleg_cnt++;
                        break;
                case GF_LK_F_UNLCK:
                        remove_deleg (frame, client, fd->inode->gfid);
                        up_entry->deleg_cnt--;
                        break;
                }
                if (ret < 0) {
                        goto err;
                }
        }
 
        STACK_WIND (frame, up_lk_cbk, FIRST_CHILD(this), FIRST_CHILD(this)->fops->lk,
                    fd, cmd, flock, xdata);
        return 0;

err:
//        STACK_UNWIND (lk, frame, -1, op_errno, NULL, NULL);
        frame->local = NULL;
        up_lk_cbk (frame, NULL, frame->this, -1, op_errno, NULL, NULL);
        return 0;
}

int32_t
mem_acct_init (xlator_t *this)
{
        int     ret = -1;

        if (!this)
                return ret;

        ret = xlator_mem_acct_init (this, gf_upcalls_mt_end + 1);

        if (ret != 0) {
                gf_log (this->name, GF_LOG_WARNING, "Memory accounting"
                        " init failed");
                return ret;
        }

        return ret;
}

int
init (xlator_t *this)
{
	int			ret	= -1;
        upcalls_private_t	*priv	= NULL;

        priv = GF_CALLOC (1, sizeof (*priv),
                          gf_upcalls_mt_private_t);
	if (!priv) {
		ret = -1;
		gf_log (this->name, GF_LOG_ERROR,
			"Error allocating private struct in xlator init");
		goto out;
	}

	this->private = priv;
	ret = 0;

        INIT_LIST_HEAD (&upcall_entry_list.list);
        INIT_LIST_HEAD (&upcall_entry_list.client.client_list);
        pthread_mutex_init (&u_mutex, NULL); 
        upcall_entry_list.deleg_cnt = 0;
out:
	if (ret) {
		GF_FREE (priv);
	}

	return ret;
}

int
fini (xlator_t *this)
{
	upcalls_private_t *priv = NULL;

	priv = this->private;
	if (!priv) {
		return 0;
	}
	this->private = NULL;
	GF_FREE (priv);

	return 0;
}

int
notify (xlator_t *this, int32_t event, void *data, ...)
{
        int          ret = 0;
        int32_t      val = 0;
        notify_event_data *notify_event = NULL;
        gfs3_upcall_req up_req;
        upcall_client_entry *up_client_entry = NULL;
        upcall_entry *old_entry = NULL;

        switch (event) {
        case GF_EVENT_UPCALL:
        {        
                gf_log (this->name, GF_LOG_INFO, "Upcall Notify event = %d",
                        event);
                if (data) {
                        gf_log (this->name, GF_LOG_INFO, "Upcall - received data");
                        notify_event = (notify_event_data *)data;
                        up_client_entry = notify_event->client_entry;

                        if (!up_client_entry) {
                                return -1;
                        }
                        memcpy(up_req.gfid, notify_event->gfid, 16);
                        gf_log (this->name, GF_LOG_INFO, "Sending notify to the client- %s, gfid - %s", up_client_entry->client_uid, up_req.gfid);
                        switch (notify_event->event_type) {
                        case CACHE_INVALIDATION:
//                                up_req.event_type = 1;
                                GF_ASSERT (notify_event->extra);
                                up_req.flags = *(uint32_t *)(notify_event->extra);
                                break;
                        case READ_DELEG:
                        case READ_WRITE_DELEG:
  //                              up_req.event_type = 2;
                                up_req.flags = 0;
                                break;
                        default:
                                return -1;
                                // shouldnt be reaching here.
                        }
                        up_req.event_type = notify_event->event_type;
                        rpcsvc_request_submit(up_client_entry->rpc, up_client_entry->trans,
                                              &upcall_cbk_prog, GF_CBK_UPCALL,
                                              &up_req, this->ctx,
                                              (xdrproc_t) xdr_gfs3_upcall_req);
                }
                break;
        }
#ifdef NOT_REQ
                      //  list_for_each_entry (up_entry, &upcall_entry_list.list, list) {
                        //        if (memcmp(up_entry->gfid, up_req.gfid) == 0) {
                                        // found the right entry
                                        // send upcalls for ech of the clients accessing that file.
                                        list_for_each_entry (up_entry, &upcall_entry_list.list, list) {
                                        /* Need a cleaner way of cleaning this list
                                         * Mostly old_entry stuff will be taken out once we start
                                         * maintaining client list and cleanup expired clients entries.
                                         */
                                        if (old_entry) {
                                                rpc_transport_unref (old_entry->trans);
                                                list_del_init (&old_entry->list);
                                                GF_FREE (old_entry);
                                                old_entry = NULL;
                                        }                                
                                        rpcsvc_request_submit(up_entry->rpc, up_entry->trans,
                                                       &upcall_cbk_prog, GF_CBK_UPCALL,
                                                       &up_req, this->ctx,
                                                       (xdrproc_t) xdr_gfs3_upcall_req);
                                        old_entry = up_entry;
                          //      }
                        
                        if (old_entry) {
                                rpc_transport_unref (old_entry->trans);
                                list_del_init (&old_entry->list);
                                GF_FREE (old_entry);
                                old_entry = NULL;
                        }
                      //  }                
                }
                break;
        }
#endif
        default:
                default_notify (this, event, data);
                break;
        }
        return ret;
}



struct xlator_fops fops = {
        .open        = up_open,
        .readv       = up_readv,
        .writev      = up_writev,
        .lk          = up_lk,
};

struct xlator_cbks cbks = {
};

struct volume_options options[] = {
        { .key = {NULL} },
};
