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

int check_dbus_config()
{
return 1;
}


int create_export_config()
{
return 1;
}

int  start_ganesha()
{
return 1;
}

int stop_ganesha()
{
return 1;
}

int ganesha_add_export()
{

runner_t                runner                     = {0,};
int ret = -1;

runinit (&runner);

runner_add_args (&runner, "/usr/local/bin/ganesha.nfsd",
                         "-L", "/nfs-ganesha-op.log",
                         "-f","/root/nfs-ganesha.conf","-N", "NIV_FULL_DEBUG",NULL);
ret = runner_run_nowait(&runner);

return ret;
}


/*int ganesha_add_export()
{
        int ret = -1;
        ret = ganesha_add_export();
        gf_log("",GF_LOG_INFO,"ganesha starts returns %d",ret);
        return ret;
}
*/
int ganesha_remove_export()
{
return 1;
}

int ganesha_export_entry( char *volname, char **op_errstr)
{
        int ret = -1;

        ret = create_export_config(volname);
        ret =  start_ganesha();
        ret =  ganesha_add_export();
        return ret;
}



int32_t
glusterd_check_if_ganesha_trans_enabled (glusterd_volinfo_t *volinfo)
{
        int32_t  ret           = 0;
        int      flag          = _gf_false;

        flag = glusterd_volinfo_get_boolean (volinfo, "features.ganesha");
        if (flag == -1) {
                gf_log ("", GF_LOG_ERROR, "failed to ganesha status");
                ret = -1;
                goto out;
        }

        if (flag == _gf_false) {
                ret = -1;
                goto out;
        }
        ret = 0;
out:
        return ret;
}




int glusterd_handle_ganesha_op(dict_t *dict, char **op_errstr,char *key)
{

        glusterd_volinfo_t     *volinfo      = NULL;
        int32_t                 ret          = -1;
        char                   *volname      = NULL;
        int                     type         = -1;
        xlator_t               *this         = NULL;
        static int             export_id    = 1;
        char *option =  NULL;

        GF_ASSERT(dict);
        GF_ASSERT(op_errstr);
        //gf_asprintf(op_errstr, "ganesha volume failed");
       //return -1;
        ret = dict_get_str (dict, "volname", &volname);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Unable to get volume name");
                goto out;
        }


        switch (1)
        {
                case 1 :
                        ret =  ganesha_export_entry(volname,op_errstr);

                        if ( ret < 0 )
                                goto out;
                        break;
                default:
                        gf_asprintf(op_errstr, "not a valid option");
                        ret = -1;
                        break;


        }

out :

return ret;

}


