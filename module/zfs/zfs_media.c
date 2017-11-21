/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright 2010 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */
/*
 * Copyright (c) 2012 by Delphix. All rights reserved.
 */

/*
 * This file contains the code to implement file range locking in
 * ZFS, although there isn't much specific to ZFS (all that comes to mind is
 * support for growing the blocksize).
 */

#include <sys/zfs_media.h>
#include <linux/slab.h>

/*
 *              0 1 3 2            -1
 *              | | | |             |
 *              v v v v             v
 * head -> ( ) -> (   ) -> ( ) -> NULL
 * Position of the offset in the list
 * 0 : between the 2 nodes
 * 1 : Equal with the start of the node
 * 2 : Equal with the end of the node
 * 3 : Inside the node
 * -1 : when it is after the last node. For partial reads mostly.
 */
medium_t *
find_in(struct list_head *head, medium_t *start, bool contin, loff_t posi, int *ret) {

    medium_t *where, *n, *pos;
    where = NULL;
    if (start == NULL)
        return NULL;
    if (contin)
        pos = list_next_entry(start, list);
    else
        pos = list_first_entry(head, typeof(*where), list);
    n = list_next_entry(pos, list);
    do {
        where = pos;
        if (posi > pos->m_end) {
            pos = n;
            n = list_next_entry(n, list);
            continue;
        }
        if (posi < pos->m_start) {
            *ret = 0;
            return where;
        }
        if (posi == pos->m_end) {
            *ret = 1;
            return where;
        }
        if (posi == pos->m_start) {
            *ret = 2;
            return where;
        }
        *ret = 3;
        return where;
    } while (&pos->list != (head));
    return NULL;
}

/* Interval list of medium of part of file */
/* Debug of this will be done as we do experiments */
medium_t *
zfs_media_add(struct list_head *dn, loff_t ppos, size_t len, int8_t rot)
{
    int start, stop;
    medium_t *new, *loop, *next, *del;
    struct medium *posh, *nh;
    loff_t end = ppos + len;
    loop = new = del = next = NULL;
    start = stop = -1;

    new = kzalloc(sizeof(medium_t), GFP_KERNEL);
    if (new == NULL) {
        printk(KERN_EMERG "[ERROR][ZFS_MEDIA_ADD]Cannot allocate for new medium\n");
        return NULL;
    }
    new->m_start = ppos;
    new->m_end = end;
    new->m_type = rot;
    if (list_empty(dn)) {
        list_add_tail(&new->list, dn);
        return new;
    }

    /* Find where the begining of this part is supposed to be added.*/
    loop = find_in(dn, list_first_entry(dn, typeof(*new), list), false, ppos, &start);

    /* Probably on read. Reading 10 first and then 10 last lines.
     * Accessing not sequencial parts of file.*/
    if (loop == NULL) {
        list_add_tail(&new->list, dn);
        return new;
    }

    /* Find where the end of this part is supposed to be added.*/
    del = find_in(dn, loop, true, end, &stop);


    switch(start) {
        case 0:
        case 2:
            list_add(&new->list, loop->list.prev);
            next = loop;
            break;
        case 1:
            if (rot != loop->m_type) {
                list_add(&new->list, &loop->list);
                next = list_next_entry(new, list);
            }
            else {
                loop->m_end = end;
                next = list_next_entry(loop, list);
            }
            break;
        case 3:
            if (rot != loop->m_type) {
                loop->m_end = ppos;
                list_add(&new->list, &loop->list);
                next = list_next_entry(new, list);
            }
            break;
        default:
            printk(KERN_EMERG "[ERROR][ZFS_MEDIA_ADD]Default in start.\n");
            return NULL;
    }
    /* Remove the excess part */
    while (next != NULL && next != del && &next->list != (dn)) {
        loop = list_next_entry(next, list);
        list_del(&next->list);
        kzfree(next);
        next = loop;
    }

    /* After last node */
    if (del == NULL) {
        new = list_last_entry(dn, typeof(*new), list);
        new->m_end = end;
        return new;
    }

    switch(stop) {
        case 0:
            loop = list_entry(del->list.prev, typeof(*loop), list);
            loop->m_end = end;
            return loop;
        case 1:
            del->m_type = rot;
            return del;
        case 2:
            loop = list_entry(del->list.prev, typeof(*loop), list);
            loop->m_type = rot;
            loop->m_end = end;
            return loop;
            break;
        case 3:
            if (del->m_type == rot)
                return del;
            else {
                new = kzalloc(sizeof(medium_t), GFP_KERNEL);
                if (new == NULL) {
                    printk(KERN_EMERG "[ERROR][ZFS_MEDIA_ADD]Cannot allocate for new medium\n");
                    return NULL;
                }
                new->m_start = end;
                new->m_end = del->m_end;
                new->m_type = del->m_type;
                del->m_end = end;
                del->m_type = rot;
                list_add(&new->list, &del->list);
                return del;
            }
            break;
        default:
            printk(KERN_EMERG "[ERROR][ZFS_MEDIA_ADD]Default in stop.\n");
            return NULL;
    }

    return new;
}
