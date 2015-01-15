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

#define PROCESS_DELEG(frame, client, gfid, is_write, up_entry) do {\
        ret = upcall_deleg_check (frame, client, gfid, is_write, up_entry); \
        if (ret == 1) {                 \
                /* conflict delegation found and recall has been sent. \
 *                  * Send ERR_DELAY */ \
                gf_log (this->name, GF_LOG_INFO, "Delegation conflict.sending EDELAY ");                               \
                op_errno = EAGAIN; /* ideally should have been EDELAY */ \
                goto err;               \
        } else if (ret == 0) {          \
                gf_log (this->name, GF_LOG_INFO, "No Delegation conflict. continuing with fop ");                       \
                /* No conflict delegation. Go ahead with the fop */ \
        } else { /* erro */             \
                op_errno = EINVAL;      \
                goto err;               \
        }                               \
        } while (0)

rpcsvc_cbk_program_t upcall_cbk_prog = {
        .progname  = "Gluster Callback",
        .prognum   = GLUSTER_CBK_PROGRAM,
        .progver   = GLUSTER_CBK_VERSION,
};

int
up_open_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
             int32_t op_ret, int32_t op_errno, fd_t *fd, dict_t *xdata)
{
        client_t         *client        = NULL;
        upcall_entry     *up_entry = NULL;
        uint32_t        flags;
        int notify = 0;
        upcall_client_entry * up_client_entry = NULL;
        notify_event_data n_event_data;
        upcall_local_t  *local = NULL;

        client = frame->root->client;
        local = frame->local;

        if (op_ret < 0) {
                goto unwind;
        }
        gf_log (this->name, GF_LOG_INFO, "In open_cbk ");
        flags = (UP_ATIME) ;
        upcall_cache_invalidate (frame, client, local->gfid, &flags);


unwind:
        UPCALL_STACK_UNWIND (open, frame, op_ret, op_errno, fd, xdata);

        return 0;
}


int
up_open (call_frame_t *frame, xlator_t *this, loc_t *loc, int32_t flags,
         fd_t *fd, dict_t *xdata)
{
        int ret = -1;
        client_t            *client        = NULL;
        dict_t              *dict                = NULL;
        char                 key[1024]            = {0};
        upcall_client_entry *up_client_entry = NULL;
        upcall_entry *up_entry = NULL;
        int32_t         op_errno = ENOMEM;
        gf_boolean_t   is_write = _gf_false;
        upcall_local_t *local = NULL;

        local = upcall_local_init(frame, fd->inode->gfid);
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }

        client = frame->root->client;

        gf_log (this->name, GF_LOG_INFO, "In open ");

        if (flags & (O_WRONLY | O_RDWR)) {
                is_write = _gf_true;
        }
        PROCESS_DELEG (frame, client, local->gfid,
                       is_write, &up_entry);

        STACK_WIND (frame, up_open_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->open,
                    loc, flags, fd, xdata);

        return 0;

err:
//        op_errno = (op_errno == -1) ? errno : op_errno;
        UPCALL_STACK_UNWIND (open, frame, -1, op_errno, NULL, NULL);
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
        upcall_local_t  *local = NULL;

        client = frame->root->client;
        local = frame->local;

        if (op_ret < 0) {
                goto out;
        }
        gf_log (this->name, GF_LOG_INFO, "In writev_cbk ");
        flags = (UP_SIZE | UP_TIMES) ;
        upcall_cache_invalidate (frame, client, local->gfid, &flags);
out:
        UPCALL_STACK_UNWIND (writev, frame, op_ret, op_errno, prebuf, postbuf, xdata);
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
        upcall_local_t *local = NULL;

        local = upcall_local_init(frame, fd->inode->gfid);
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }

        client = frame->root->client;
        
        gf_log (this->name, GF_LOG_INFO, "In writev ");
        PROCESS_DELEG (frame, client, local->gfid,
                       _gf_true, &up_entry);

        STACK_WIND (frame, up_writev_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->writev,
                    fd, vector, count, off, flags, iobref, xdata);

        return 0;

err:
//        UPCALL_STACK_UNWIND (writev, frame, -1, op_errno, NULL, NULL, NULL);
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
        upcall_local_t  *local = NULL;

        client = frame->root->client;
        local = frame->local;

        if (op_ret < 0) {
                goto out;
        }
        gf_log (this->name, GF_LOG_INFO, "In readv_cbk ");
        flags = (UP_ATIME) ;
        upcall_cache_invalidate (frame, client, local->gfid, &flags);

out:
        UPCALL_STACK_UNWIND (readv, frame, op_ret, op_errno, vector, count, stbuf,
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
        upcall_local_t *local = NULL;

        local = upcall_local_init(frame, fd->inode->gfid);
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }

        client = frame->root->client;

        gf_log (this->name, GF_LOG_INFO, "In readv ");
        PROCESS_DELEG (frame, client, local->gfid,
                       _gf_false, &up_entry);

        STACK_WIND (frame, up_readv_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->readv,
                    fd, size, offset, flags, xdata);

        return 0;

err:
        //op_errno = (op_errno == -1) ? errno : op_errno;
        UPCALL_STACK_UNWIND (readv, frame, -1, op_errno, NULL, 0, NULL, NULL, NULL);

        return 0;
}

int32_t
up_lk_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                int32_t op_ret, int32_t op_errno, struct gf_flock *lock,
                dict_t *xdata)
{
        upcall_local_t  *local = NULL;
        uint32_t        flags;
        int notify = 0;
        client_t *client = NULL;
        upcall_entry *up_entry = NULL;
        gf_boolean_t is_write_deleg = _gf_false;
        int ret = -1;

        client = frame->root->client;
        local = frame->local;

        if (op_ret < 0) {
                gf_log (this->name, GF_LOG_INFO, "In lk_cbk op_ret = %d", op_ret);
                goto out;
        }
        gf_log (this->name, GF_LOG_INFO, "In lk_cbk ");
        if (local->is_delegation_enabled) {
                gf_log (this->name, GF_LOG_INFO, "In up_lk_cbk, in"
                        " is_delegation_enabled block");
                /* Add the delegation
                 * Client must have been created. So frame->root->client
                 * must contain the client entry.
                 */
                if (!up_entry) {
                        up_entry = get_upcall_entry (local->gfid);
                }
                switch (lock->l_type) {
                case GF_LK_F_RDLCK:
                        is_write_deleg = _gf_false;
                        ret = add_deleg (frame, client, local->gfid, is_write_deleg);
                        if (ret < 0 ) {
                                goto err;
                        }
                        up_entry->deleg_cnt++;
                        break;
                case GF_LK_F_WRLCK:
                        is_write_deleg = _gf_true;
                        ret = add_deleg (frame, client, local->gfid, is_write_deleg);
                        if (ret < 0 ) {
                                goto err;
                        }
                        up_entry->deleg_cnt++;
                        break;
                case GF_LK_F_UNLCK:
                        ret = remove_deleg (frame, client, local->gfid);
                        if (ret < 0 ) {
                                goto err;
                        }
                        up_entry->deleg_cnt--;
                        break;
                }
        }
        flags = (UP_ATIME) ;
        upcall_cache_invalidate (frame, client, local->gfid, &flags);
        goto out;

err:
        op_ret = ret;
        op_errno = EINVAL;
out:
        gf_log (this->name, GF_LOG_INFO, "In lk_cbk ");
        UPCALL_STACK_UNWIND (lk, frame, op_ret, op_errno, lock, xdata);
        return 0;
}

int
up_lk (call_frame_t *frame, xlator_t *this,
       fd_t *fd, int32_t cmd, struct gf_flock *flock, dict_t *xdata)
{
        int ret = -1;
        client_t            *client        = NULL;
        dict_t              *dict                = NULL;
        int32_t         is_delegation_enabled = 0;
        char                 key[1024]            = {0};
        upcall_client_entry *up_client_entry = NULL;
        upcall_entry *up_entry = NULL;
        int32_t op_errno = ENOMEM;
        gf_boolean_t is_write_deleg = _gf_false;
        upcall_local_t *local = NULL;

        local = upcall_local_init(frame, fd->inode->gfid);
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }

        gf_log (this->name, GF_LOG_INFO, "In up_lk ");
        client = frame->root->client;
        snprintf (key, sizeof (key), "set_delegation");
        ret = dict_get_int32 (xdata, key, (void *)&is_delegation_enabled);
        if (ret) {
                op_errno = EINVAL;
                goto err;
        }
        gf_log (this->name, GF_LOG_INFO, "In up_lk, is_delegation_enabled = %d ",
                is_delegation_enabled);
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
                PROCESS_DELEG (frame, client, local->gfid,
                               _gf_true, &up_entry);
        }

        if (is_delegation_enabled) {
                gf_log (this->name, GF_LOG_INFO, "In up_lk, in"
                        " is_delegation_enabled block");
                local->is_delegation_enabled = _gf_true;
        }
 
        STACK_WIND (frame, up_lk_cbk, FIRST_CHILD(this), FIRST_CHILD(this)->fops->lk,
                    fd, cmd, flock, xdata);
        return 0;

err:
        gf_log (this->name, GF_LOG_INFO, "In up_lk err section, ret = %d, op_errno=%d", ret, op_errno);
//        STACK_UNWIND (lk, frame, -1, op_errno, NULL, NULL);
        up_lk_cbk (frame, NULL, frame->this, -1, op_errno, NULL, NULL);
        return 0;
}

int
up_truncate_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int op_ret, int op_errno, struct iatt *prebuf,
                  struct iatt *postbuf, dict_t *xdata)
{
        client_t         *client        = NULL;
        upcall_entry     *up_entry = NULL;
        uint32_t        flags;
        int notify = 0;
        upcall_client_entry * up_client_entry = NULL;
        notify_event_data n_event_data;
        upcall_local_t  *local = NULL;

        client = frame->root->client;
        local = frame->local;

        if (op_ret < 0) {
                goto out;
        }
        gf_log (this->name, GF_LOG_INFO, "In truncate_cbk ");
        flags = (UP_SIZE | UP_TIMES) ;
        upcall_cache_invalidate (frame, client, local->gfid, &flags);

out:
        UPCALL_STACK_UNWIND (truncate, frame, op_ret, op_errno,
                             prebuf, postbuf, xdata);
        return 0;
}

int
up_truncate (call_frame_t *frame, xlator_t *this, loc_t *loc, off_t offset,
              dict_t *xdata)
{
        int ret = -1;
        client_t            *client        = NULL;
        dict_t              *dict                = NULL;
        char                 key[1024]            = {0};
        upcall_client_entry *up_client_entry = NULL;
        upcall_entry *up_entry = NULL;
        int32_t         op_errno = ENOMEM;
        upcall_local_t *local = NULL;

        local = upcall_local_init(frame, loc->gfid);
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }

        client = frame->root->client;
        
        gf_log (this->name, GF_LOG_INFO, "In truncate ");
        /* do we need to use loc->inode->gfid ?? */
        PROCESS_DELEG (frame, client, local->gfid,
                       _gf_true, &up_entry);

        STACK_WIND (frame, up_truncate_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->truncate,
                    loc, offset, xdata);

        return 0;

err:
        //op_errno = (op_errno == -1) ? errno : op_errno;
        UPCALL_STACK_UNWIND (truncate, frame, -1, op_errno, NULL, NULL, NULL);

        return 0;
}

int
up_setattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                 int op_ret, int op_errno, struct iatt *statpre,
                 struct iatt *statpost, dict_t *xdata)
{
        client_t         *client        = NULL;
        upcall_entry     *up_entry = NULL;
        uint32_t        flags;
        int notify = 0;
        upcall_client_entry * up_client_entry = NULL;
        notify_event_data n_event_data;
        upcall_local_t  *local = NULL;

        client = frame->root->client;
        local = frame->local;

        if (op_ret < 0) {
                goto out;
        }
        gf_log (this->name, GF_LOG_INFO, "In truncate_cbk ");
        // setattr -> UP_SIZE or UP_OWN or UP_MODE or UP_TIMES -> INODE_UPDATE (or UP_PERM esp incase of ACLs -> INODE_INVALIDATE)
        // Need to check what attr is changed and accordingly pass UP_FLAGS.
        flags = (UP_SIZE | UP_TIMES | UP_OWN | UP_MODE | UP_PERM) ;
        upcall_cache_invalidate (frame, client, local->gfid, &flags);

out:
        UPCALL_STACK_UNWIND (setattr, frame, op_ret, op_errno,
                             statpre, statpost, xdata);
        return 0;
}

int
up_setattr (call_frame_t *frame, xlator_t *this, loc_t *loc,
             struct iatt *stbuf, int32_t valid, dict_t *xdata)
{
        int ret = -1;
        client_t            *client        = NULL;
        dict_t              *dict                = NULL;
        char                 key[1024]            = {0};
        upcall_client_entry *up_client_entry = NULL;
        upcall_entry *up_entry = NULL;
        int32_t         op_errno = ENOMEM;
        upcall_local_t *local = NULL;

        local = upcall_local_init(frame, loc->gfid);
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }

        client = frame->root->client;
        
        gf_log (this->name, GF_LOG_INFO, "In setattr ");
        /* do we need to use loc->inode->gfid ?? */
        PROCESS_DELEG (frame, client, local->gfid,
                       _gf_true, &up_entry);

        STACK_WIND (frame, up_setattr_cbk,
                           FIRST_CHILD(this),
                           FIRST_CHILD(this)->fops->setattr,
                            loc, stbuf, valid, xdata);
        return 0;

err:
       // op_errno = (op_errno == -1) ? errno : op_errno;
        UPCALL_STACK_UNWIND (setattr, frame, -1, op_errno, NULL, NULL, NULL);

        return 0;
}

int
up_rename_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, struct iatt *stbuf,
                   struct iatt *preoldparent, struct iatt *postoldparent,
                   struct iatt *prenewparent, struct iatt *postnewparent,
                   dict_t *xdata)
{
        client_t         *client        = NULL;
        upcall_entry     *up_entry = NULL;
        uint32_t        flags;
        int notify = 0;
        upcall_client_entry * up_client_entry = NULL;
        notify_event_data n_event_data;
        upcall_local_t  *local = NULL;

        client = frame->root->client;
        local = frame->local;

        if (op_ret < 0) {
                goto out;
        }
        gf_log (this->name, GF_LOG_INFO, "In rename_cbk ");
        flags = (UP_RENAME);
        upcall_cache_invalidate (frame, client, local->gfid, &flags);

        /* Need to invalidate old and new parent entries as well */
        flags = (UP_TIMES);
        upcall_cache_invalidate (frame, client, preoldparent->ia_gfid, &flags);
        if (uuid_compare (preoldparent->ia_gfid, prenewparent->ia_gfid))
                upcall_cache_invalidate (frame, client, prenewparent->ia_gfid, &flags);

out:
        UPCALL_STACK_UNWIND (rename, frame, op_ret, op_errno,
                             stbuf, preoldparent, postoldparent,
                             prenewparent, postnewparent, xdata);
        return 0;
}

int
up_rename (call_frame_t *frame, xlator_t *this,
          loc_t *oldloc, loc_t *newloc, dict_t *xdata)
{
        int ret = -1;
        client_t            *client        = NULL;
        dict_t              *dict                = NULL;
        char                 key[1024]            = {0};
        upcall_client_entry *up_client_entry = NULL;
        upcall_entry *up_entry = NULL;
        int32_t         op_errno = ENOMEM;
        upcall_local_t *local = NULL;

        local = upcall_local_init(frame, oldloc->gfid);
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }

        client = frame->root->client;
        
        gf_log (this->name, GF_LOG_INFO, "In rename ");
        /* do we need to use loc->inode->gfid ?? */
        PROCESS_DELEG (frame, client, local->gfid,
                       _gf_true, &up_entry);

        STACK_WIND (frame, up_rename_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->rename,
                    oldloc, newloc, xdata);

        return 0;

err:
        //op_errno = (op_errno == -1) ? errno : op_errno;
        UPCALL_STACK_UNWIND (rename, frame, -1, op_errno, NULL, NULL, NULL, NULL,
                             NULL, NULL);

        return 0;
}

int
up_unlink_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                int op_ret, int op_errno, struct iatt *preparent,
                struct iatt *postparent, dict_t *xdata)
{
        client_t         *client        = NULL;
        upcall_entry     *up_entry = NULL;
        uint32_t        flags;
        int notify = 0;
        upcall_client_entry * up_client_entry = NULL;
        notify_event_data n_event_data;
        upcall_local_t  *local = NULL;

        client = frame->root->client;
        local = frame->local;

        if (op_ret < 0) {
                goto out;
        }
        gf_log (this->name, GF_LOG_INFO, "In unlink_cbk ");
        flags = (UP_NLINK | UP_TIMES) ;
        /* how do we get files' gfid?? */
        upcall_cache_invalidate (frame, client, local->gfid, &flags);
        flags = (UP_TIMES) ;
        /* invalidate parent's entry too */
        upcall_cache_invalidate (frame, client, postparent->ia_gfid, &flags);

out:
        UPCALL_STACK_UNWIND (unlink, frame, op_ret, op_errno,
                             preparent, postparent, xdata);
        return 0;
}

int
up_unlink (call_frame_t *frame, xlator_t *this, loc_t *loc, int xflag,
              dict_t *xdata)
{
        int ret = -1;
        client_t            *client        = NULL;
        dict_t              *dict                = NULL;
        char                 key[1024]            = {0};
        upcall_client_entry *up_client_entry = NULL;
        upcall_entry *up_entry = NULL;
        int32_t         op_errno = ENOMEM;
        upcall_local_t *local = NULL;

        local = upcall_local_init(frame, loc->gfid);
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }

        client = frame->root->client;
        
        gf_log (this->name, GF_LOG_INFO, "In unlink ");
        /* do we need to use loc->inode->gfid ?? */
        PROCESS_DELEG (frame, client, local->gfid,
                       _gf_true, &up_entry);

        STACK_WIND (frame, up_unlink_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->unlink,
                    loc, xflag, xdata);

        return 0;

err:
//        op_errno = (op_errno == -1) ? errno : op_errno;
        UPCALL_STACK_UNWIND (unlink, frame, -1, op_errno, NULL, NULL, NULL);

        return 0;
}

int
up_link_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                int op_ret, int op_errno, inode_t *inode, struct iatt *stbuf,
                struct iatt *preparent, struct iatt *postparent, dict_t *xdata)
{
        client_t         *client        = NULL;
        upcall_entry     *up_entry = NULL;
        uint32_t        flags;
        int notify = 0;
        upcall_client_entry * up_client_entry = NULL;
        notify_event_data n_event_data;
        upcall_local_t  *local = NULL;

        client = frame->root->client;
        local = frame->local;

        if (op_ret < 0) {
                goto out;
        }
        gf_log (this->name, GF_LOG_INFO, "In link_cbk ");
        flags = (UP_NLINK | UP_TIMES) ;
        /* how do we get files' gfid?? */
        upcall_cache_invalidate (frame, client, local->gfid, &flags);

        /* do we need to update parent as well?? */
out:
        UPCALL_STACK_UNWIND (link, frame, op_ret, op_errno,
                             inode, stbuf, preparent, postparent, xdata);
        return 0;
}

int
up_link (call_frame_t *frame, xlator_t *this, loc_t *oldloc,
         loc_t *newloc, dict_t *xdata)
{
        int ret = -1;
        client_t            *client        = NULL;
        dict_t              *dict                = NULL;
        char                 key[1024]            = {0};
        upcall_client_entry *up_client_entry = NULL;
        upcall_entry *up_entry = NULL;
        int32_t         op_errno = ENOMEM;
        upcall_local_t *local = NULL;

        local = upcall_local_init(frame, oldloc->gfid);
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }

        client = frame->root->client;
        
        gf_log (this->name, GF_LOG_INFO, "In link ");
        /* do we need to use loc->inode->gfid ?? */
        PROCESS_DELEG (frame, client, local->gfid,
                       _gf_true, &up_entry);

        STACK_WIND (frame, up_link_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->link,
                    oldloc, newloc, xdata);

        return 0;

err:
//        op_errno = (op_errno == -1) ? errno : op_errno;
        UPCALL_STACK_UNWIND (link, frame, -1, op_errno, NULL, NULL, NULL, NULL, NULL);

        return 0;
}

int
up_rmdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                int op_ret, int op_errno, struct iatt *preparent,
                struct iatt *postparent, dict_t *xdata)
{
        client_t         *client        = NULL;
        upcall_entry     *up_entry = NULL;
        uint32_t        flags;
        int notify = 0;
        upcall_client_entry * up_client_entry = NULL;
        notify_event_data n_event_data;
        upcall_local_t  *local = NULL;

        client = frame->root->client;
        local = frame->local;

        if (op_ret < 0) {
                goto out;
        }
        gf_log (this->name, GF_LOG_INFO, "In rmdir_cbk ");
        flags = (UP_NLINK | UP_TIMES) ;
        /* how do we get files' gfid?? */
        upcall_cache_invalidate (frame, client, local->gfid, &flags);
        flags = (UP_TIMES) ;
        /* invalidate parent's entry too */
        upcall_cache_invalidate (frame, client, postparent->ia_gfid, &flags);

out:
        UPCALL_STACK_UNWIND (rmdir, frame, op_ret, op_errno,
                             preparent, postparent, xdata);
        return 0;
}

int
up_rmdir (call_frame_t *frame, xlator_t *this, loc_t *loc, int flags,
              dict_t *xdata)
{
        int ret = -1;
        client_t            *client        = NULL;
        dict_t              *dict                = NULL;
        char                 key[1024]            = {0};
        upcall_client_entry *up_client_entry = NULL;
        upcall_entry *up_entry = NULL;
        int32_t         op_errno = ENOMEM;
        upcall_local_t *local = NULL;

        local = upcall_local_init(frame, loc->gfid);
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }

        client = frame->root->client;
        
        gf_log (this->name, GF_LOG_INFO, "In rmdir ");
        /* do we need to use loc->inode->gfid ?? */
        PROCESS_DELEG (frame, client, local->gfid,
                       _gf_true, &up_entry);

        STACK_WIND (frame, up_rmdir_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->rmdir,
                    loc, flags, xdata);

        return 0;

err:
        //op_errno = (op_errno == -1) ? errno : op_errno;
        UPCALL_STACK_UNWIND (rmdir, frame, -1, op_errno, NULL, NULL, NULL);

        return 0;
}

int
up_mkdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                int op_ret, int op_errno, inode_t *inode,
                struct iatt *stbuf, struct iatt *preparent,
                struct iatt *postparent, dict_t *xdata)
{
        client_t         *client        = NULL;
        upcall_entry     *up_entry = NULL;
        uint32_t        flags;
        int notify = 0;
        upcall_client_entry * up_client_entry = NULL;
        notify_event_data n_event_data;
        upcall_local_t  *local = NULL;

        client = frame->root->client;
        local = frame->local;

        if (op_ret < 0) {
                goto out;
        }
        gf_log (this->name, GF_LOG_INFO, "In mkdir_cbk ");
        flags = (UP_NLINK | UP_TIMES) ;
        /* how do we get files' gfid?? */
        upcall_cache_invalidate (frame, client, local->gfid, &flags);
        flags = (UP_TIMES) ;
        /* invalidate parent's entry too */
        upcall_cache_invalidate (frame, client, postparent->ia_gfid, &flags);

out:
        UPCALL_STACK_UNWIND (mkdir, frame, op_ret, op_errno,
                             inode, stbuf, preparent, postparent, xdata);
        return 0;
}

int
up_mkdir (call_frame_t *frame, xlator_t *this,
          loc_t *loc, mode_t mode, mode_t umask, dict_t *params)
{
        int ret = -1;
        client_t            *client        = NULL;
        dict_t              *dict                = NULL;
        char                 key[1024]            = {0};
        upcall_client_entry *up_client_entry = NULL;
        upcall_entry *up_entry = NULL;
        int32_t         op_errno = ENOMEM;
        upcall_local_t *local = NULL;

        local = upcall_local_init(frame, loc->gfid);
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }

        client = frame->root->client;
        
        gf_log (this->name, GF_LOG_INFO, "In mkdir ");

        /* no need of delegation recall as we do not support
         * directory delegations 
        PROCESS_DELEG (frame, client, local->gfid,
                       _gf_true, &up_entry); */

        STACK_WIND (frame, up_mkdir_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->mkdir,
                    loc, mode, umask, params);

        return 0;

err:
        //op_errno = (op_errno == -1) ? errno : op_errno;
        UPCALL_STACK_UNWIND (mkdir, frame, -1, op_errno, NULL, NULL, NULL, NULL, NULL);

        return 0;
}

int
up_create_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                int op_ret, int op_errno, fd_t *fd, inode_t *inode,
                struct iatt *stbuf, struct iatt *preparent,
                struct iatt *postparent, dict_t *xdata)
{
        client_t         *client        = NULL;
        upcall_entry     *up_entry = NULL;
        uint32_t        flags;
        int notify = 0;
        upcall_client_entry * up_client_entry = NULL;
        notify_event_data n_event_data;
        upcall_local_t  *local = NULL;

        client = frame->root->client;
        local = frame->local;

        if (op_ret < 0) {
                goto out;
        }
        gf_log (this->name, GF_LOG_INFO, "In create_cbk ");
        flags = (UP_NLINK | UP_TIMES) ;
        /* how do we get files' gfid?? */
        upcall_cache_invalidate (frame, client, local->gfid, &flags);
        flags = (UP_TIMES) ;
        /* invalidate parent's entry too */
        upcall_cache_invalidate (frame, client, postparent->ia_gfid, &flags);

out:
        UPCALL_STACK_UNWIND (create, frame, op_ret, op_errno, fd,
                             inode, stbuf, preparent, postparent, xdata);
        return 0;
}

int
up_create (call_frame_t *frame, xlator_t *this,
          loc_t *loc, int32_t flags, mode_t mode,
          mode_t umask, fd_t * fd, dict_t *params)
{
        int ret = -1;
        client_t            *client        = NULL;
        dict_t              *dict                = NULL;
        char                 key[1024]            = {0};
        upcall_client_entry *up_client_entry = NULL;
        upcall_entry *up_entry = NULL;
        int32_t         op_errno = ENOMEM;
        upcall_local_t *local = NULL;

        local = upcall_local_init(frame, loc->gfid);
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }

        client = frame->root->client;
        
        gf_log (this->name, GF_LOG_INFO, "In create ");

        /* no need of delegation recall as we do not support
         * directory delegations 
        PROCESS_DELEG (frame, client, local->gfid,
                       _gf_true, &up_entry); */

        STACK_WIND (frame, up_create_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->create,
                    loc, flags, mode, umask, fd, params);

        return 0;

err:
//        op_errno = (op_errno == -1) ? errno : op_errno;
        UPCALL_STACK_UNWIND (create, frame, -1, op_errno, NULL, NULL, NULL, NULL, NULL, NULL);
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

void
upcall_local_wipe (xlator_t *this, upcall_local_t *local)
{
        if (local)
                mem_put (local);
}

upcall_local_t *
upcall_local_init (call_frame_t *frame, uuid_t gfid)
{
        upcall_local_t *local = NULL;
        local = mem_get0 (THIS->local_pool);
        if (!local)
                goto out;
        uuid_copy (local->gfid, gfid);
        local->is_delegation_enabled = _gf_false;
        frame->local = local;
out:
        return local;
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
        this->local_pool = mem_pool_new (upcall_local_t, 512);
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
        .open        = up_open, // do we need to send invalidate flags
        .readv       = up_readv,
        .writev      = up_writev,
        .truncate    = up_truncate,
//        .ftruncate   = up_ftruncate, //reqd??
        .lk          = up_lk,
        .setattr     = up_setattr,
        .rename      = up_rename,
        .unlink      = up_unlink, /* invalidate both file and parent dir */
        .rmdir       = up_rmdir, /* same as above */
        .link        = up_link, /* invalidate both file and parent dir */
        .create      = up_create, /* may be needed to update dir inode entry */
        .mkdir       = up_mkdir, /* for the same reason as above */
//        .opendir        = up_opendir, // do we need to send invalidate flags
//        .setxattr    = up_setxattr, /* ?? */
//        .removexattr    = up_removexattr, /* ?? */
//        .mknod       = up_mknod,
//        .symlink     = up_symlink, /* invalidate both file and parent dir maybe */
//        .flush       = up_flush,

//        .remove      = up_remove, /* Not present */
#ifdef WIP
        .getattr     = up_getattr, /* ?? */
        .getxattr    = up_getxattr, /* ?? */
        .access      = up_access,
        .lookup      = uo_lookup,
        .symlink     = up_symlink, /* invalidate both file and parent dir maybe */
        .readlink    = up_readlink, /* Needed? readlink same as read? */
        .readdirp    = up_readdirp,
        .readdir     = up_readdir,
/*  other fops to be considered -
 *   lookup, stat, opendir, readdir, readdirp, readlink, mknod, statfs, flush,
 *   fsync
 */ 
#endif
};

struct xlator_cbks cbks = {
};

struct volume_options options[] = {
        { .key = {NULL} },
};
