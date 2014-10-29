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

        up_entry = GF_CALLOC (1, sizeof(*up_entry), gf_upcalls_mt_upcall_entry_t);
        INIT_LIST_HEAD (&up_entry->list);
        INIT_LIST_HEAD (&up_entry->xprt_list);

        client = frame->root->client;
        up_entry->rpc = (rpcsvc_t *)(client->rpc);
        up_entry->trans = (rpc_transport_t *)client->trans; 

        /* Have to do it atomically or take lock */
        list_add_tail (&up_entry->list, &upcall_entry_list.list);
        this->notify (frame->this, GF_EVENT_UPCALL, &postbuf->ia_gfid);
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
        dict_t                     *dict                = NULL;
        char                       key[1024]            = {0};

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
        INIT_LIST_HEAD (&upcall_entry_list.xprt_list);
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
        rpcsvc_t  *svc = NULL;
        gfs3_upcall_req up_req;
        upcall_entry *up_entry = NULL;
        upcall_entry *old_entry = NULL;

        svc = this->private;

        switch (event) {
        case GF_EVENT_UPCALL:
        {        
                gf_log (this->name, GF_LOG_INFO, "Upcall Notify event = %d",
                        event);
                if (data) {
                        gf_log (this->name, GF_LOG_INFO, "Upcall - received data");
                        memcpy(up_req.gfid, (char *)data, 16);
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
                        }
                        if (old_entry) {
                                rpc_transport_unref (old_entry->trans);
                                list_del_init (&old_entry->list);
                                GF_FREE (old_entry);
                                old_entry = NULL;
                        }
                                        
                }
                break;
        }
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
