/**
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the GNU Lesser General Public License
 * (LGPL) version 2.1 which accompanies this distribution, and is available at
 * http://www.gnu.org/licenses/lgpl-2.1.html
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 * GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
 * The Government's rights to use, modify, reproduce, release, perform, display,
 * or disclose this software are subject to the terms of the LGPL License as
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 *
 * (C) Copyright 2016 Intel Corporation.
 */
/**
 * Implementation for pool specific functions in VOS
 *
 * vos/src/vos_pool.c
 *
 * Author: Vishwanath Venkatesan <vishwanath.venkatesan@intel.com>
 */
#include <daos_srv/vos.h>
#include <vos_layout.h>
#include <daos/daos_errno.h>
#include <daos/daos_common.h>

/**
 * Create a Versioning Object Storage Pool (VOSP) and its root object.
 *
 */
int
vos_pool_create(const char *path, uuid_t uuid, daos_size_t size,
		daos_handle_t *poh, daos_event_t *ev)
{
	int		 rc    = 0;
	vos_pool_t	*vpool = NULL;
	vos_pool_root_t	*root  = NULL;
	size_t		 root_size;

	if (NULL == path || uuid_is_null(uuid) || size <= 0)
		return DER_INVAL;

	vpool = (vos_pool_t *)malloc(sizeof(vos_pool_t));
	if (NULL == vpool) {
		D_ERROR("Error allocating vpool handle");
		return DER_NOMEM;
	}

	vpool->path = strdup(path);
	vpool->ph = pmemobj_create(path, POBJ_LAYOUT_NAME(vos_pool_layout),
							  size, 0666);
	if (NULL == vpool->ph) {
		D_ERROR("Failed to create pool\n");
		return DER_NOSPACE;
	}

	TOID(struct vos_pool_root) proot;
	proot = POBJ_ROOT(vpool->ph, struct vos_pool_root);
	root = D_RW(proot);
	root_size = pmemobj_root_size(vpool->ph);

	TX_BEGIN(vpool->ph) {
		root->vpr_magic = 0;
		uuid_copy(root->vos_pool_id, uuid);
		/* TODO: Yet to identify compatibility and
		   incompatibility flags */
		root->vpr_compat_flags = 0;
		root->vpr_incompat_flags = 0;
		/* This will eventually call the
		   constructor of container table */
		root->container_index_table =
			TOID_NULL(struct vos_container_table);
		root->vos_pool_info.pif_ncos = 0;
		root->vos_pool_info.pif_nobjs = 0;
		root->vos_pool_info.pif_size = size;
		root->vos_pool_info.pif_avail = size - root_size;
	}
	TX_END

	/* TODO: Clarify where pool handle to be maintained */
	/* If to be maintained at the VOS need to create a hash to convert
	   generic handle to cookie */
	/* Temporarily assigning value of the PMEMobjpool as handle */
	poh->cookie = (uintptr_t)vpool;
	return rc;
}

/**
 * Destroy a Versioning Object Storage Pool (VOSP)
 * and revoke all its handles
 *
 */
int
vos_pool_destroy(daos_handle_t poh, daos_event_t *ev)
{

	int		 rc    = 0;
	vos_pool_t	*vpool = NULL;

	/*TODO: Change to fetch this handle from a hash-table*/
	if (!poh.cookie) {
		D_ERROR("Invalid handle for pool");
		return DER_INVAL;
	}

	vpool = (vos_pool_t *)poh.cookie;
	if (NULL == vpool) {
		D_ERROR("Invalid handle for pool");
		return DER_NO_HDL;
	}

	pmemobj_close(vpool->ph);
	rc = remove(vpool->path);
	if (rc)
		return rc;

	/*TODO delete handle entry in the hash-table*/
	if (NULL != vpool->path)
		free(vpool->path);
	free(vpool);
	vpool = NULL;

	return rc;
}

/**
 * Open a Versioning Object Storage Pool (VOSP), load its root object
 * and other internal data structures.
 *
 */
int
vos_pool_open(const char *path, uuid_t uuid, daos_handle_t *poh,
	      daos_event_t *ev)
{

	int		 rc    = 0;
	vos_pool_t	*vpool = NULL;
	vos_pool_root_t *root  = NULL;
	char		 pool_uuid_str[37], uuid_str[37];

	if (NULL == path)
		return DER_INVAL;

	/* Create a new handle during open */
	vpool = (vos_pool_t *)malloc(sizeof(vos_pool_t));
	if (NULL == vpool) {
		D_ERROR("Error allocating vpool handle");
		return DER_NOMEM;
	}

	vpool->path = strdup(path);
	vpool->ph = pmemobj_open(path, POBJ_LAYOUT_NAME(vos_pool_layout));
	if (NULL == vpool->ph) {
		D_ERROR("Error in opening the pool handle");
		if (NULL != vpool)
			free(vpool);
		return  DER_NO_HDL;
	}

	TOID(struct vos_pool_root) proot;
	proot = POBJ_ROOT(vpool->ph, struct vos_pool_root);
	root = D_RW(proot);
	if (uuid_compare(uuid, root->vos_pool_id)) {
		uuid_unparse(uuid, pool_uuid_str);
		uuid_unparse(uuid, uuid_str);
		D_ERROR("UUID mismatch error (uuid: %s, vpool_id: %s",
			uuid_str, pool_uuid_str);
		if (NULL != vpool)
			free(vpool);
		return DER_INVAL;
	}
	/* TODO: Clarify where pool handle to be maintained */
	/* If to be maintained at the VOS need to create a hash to convert
	   generic handle to cookie */
	/* Temporarily assigning value of the PMEMobjpool as handle */
	poh->cookie = (uintptr_t)vpool;
	return rc;
}

/**
 * close a VOSP, all opened containers sharing this pool handle
 * will be revoked.
 */
int
vos_pool_close(daos_handle_t poh, daos_event_t *ev)
{

	int		 rc    = 0;
	vos_pool_t	*vpool = NULL;

	/*TODO: Change to fetch this handle from a hash-table*/
	if (!poh.cookie) {
		D_ERROR("Invalid handle for pool");
		return DER_INVAL;
	}

	vpool = (vos_pool_t *)poh.cookie;
	if (NULL == vpool) {
		D_ERROR("No handle available for vpool");
		return DER_NO_HDL;
	}

	pmemobj_close(vpool->ph);
	/*TODO delete handle entry in the hash-table*/
	if (NULL != vpool->path)
		free(vpool->path);
	free(vpool);
	vpool = NULL;

	return rc;
}

/**
 * Query attributes and statistics of the current pool
 *
 */
int
vos_pool_query(daos_handle_t poh, vos_pool_info_t *pinfo, daos_event_t *ev)
{

	int		 rc    = 0;
	vos_pool_t	*vpool = NULL;
	vos_pool_root_t *root  = NULL;

	/*TODO: Change to fetch this handle from a hash-table*/
	if (!poh.cookie) {
		D_ERROR("Invalid handle for pool");
		return DER_INVAL;
	}

	vpool = (vos_pool_t *)poh.cookie;
	if (NULL == vpool) {
		D_ERROR("No handle available for vpool");
		return DER_NO_HDL;
	}

	TOID(struct vos_pool_root) proot;
	proot = POBJ_ROOT(vpool->ph, struct vos_pool_root);
	root = D_RW(proot);
	pinfo->pif_ncos = root->vos_pool_info.pif_ncos;
	pinfo->pif_nobjs = root->vos_pool_info.pif_nobjs;
	pinfo->pif_size = root->vos_pool_info.pif_size;
	pinfo->pif_avail = root->vos_pool_info.pif_avail;

	return rc;
}
