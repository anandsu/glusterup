/*
   Copyright (c) 2013 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#include <sys/uio.h>

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "xlator.h"
#include "defaults.h"
#include "logging.h"

#include "ganesha.h"
#include "ganesha-mem-types.h"



int32_t
init (xlator_t *this)
{
      int         ret      = -1;
      /*ganesha_priv_t  *priv= NULL;
      priv = GF_CALLOC (1, sizeof (*priv),gf_ganesha_mt_priv_t);
        if (!priv)
        {
                ret=-1;
                gf_log(this->name, GF_LOG_ERROR, "Error priv");
                goto out;
        }
        this->private = priv;
        ret = 0;
out:
        if(ret){
        GF_FREE(priv)
}
}

        GF_VALIDATE_OR_GOTO ("ganesha", this, err);
        */

        if (!this->children || this->children->next) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Need subvolume == 1");
                goto err;
        }

        if (!this->parents) {
                gf_log (this->name, GF_LOG_WARNING,
                        "Dangling volume. Check volfile");
        }


        if( 1 ) {
                gf_log (this->name, GF_LOG_DEBUG, "ganesha debug option turned on");
        }
err:
return 0;
}


int ganesha_start()
{
        gf_log ("", GF_LOG_INFO,"STARTING GANESHA");
        return 1;
}

/*int glusterd_handle_ganesha_op(dict_t *dict, char **op_errstr)
{
        GF_ASSERT(dict);
        GF_ASSERT(op_errstr);
       gf_asprintf(op_errstr, "ganesha volume failed");
       return -1;
}
*/

void
fini (xlator_t *this)
{
        return;
}

struct xlator_fops fops = {
};

struct xlator_cbks cbks = {
};

struct volume_options options[] = {
        { .key  = {"ganesha.enable"},
          .default_value = "off",
          .type = GF_OPTION_TYPE_ANY,
          .description = "export the volume in question via ganesha"
        },
        { .key  = {NULL}
        },
};
