/**
 * (C) Copyright 2018 Intel Corporation.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
 * The Government's rights to use, modify, reproduce, release, perform, display,
 * or disclose this software are subject to the terms of the Apache License as
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 */

#ifndef __VEA_INTERNAL_H__
#define __VEA_INTERNAL_H__

#include <gurt/list.h>
#include <gurt/heap.h>
#include <daos/mem.h>
#include <daos/btree.h>
#include <daos_srv/vea.h>

#define VEA_MAGIC	(0xea201804)

/* Per I/O stream hint context */
struct vea_hint_context {
	struct vea_hint_df	*vhc_pd;
	/* In-memory hint block offset */
	uint64_t		 vhc_off;
	/* In-memory hint sequence */
	uint64_t		 vhc_seq;
};

/* Free extent informat stored in the in-memory compound free extent index */
struct vea_entry {
	struct vea_free_extent	ve_ext;
	/* Link to vfc_heap */
	struct d_binheap_node	ve_node;
	/* Link to one of vfc_lrus or vsi_agg_lru */
	d_list_t		ve_link;
	uint32_t		ve_in_heap:1;
};

#define VEA_LARGE_EXT_MB	64	/* Large extent threashold in MB */
#define VEA_HINT_OFF_INVAL	0	/* Inavlid hint offset */
#define VEA_MIGRATE_INTVL	10	/* Seconds */

struct free_ext_cursor {
	struct vea_entry	*fec_cur;
	int			 fec_idx;
	int			 fec_entry_cnt;
	struct vea_entry	*fec_entries[0];
};

/*
 * Large free extents (>=VEA_LARGE_EXT_MB) are tracked in max a heap, small
 * free extents (< VEA_LARGE_EXT_MB) are tracked in size categorized LRUs
 * respectively.
 */
struct vea_free_class {
	/* Max heap for tracking the largest free extent */
	struct d_binheap	 vfc_heap;
	/* Size threshold for large extent */
	uint32_t		 vfc_large_thresh;
	/* How many size classed LRUs for small free extents */
	uint32_t		 vfc_lru_cnt;
	/* Extent size classed LRU lists  */
	d_list_t		*vfc_lrus;
	/*
	 * Upper size (in blocks) bounds for all size classes. The lower size
	 * bound of the size class is the upper bound of previous class (0 for
	 * first class), so the size of each extent in a size class satisfies:
	 * vfc_sizes[i + 1] < blk_cnt <= vfc_sizes[i].
	 */
	uint32_t		*vfc_sizes;
	/*
	 * Cursor used to scan the size classed LRUs when trying to reserve
	 * from small extents.
	 */
	struct free_ext_cursor	*vfc_cursor;
};

/* In-memory compound index */
struct vea_space_info {
	/* Instance for the pmemobj pool on SCM */
	struct umem_instance	*vsi_umem;
	/* Free space information stored on SCM */
	struct vea_space_df	*vsi_md;
	/* Open handles for the persistent free extent tree */
	daos_handle_t		 vsi_md_free_btr;
	/* Open handles for the persistent extent vector tree */
	daos_handle_t		 vsi_md_vec_btr;
	/* Free extent tree sorted by offset, for all free extents. */
	daos_handle_t		 vsi_free_btr;
	/* Extent vector tree, for non-contiguous allocation */
	daos_handle_t		 vsi_vec_btr;
	/* Reserved blocks in total */
	uint64_t		 vsi_tot_resrvd;
	/* Index for searching free extent by size & age */
	struct vea_free_class	 vsi_class;
	/* LRU to aggergate just recent freed extents */
	d_list_t		 vsi_agg_lru;
	/*
	 * Free extent tree sorted by offset, for coalescing the just recent
	 * free extents.
	 */
	daos_handle_t		 vsi_agg_btr;
	/* Last aggregation time */
	uint64_t		 vsi_agg_time;
	/* Unmap context to performe unmap against freed extent */
	struct vea_unmap_context	vsi_unmap_ctxt;
};

static inline bool ext_is_idle(struct vea_free_extent *vfe)
{
	return vfe->vfe_age == VEA_EXT_AGE_MAX;
}

static inline int get_current_age(uint64_t *age)
{
	struct timespec now;
	int rc;

	rc = clock_gettime(CLOCK_MONOTONIC_COARSE, &now);
	if (rc != 0)
		return rc;
	*age = now.tv_sec;

	return 0;
}

enum vea_free_flags {
	VEA_FL_NO_MERGE		= (1 << 0),
	VEA_FL_GEN_AGE		= (1 << 1),
};

/* vea_init.c */
void destroy_free_class(struct vea_free_class *vfc);
int create_free_class(struct vea_free_class *vfc, struct vea_space_df *md);
void unload_space_info(struct vea_space_info *vsi);
int load_space_info(struct vea_space_info *vsi);

/* vea_util.c */
int verify_free_entry(uint64_t *off, struct vea_free_extent *vfe);
int verify_vec_entry(uint64_t *off, struct vea_ext_vector *vec);
int ext_adjacent(struct vea_free_extent *cur, struct vea_free_extent *next);
int verify_resrvd_ext(struct vea_resrvd_ext *resrvd);
int vea_dump(struct vea_space_info *vsi, bool transient);
int vea_verify_alloc(struct vea_space_info *vsi, bool transient,
		     uint64_t off, uint32_t cnt);

/* vea_alloc.c */
void free_class_remove(struct vea_free_class *vfc, struct vea_entry *entry);
int compound_vec_alloc(struct vea_space_info *vsi, struct vea_ext_vector *vec);
int reserve_hint(struct vea_space_info *vsi, uint32_t blk_cnt,
		 struct vea_resrvd_ext *resrvd);
int reserve_large(struct vea_space_info *vsi, uint32_t blk_cnt,
		  struct vea_resrvd_ext *resrvd);
int reserve_small(struct vea_space_info *vsi, uint32_t blk_cnt,
		  struct vea_resrvd_ext *resrvd);
int reserve_vector(struct vea_space_info *vsi, uint32_t blk_cnt,
		   struct vea_resrvd_ext *resrvd);
int persistent_alloc(struct vea_space_info *vsi, struct vea_free_extent *vfe);

/* vea_free.c */
int compound_free(struct vea_space_info *vsi, struct vea_free_extent *vfe,
		  unsigned int flags);
int persistent_free(struct vea_space_info *vsi, struct vea_free_extent *vfe);
int aggregated_free(struct vea_space_info *vsi, struct vea_free_extent *vfe);
void migrate_free_exts(struct vea_space_info *vsi);

/* vea_hint.c */
void hint_get(struct vea_hint_context *hint, uint64_t *off);
void hint_update(struct vea_hint_context *hint, uint64_t off, uint64_t *seq);
int hint_cancel(struct vea_hint_context *hint, uint64_t off, uint64_t seq_min,
		uint64_t seq_max);
int hint_tx_publish(struct vea_hint_context *hint, uint64_t off,
		    uint64_t seq_min, uint64_t seq_max);

#endif /* __VEA_INTERNAL_H__ */
