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

#include "lkowner.h"

struct _upcalls_private_t {
	int client_id;
};
typedef struct _upcalls_private_t upcalls_private_t;

#endif /* __UPCALLS_INFRA_H__ */
