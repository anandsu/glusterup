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


int create_export_config(char *volname)
{
runner_t                runner                     = {0,};
int ret = -1;
runinit (&runner);
gf_log("",GF_LOG_INFO,"running create_EXPORT");
runner_add_args (&runner, "sh", "/etc/ganesha/create_export.sh",volname,NULL);
ret = runner_run_nowait(&runner);
return 1;
}

int  ganesha_add_export(char *volname)
{
runner_t runner = {0,};
int ret = -1;
runinit (&runner);
create_export_config(volname);
gf_log("",GF_LOG_INFO,"runing dbus send");
runner_add_args (&runner, "sh", "/etc/ganesha/dbus-send.sh", "add", volname ,NULL);
ret = runner_run_nowait(&runner);
return ret;
}

int tear_down_cluster(dict_t *dict)
{
int ret = -1;
runner_t runner = {0,};
if (is_origin_glusterd(dict))
{
gf_log ( "",GF_LOG_INFO,"before teardown");
runinit (&runner);
runner_add_args (&runner, "sh","/etc/ganesha/ganesha-ha.sh","teardown",NULL);
ret = runner_run_nowait(&runner);
return ret;
}
return 1;
}

int setup_cluster(dict_t *dict)
{
int ret = -1;
runner_t runner = {0,};
if (is_origin_glusterd(dict))
{
        gf_log ("",GF_LOG_INFO, "I am originator glusterd");
       runinit (&runner);
       runner_add_args (&runner, "sh","/etc/ganesha/ganesha-ha.sh","setup",NULL);

ret =  runner_run_nowait(&runner);
return ret;
}
return 1;
}


int stop_ganesha(dict_t *dict,char **op_errstr)
{
runner_t                runner                     = {0,};
int ret = -1;
tear_down_cluster(dict);
runinit (&runner);
runner_add_args (&runner, "pkill", "ganesha.nfsd",NULL);
ret = runner_run_nowait(&runner);
return ret;
}

int start_ganesha(dict_t *dict, glusterd_volinfo_t *volinfo)
{

runner_t                runner                     = {0,};
int ret = -1;
char key[1024] = {0,};
long int i =1;
dict_t *vol_opts =  NULL;
glusterd_volinfo_t *volinfo1 = NULL;
int count =0;
dict_t *dict1 = NULL;
char *volname =  NULL;
glusterd_conf_t *priv = NULL;

priv =  THIS->private;
GF_ASSERT(priv);

dict1 = dict_new();
if (!dict)
        goto out;

//vol_opts = volinfo->dict;

/*ret = dict_get_int32 (dict, key, &volcount);
gf_log ("", GF_LOG_INFO, "number of volumes is %d", volcount);
        if (ret) {
                gf_log ("", GF_LOG_INFO, "failed to get volcount");
                goto out;
        }
        if (volcount <= 0) {
                ret = -1;
                goto out;
        }

for (i = 1; i <= volcount; i++) {
                snprintf (key, sizeof (key), "volname%ld", i);
                ret = dict_get_str (dict, key, &volname);
                if (ret) {
                        gf_log ("", GF_LOG_ERROR, "failed to get the "
                                "volname");
                        goto out;
                }

                ret = glusterd_volinfo_find (volname, &volinfo1);
                if (ret) {
                        gf_log ("", GF_LOG_ERROR, "volinfo for %s "
                                "not found", volname);
                        goto out;
                }
                vol_opts = volinfo1->dict;
                ret = dict_set_str(vol_opts, "nfs.disable","on");
}
*/
//runinit (&runner);
        list_for_each_entry(volinfo1,&priv->volumes, vol_list) {
                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "volume%d", count);
                ret = dict_set_str (dict1, key, volinfo1->volname);
                if (ret)
                        goto out;
                vol_opts = volinfo1->dict;
                ret = dict_set_str(vol_opts, "nfs.disable","on");

                count++;

        }

glusterd_nfs_server_stop();
runinit(&runner);
runner_add_args (&runner, "/usr/bin/ganesha.nfsd",
                         "-L", "/nfs-ganesha-op.log",
                         "-f","/etc/ganesha/nfs-ganesha.conf","-N", "NIV_FULL_DEBUG",NULL);
ret =  runner_run_nowait(&runner);
ret = setup_cluster(dict);
out : return ret;
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

int ganesha_export_entry( char *volname, char **op_errstr,dict_t *dict, glusterd_volinfo_t *volinfo)
{
        int ret = -1;

     //   ret = create_export_config(volname);
      //  ret =  start_ganesha(volname);
       // ret =  ganesha_add_export(dict,volinfo);
        return 1;
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




int glusterd_handle_ganesha_op(dict_t *dict, char **op_errstr,char *key,char *value, glusterd_volinfo_t *volinfo)
{

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


  /*      switch (1)
        {
                case 1 :
                        ret =  ganesha_export_entry(volname,op_errstr,dict,volinfo);

                        if ( ret < 0 )
                                goto out;
                        break;
                default:
                        gf_asprintf(op_errstr, "not a valid option");
                        ret = -1;
                        break;


        }
*/
        gf_log ( "",GF_LOG_INFO,"the value is %s ",value);
        if (strcmp (key, "ganesha.enable") == 0)
        {
        if (strcmp (value,"on") == 0)
        {
                ret =  ganesha_add_export(volname);

                        if ( ret < 0 )
                                goto out;
        }

        else
        {
                ret = stop_ganesha (dict,op_errstr);
                        if ( ret < 0)
                                goto out;
        }
        }
        if ( strcmp (key, "features.ganesha") == 0)
        {
               ret =  start_ganesha(dict,volinfo);

                        if ( ret < 0 )
                                goto out;
 
        }

out :

return ret;

}


