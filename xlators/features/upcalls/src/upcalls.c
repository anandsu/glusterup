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
        list_for_each_entry (up_entry, &upcall_entry_list.list, list) {
                if (memcmp(up_entry->gfid, gfid, 16) == 0) {
                        /* found entry */
                        return up_entry;
                }
        }
        /* entry not found. create one */
        up_entry = GF_CALLOC (1, sizeof(*up_entry), gf_upcalls_mt_upcall_entry_t);
        if (!up_entry) {
                gf_log (THIS->name, GF_LOG_WARNING, "upcall_entry Memory allocation"
                        " failed");
                return NULL;
        }
        INIT_LIST_HEAD (&up_entry->list);
        uuid_copy(up_entry->gfid, gfid);
        INIT_LIST_HEAD(&up_entry->client.client_list);

        /* Have to do it atomically or take lock */
        list_add_tail (&up_entry->list, &upcall_entry_list.list);
        return up_entry;
        
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
 * RECALL_DELEGATION_IN_PROGRESS, store recall_time so that if that deleg doesnt get
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
 * routines to be checked "should_We_grant_deleg", " check_deleg_conflict" etc..
 */
int
upcall_deleg_check (call_frame_t *frame, client_t *client, uuid_t gfid, void *extra)
{
        upcall_entry * up_entry = NULL;
        upcall_client_entry * up_client_entry = NULL;
        int found_client = 0;
        notify_event_data n_event_data;
        time_t t = time(NULL);

        up_entry = get_upcall_entry(gfid);
        if (!up_entry) { /* cannot reach here */
                return -1;
        }

        list_for_each_entry (up_client_entry, &up_entry->client.client_list, client_list) {
                if (up_client_entry->client_uid) {
                if(strcmp(client->client_uid, up_client_entry->client_uid) == 0) {
                        /* found client entry. Update the timestamp */
                        up_client_entry->timestamp = t;
                        found_client = 1;
                        frame->local = up_client_entry;
                } else { /* any other client */
                        if (up_client_entry->deleg) {
                                /* send notify */
                                uuid_copy(n_event_data.gfid, gfid);
                                n_event_data.client_entry = up_client_entry;
                                n_event_data.event_type =  up_client_entry->deleg;
                                n_event_data.extra = extra; /* Need to send inode flags */
                                frame->this->notify (frame->this, GF_EVENT_UPCALL, &n_event_data);
                         }
                }
                }
        }
        if (!found_client) { /* create one */
                up_client_entry = NULL;
                up_client_entry = GF_CALLOC (1, sizeof(*up_entry), gf_upcalls_mt_upcall_entry_t);
                if (!up_client_entry) {
                        gf_log (frame->this->name, GF_LOG_WARNING, "upcall_entry Memory allocation"
                                " failed");
                        return -1;
                }
                INIT_LIST_HEAD (&up_client_entry->client_list);
                up_client_entry->client_uid = gf_strdup(client->client_uid);
                up_client_entry->rpc = (rpcsvc_t *)(client->rpc);
                up_client_entry->trans = (rpc_transport_t *)client->trans; 
                up_client_entry->timestamp = t;

                /* Have to do it atomically or take lock */
                list_add_tail (&up_client_entry->client_list, &up_entry->client.client_list);
                frame->local = up_client_entry;
                 gf_log (frame->this->name, GF_LOG_INFO, "upcall_entry client added - %s", up_client_entry->client_uid);
                
        }
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
        int found_client = 0;
        notify_event_data n_event_data;
        time_t t = time(NULL);

        if (frame->local) {
                up_client = (upcall_client_entry *)frame->local;
                found_client = 1;
                frame->local = NULL;
        } else {
                up_entry = get_upcall_entry(gfid);
                if (!up_entry) { /* cannot reach here */
                        return -1;
                }
                up_client = &up_entry->client;
        }

        list_for_each_entry (up_client_entry, &up_client->client_list, client_list) {
                if (up_client_entry->client_uid) {
                if(strcmp(client->client_uid, up_client_entry->client_uid) == 0) {
                        /* found client entry. Update the timestamp */
                        up_client_entry->timestamp = t;
                        found_client = 1;
                } else { /* any other client */
                        /* TODO: check if that client entry is still valid.
                         * It could have gone down or may be restarted in which case
                         * client_uid may change and we end up in new client entry here?
                         * If yes we are good, otherwise need to check if rpc/trans
                         * objects are still valid
                         */
                        if ((t-up_client_entry->timestamp) < 60) { /* Send notify call */
                                /* default cache_invalidation time is 60sec. Need to read that option
                                 * dynamically. */
                                uuid_copy(n_event_data.gfid, gfid);
                                n_event_data.client_entry = up_client_entry;
                                n_event_data.event_type =  CACHE_INVALIDATION;
                                n_event_data.extra = extra; /* Need to send inode flags */
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
        }
        if (!found_client) { /* create one */
                up_client_entry = NULL;
                up_client_entry = GF_CALLOC (1, sizeof(*up_entry), gf_upcalls_mt_upcall_entry_t);
                if (!up_client_entry) {
                        gf_log (frame->this->name, GF_LOG_WARNING, "upcall_entry Memory allocation"
                                " failed");
                        return -1;
                }
                INIT_LIST_HEAD (&up_client_entry->client_list);
                up_client_entry->client_uid = gf_strdup(client->client_uid);
                up_client_entry->rpc = (rpcsvc_t *)(client->rpc);
                up_client_entry->trans = (rpc_transport_t *)client->trans; 
                up_client_entry->timestamp = t;

                /* Have to do it atomically or take lock */
                list_add_tail (&up_client_entry->client_list, &up_entry->client.client_list);
                 gf_log (frame->this->name, GF_LOG_INFO, "upcall_entry client added - %s", up_client_entry->client_uid);
                
        }
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
        int notify = 0;
        upcall_client_entry * up_client_entry = NULL;
        notify_event_data n_event_data;

        client = frame->root->client;

        upcall_cache_invalidate (frame, client, postbuf->ia_gfid, NULL);
#ifdef NOT_REQ
        up_entry->rpc = (rpcsvc_t *)(client->rpc);
        up_entry->trans = (rpc_transport_t *)client->trans; 

        /* Have to do it atomically or take lock */
        list_add_tail (&up_entry->list, &upcall_entry_list.list);
        this->notify (frame->this, GF_EVENT_UPCALL, &postbuf->ia_gfid);
#endif
//out:
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

        client = frame->root->client;
        frame->local = NULL;
        upcall_deleg_check (frame, client, fd->inode->gfid, &up_client_entry);

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
#ifdef NOT_REQ

#endif
        STACK_WIND (frame, up_writev_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->writev,
                    fd, vector, count, off, flags, iobref, xdata);

        return 0;

#ifdef USE_XDATA_FOR_UPCALLS
out:
       return ret;
#endif
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
                                /* Need to find a way on how to send extra flags;
                                 * Maybe add xdata to gfs3_upcall_req
                                 */
                                break;
                        case READ_DELEG:
                        case READ_WRITE_DELEG:
                                break;
                        default:
                                return -1;
                                // shouldnt be reaching here.
                        }
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
        .writev      = up_writev,
};

struct xlator_cbks cbks = {
};

struct volume_options options[] = {
        { .key = {NULL} },
};
