#include <linux/kernel.h>
#include <linux/unistd.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/rbtree.h>
#include <linux/types.h>
#include <linux/rwsem.h>
#include <linux/slab.h>
#include <sys/zfs_syscalls.h>
#include <sys/zfs_media.h>
#include <sys/hetfs.h>
#include <sys/disk.h>
#include <sys/zpl.h>
#include <asm/uaccess.h>
#include <linux/list.h>
#include <sys/zfs_znode.h>
#include <sys/dnode.h>
#include <sys/dbuf.h>

#include <linux/err.h>
#include <sys/hetfs.h>

extern struct rb_root *hetfs_tree;
extern int media_tree;
extern int only_one;
extern char *only_name;
char *number;
char *start;
char *end;
char *where;
char procfs_buffer[PATH_MAX+NAME_MAX+40];
const char delimiters[] = " \n";

#define for_each_syscall(_iter, _tests, _tmp) \
	for (_tmp = 0, _iter = _tests; \
	     _tmp < ARRAY_SIZE(_tests); \
	     _tmp++, _iter++)

struct storage_media available_media[] = {
    {"METASLAB_ROTOR_VDEV_TYPE_SSD", 0x01},
    {"METASLAB_ROTOR_VDEV_TYPE_HDD", 0x08},
};

void print_only_one(int flag) {
    only_one = flag;
}

/*struct list_head *tight_list(struct list_head *general)
{
    struct list_head *pos, *n, *f, *pos1, *new;
    struct analyze_request *areq, *areq1;
    int found;
    struct analyze_request *posh, *nh;

start:
    new = kzalloc(sizeof(struct list_head), GFP_KERNEL);
    if (new == NULL)
        return NULL;
    INIT_LIST_HEAD(new);

    list_for_each_safe(pos, n, general) {
        found = 0;
        areq = list_entry(pos, struct analyze_request, list);
        list_for_each_safe(pos1, f, new) {
            areq1 = list_entry(pos1, struct analyze_request, list);
            if (areq->start_offset == areq1->end_offset &&
                abs(areq->start_time - areq1->end_time) <= MAX_DIFF) {
                areq1->end_offset = areq->end_offset;
                areq1->end_time = areq->start_time < areq1->end_time? areq1->end_time: areq->start_time;
                found = 1;
                break;
            }
            if (areq->end_offset == areq1->start_offset &&
                abs(areq1->start_time - areq->end_time) <= MAX_DIFF) {
                areq1->start_offset = areq->start_offset;
                areq1->start_time = areq1->start_time < areq->end_time? areq1->start_time: areq->end_time;
                found = 1;
                break;
            }
        }
        if (!found) {
            list_del(pos);
            list_add_tail(pos, new);
        }
        printk(KERN_EMERG "[START-IN]\n");
        list_for_each_entry_safe(posh, nh, new, list) {
            printk(KERN_EMERG "[TIGHT-IN] start: %lld - end:%lld start time: %lld - end time:%lld times:%d\n",
                        posh->start_offset, posh->end_offset,
                        posh->start_time, posh->end_time, posh->times);
        }
        printk(KERN_EMERG "[END-IN]\n");
    }
    if (list_empty(general)) {
        kzfree(general);
        return new;
    }*/

/*    list_for_each_safe(pos, n, general) {
        areq = list_entry(pos, struct analyze_request, list);
        list_del(pos);
        kzfree(areq);
        kzfree(pos);
    }

    if (only_one) {
        general = new;
        goto start;
    }
    return new;
}*/


void print_one_file(char *name) {
    struct rb_node *nh;
    struct data *entry;
    struct analyze_request *posh;

    if (name == NULL) {
        printk(KERN_EMERG "[ERR-POF]Empty name\n");
        return;
    }

    down_read(&tree_sem);
    if (hetfs_tree == NULL || RB_EMPTY_ROOT(hetfs_tree)) {
        printk(KERN_EMERG "[ERR-POF]Empty root\n");
        return;
    }
    entry = rb_search(hetfs_tree, name);
    up_read(&tree_sem);
    if (entry == NULL) {
        printk(KERN_EMERG "[ERR-POF]No file %s in tree\n", name);
        return;
    }

    printk(KERN_EMERG "[HETFS] file: %s size %llu blksz %u\n", name, entry->size, entry->dn_datablksz);
    down_read(&entry->read_sem);
    if (!RB_EMPTY_ROOT(entry->read_reqs))
        printk(KERN_EMERG "[HETFS] READ req:\n");
    list_for_each_entry_rb(posh, nh, entry->read_reqs)
        printk(KERN_EMERG "[HETFS] blkid: %lld times: %d\n", posh->blkid, posh->times);
    up_read(&entry->read_sem);
    down_read(&entry->write_sem);
    if (!RB_EMPTY_ROOT(entry->write_reqs))
        printk(KERN_EMERG "[HETFS] WRITE req:\n");
    list_for_each_entry_rb(posh, nh, entry->write_reqs)
        printk(KERN_EMERG "[HETFS] blkid: %lld times: %d\n", posh->blkid, posh->times);
    up_read(&entry->write_sem);
    down_read(&entry->read_sem);
    if (!RB_EMPTY_ROOT(entry->mmap_reqs))
        printk(KERN_EMERG "[HETFS] MAP MMAP req:\n");
    list_for_each_entry_rb(posh, nh, entry->mmap_reqs)
        printk(KERN_EMERG "[HETFS] blkid: %lld times: %d\n", posh->blkid, posh->times);
    if (!RB_EMPTY_ROOT(entry->rmap_reqs))
        printk(KERN_EMERG "[HETFS] READ MMAP req:\n");
    list_for_each_entry_rb(posh, nh, entry->rmap_reqs)
        printk(KERN_EMERG "[HETFS] blkid: %lld times: %d\n", posh->blkid, posh->times);
    up_read(&entry->read_sem);
//  analyze(entry);
}

void print_name_region(char *name) {
    struct rb_node *node, *nh;
    struct data *entry;
    struct analyze_request *posh;

    down_read(&tree_sem);
    if (hetfs_tree == NULL || RB_EMPTY_ROOT(hetfs_tree)) {
        printk(KERN_EMERG "[ERROR] __exact empty root\n");
        return;
    }
    for (node = rb_first(hetfs_tree); node; node = rb_next(node)) {
        entry = rb_entry(node, struct data, node);
        if (entry->file == NULL || strstr(entry->file, name) == NULL)
            continue;

        printk(KERN_EMERG "[HETFS] file: %s size %llu blksz %u\n", entry->file, entry->size, entry->dn_datablksz);
        if (!RB_EMPTY_ROOT(entry->read_reqs))
            printk(KERN_EMERG "[HETFS] READ req:\n");
        list_for_each_entry_rb(posh, nh, entry->read_reqs)
            printk(KERN_EMERG "[HETFS] blkid: %lld times: %d\n", posh->blkid, posh->times);
        if (!RB_EMPTY_ROOT(entry->write_reqs))
            printk(KERN_EMERG "[HETFS] WRITE req:\n");
        list_for_each_entry_rb(posh, nh, entry->write_reqs)
            printk(KERN_EMERG "[HETFS] blkid: %lld times: %d\n", posh->blkid, posh->times);
        if (!RB_EMPTY_ROOT(entry->mmap_reqs))
            printk(KERN_EMERG "[HETFS] MAP MMAP req:\n");
        list_for_each_entry_rb(posh, nh, entry->mmap_reqs)
            printk(KERN_EMERG "[HETFS] blkid: %lld times: %d\n", posh->blkid, posh->times);
        if (!RB_EMPTY_ROOT(entry->rmap_reqs))
            printk(KERN_EMERG "[HETFS] READ MMAP req:\n");
        list_for_each_entry_rb(posh, nh, entry->rmap_reqs)
            printk(KERN_EMERG "[HETFS] blkid: %lld times: %d\n", posh->blkid, posh->times);
    }
    up_read(&tree_sem);
}

void print_tree(int flag) {
    struct rb_node *node, *nh;
    struct data *entry;
    struct analyze_request *posh;
    int all_nodes, all_requests, requests;

    all_nodes = all_requests = requests = 0;

    down_read(&tree_sem);
    if (hetfs_tree == NULL || RB_EMPTY_ROOT(hetfs_tree)) {
        printk(KERN_EMERG "[ERROR] __exact empty root\n");
        return;
    }
    for (node = rb_first(hetfs_tree); node; node = rb_next(node)) {
        ++all_nodes;
        entry = rb_entry(node, struct data, node);
        if (entry->file == NULL) {
            printk(KERN_EMERG "[HETFS] Error name NULL\n");
            continue;
        }

        printk(KERN_EMERG "[HETFS] file: %s size %llu blksz %u\n", entry->file, entry->size, entry->dn_datablksz);
        if (flag) {
            if (rb_first(entry->read_reqs) != NULL)
                printk(KERN_EMERG "[HETFS] READ req:\n");
            list_for_each_entry_rb(posh, nh, entry->read_reqs) {
                all_requests += posh->times;
                printk(KERN_EMERG "[HETFS] read file %s blkid: %lld times: %d\n", entry->file, posh->blkid, posh->times);
            }
            if (rb_first(entry->write_reqs) != NULL)
                printk(KERN_EMERG "[HETFS] WRITE req:\n");
            list_for_each_entry_rb(posh, nh, entry->write_reqs) {
                all_requests += posh->times;
                printk(KERN_EMERG "[HETFS] write file %s blkid: %lld times: %d\n", entry->file, posh->blkid, posh->times);
            }
            if (rb_first(entry->mmap_reqs) != NULL)
                printk(KERN_EMERG "[HETFS] MAP MMAP req:\n");
            list_for_each_entry_rb(posh, nh, entry->mmap_reqs) {
                printk(KERN_EMERG "[HETFS] mmap file %s blkid: %lld times: %d\n", entry->file, posh->blkid, posh->times);
            }
            if (rb_first(entry->rmap_reqs) != NULL)
                printk(KERN_EMERG "[HETFS] READ MMAP req:\n");
            list_for_each_entry_rb(posh, nh, entry->rmap_reqs) {
                all_requests += posh->times;
                printk(KERN_EMERG "[HETFS] read mmap file %s blkid: %lld times: %d\n", entry->file, posh->blkid, posh->times);
            }
        }
    }
    if (flag)
        printk(KERN_EMERG "[HETFS]Tree Nodes:%d, requests:%d\n", all_nodes, all_requests);
    else
        printk(KERN_EMERG "[HETFS]Tree Nodes:%d\n", all_nodes);
    up_read(&tree_sem);
}


/*struct list_head *list_stuff(struct list_head *general, char* name, char *list) {
    struct list_head *pos, *n, *posh, *new;
    struct analyze_request *areq, *areq1;
    int found;

    if (!list_empty(general)) {
        new = kzalloc(sizeof(struct list_head), GFP_KERNEL);
        if (new == NULL)
            return NULL;
        INIT_LIST_HEAD(new);

        list_for_each_safe(pos, n, general) {
            found = 0;
            areq = list_entry(pos, struct analyze_request, list);
            list_for_each(posh, new) {
                areq1 = list_entry(posh, struct analyze_request, list);
                if (areq->start_offset == areq1->end_offset &&
                    abs(areq->start_time - areq1->end_time) <= MAX_DIFF) {
                    areq1->end_offset = areq->end_offset;
                    areq1->start_time = (areq->start_time < areq1->start_time)?areq->start_time:areq1->start_time;
                    areq1->end_time = (areq->end_time > areq1->end_time)?areq->end_time:areq1->end_time;
                    found = 1;
                    break;
                }
                if (areq->end_offset == areq1->start_offset &&
                    abs(areq->end_time - areq1->start_time) <= MAX_DIFF) {
                    areq1->start_offset = areq->start_offset;
                    areq1->end_time = (areq->end_time > areq1->end_time)?areq->end_time:areq1->end_time;
                    areq1->start_time = (areq->start_time < areq1->start_time)?areq->start_time:areq1->start_time;
                    found = 1;
                    break;
                }
            }
            if (!found) {
                list_del(pos);
                list_add_tail(pos, new);
            }
        }

        if (list_empty(general)) {
            printk(KERN_EMERG "[STOP] File's %s %s list had enough nodes\n", name, list);
            kzfree(general);
            return new;
        }
        list_for_each_safe(pos, n, general) {
            areq = list_entry(pos, struct analyze_request, list);
            list_del(pos);
            kzfree(areq);
        }
        kzfree(general);
        return new;
    }
    return general;
}*/

static void print_file(void)
{
    print_one_file(only_name);
}

static void print_region(void)
{
    print_name_region(only_name);
}

static void print_nodes(void)
{
    print_tree(false);
}

static void print_all(void)
{
    print_tree(true);
}

static void change_medium(void)
{
    struct data *tree_entry = NULL;
    ssize_t n_start, n_end;
    int ret, n_where;

    if (start == NULL || end == NULL || where == NULL) {
        printk(KERN_EMERG "[ERROR] Start, end and where should be mentioned\n");
        return;
    }

    ret = kstrtoul(start, 10, &n_start);
    if (ret) {
        printk(KERN_EMERG "[ERROR] Change start to n_start failed\n");
        return ;
    }
    ret = kstrtoul(end, 10, &n_end);
    if (ret) {
        printk(KERN_EMERG "[ERROR] Change end to n_end failed\n");
        return ;
    }
    if (n_end < n_start) {
        printk(KERN_EMERG "[ERROR] End smaller than start\n");
        return;
    }
    ret = kstrtoint(where, 10, &n_where);
    if (ret) {
        printk(KERN_EMERG "[ERROR] Change where to n_where failed\n");
        return ;
    }

    down_write(&tree_sem);
    if (hetfs_tree == NULL || RB_EMPTY_ROOT(hetfs_tree)) {
        printk(KERN_EMERG "[ERROR] Empty root\n");
        return;
    }
    tree_entry = tree_insearch(NULL, only_name);
    up_write(&tree_sem);

    if (tree_entry == NULL) {
        printk(KERN_EMERG "[ERROR] No node in tree\n");
        return;
    }

    if (n_end != n_start)
        n_end = INT64_MAX;

    zfs_media_add_blkid(tree_entry->list_write_rot, n_start, n_end, available_media[n_where].bit, 0);

    if (tree_entry->filp != NULL)
        zpl_rewrite(tree_entry->filp, tree_entry->ip, n_start, n_end, tree_entry->dn_datablksz);
    return;
}

void list_print(struct list_head *dn) {
    media_t *loop, *n = NULL;
    list_for_each_entry_safe(loop, n, dn, list) {
        if (loop->m_type == METASLAB_ROTOR_VDEV_TYPE_HDD)
            printk(KERN_EMERG "[PRINT]File %s from %lld to %lld is in METASLAB_ROTOR_VDEV_TYPE_HDD\n", only_name, loop->m_start, loop->m_end);
        else if (loop->m_type == METASLAB_ROTOR_VDEV_TYPE_SSD)
            printk(KERN_EMERG "[PRINT]File %s from %lld to %lld is in METASLAB_ROTOR_VDEV_TYPE_SSD\n", only_name, loop->m_start, loop->m_end);
        else if (loop->m_type == -1)
            printk(KERN_EMERG "[PRINT]File %s from %lld to %lld is in METASLAB_ROTOR_VDEV_TYPE_HDD with -1\n", only_name, loop->m_start, loop->m_end);
        else
            printk(KERN_EMERG "[PRINT]File %s from %lld to %lld with rot %d\n", only_name, loop->m_start, loop->m_end, loop->m_type);
    }
}

static void print_media(void)
{
    struct data *tree_entry = NULL;

    tree_entry = rb_search(hetfs_tree, only_name);
    if (tree_entry == NULL) {
        printk(KERN_EMERG "[ERROR] No node in tree\n");
        return;
    }
    else {
        printk(KERN_EMERG "[PRINT] File %s InsNode %p\n", only_name, tree_entry);
    }

    if (!list_empty(tree_entry->list_write_rot)) {
        printk(KERN_EMERG "[PRINT] File %s write list rotor\n", only_name);
        list_print(tree_entry->list_write_rot);
    }
    else {
        printk(KERN_EMERG "[PRINT] File %s write list rotor is empty\n", only_name);
    }

    if (!list_empty(tree_entry->list_read_rot)) {
        printk(KERN_EMERG "[PRINT] File %s read list rotor\n", only_name);
        list_print(tree_entry->list_read_rot);
    }
    else {
        printk(KERN_EMERG "[PRINT] File %s read list rotor is empty\n", only_name);
    }

    return;
}

/*struct list_head *zip_list(struct list_head *general)
{
    struct list_head *pos, *n;
    struct analyze_request *areq, *areq1;
    int found;

    new = kzalloc(sizeof(struct list_head), GFP_KERNEL);
    if (new == NULL)
        return NULL;
    INIT_LIST_HEAD(new);

    list_for_each_safe(pos, n, general) {
//        found = 0;
        areq = list_entry(pos, struct analyze_request, list);
        areq1 = areq;
        list_for_each_entry_continue(areq1, general, list){
//            areq1 = list_entry(pos1, struct analyze_request, list);
            if (areq->start_offset == areq1->start_offset &&
                areq->end_offset == areq1->end_offset) {
                areq1->times += areq->times;
                list_del(pos);
                kzfree(areq);
                break;
            }
        }
        if (!found) {
            __list_del_entry(pos);
            list_add_tail(pos,new);
        }
    }
    return general;
    list_for_each_safe(pos, n, general) {
        areq = list_entry(pos, struct analyze_request, list);
        list_del(pos);
        kzfree(areq);
    }
    kzfree(general);
    return new;
}*/

/*void analyze(struct data* InsNode)
{
    struct list_head *pos, *n;
    struct analyze_request *areq;
    loff_t half;
    int mid, all = 0;
    half = InsNode->size >> 1;
    if (!RB_EMPTY_ROOT(InsNode->read_reqs)) {
        InsNode->read_reqs = zip_list(InsNode->read_reqs);
        printk(KERN_EMERG "[HETFS]File %s\n", InsNode->file);
        list_for_each_safe(pos, n, InsNode->read_reqs) {
            areq = list_entry(pos, struct analyze_request, list);
            part = areq->end_offset - areq->start_offset;
            InsNode->read_all_file += areq->times;
            if (part == InsNode->size) {
                all += areq->times;
            }
            else if (part >= half) {
                printk(KERN_EMERG "[HETFS] This part is a big read start %lld end %lld accessed %d times\n",
                        areq->start_offset, areq->end_offset, areq->times);
            }
        }
        mid = InsNode->read_all_file >> 1;
        if (all > 0 && (((all & 1) && all > mid) || (!(all & 1) && all >= mid))) {
            printk(KERN_EMERG "[HETFS] It was read sequentially\n");
        }
    }
    if (!RB_EMPTY_ROOT(InsNode->write_reqs)) {
        InsNode->write_reqs = zip_list(InsNode->write_reqs);
        all = 0;
        list_for_each_safe(pos, n, InsNode->write_reqs) {
            areq = list_entry(pos, struct analyze_request, list);
            part = areq->end_offset - areq->start_offset;
            InsNode->write_all_file += areq->times;
            if (part == InsNode->size)
                all++;
            else if (part >= half) {
                printk(KERN_EMERG "[HETFS] This part is a big write start %lld end %lld accessed %d times\n",
                        areq->start_offset, areq->end_offset, areq->times);
            }
        }
        mid = InsNode->write_all_file >> 1;
        if (all > 0 && (((all & 1) && all > mid) || (!(all & 1) && all >= mid)))
            printk(KERN_EMERG "[HETFS] It was write sequentially\n");
    }
}*/

void analyze(struct data* InsNode)
{
    struct rb_node *nh;
    struct analyze_request *posh;
    int proportion, ret;
    int max = -1;
    int min = 0;
    int part = 0;
    if (start == NULL)
        proportion = 50;
    else {
        ret = kstrtoint(start, 10, &proportion);
        if (ret) {
            printk(KERN_EMERG "[ERROR]Change proportion\n");
            return;
        }
    }
    down_read(&InsNode->read_sem);
    if (RB_EMPTY_ROOT(InsNode->read_reqs)) {
        up_read(&InsNode->read_sem);
        return;
    }
    list_for_each_entry_rb(posh, nh, InsNode->read_reqs) {
        if (max == -1) {
            max = posh->times;
            min = posh->times;
            continue;
        }
        if (max < posh->times) {
            max = posh->times;
            continue;
        }
        if (min > posh->times) {
            min = posh->times;
            continue;
        }
    }
    part = max - min;
    part = (part * proportion) / 100;
    part = max - part;
    printk(KERN_EMERG "[ANALYZE]max %d, min %d, part %d, proportion %d\n", max, min, part, proportion);
    min = max = -1;
    list_for_each_entry_rb(posh, nh, InsNode->read_reqs) {
        if (posh->times >= part) {
            if (min == -1)
                min = posh->blkid;
            max = posh->blkid;
            zfs_media_add_blkid(InsNode->list_write_rot, posh->blkid, posh->blkid+1, METASLAB_ROTOR_VDEV_TYPE_SSD, 0);
        }
        if (max != posh->blkid && max != -1 && InsNode->filp != NULL) {
            zpl_rewrite(InsNode->filp, InsNode->ip, min, max, InsNode->dn_datablksz);
            min = max = -1;
        }
    }
    up_read(&InsNode->read_sem);
}

static void analyze_tree(void)
{
    struct rb_node *node;
    struct data *entry;
    down_read(&tree_sem);
    /*We actually write to nodes in the tree but no insert or delete*/
    for (node = rb_first(hetfs_tree); node; node = rb_next(node)) {
        entry = rb_entry(node, struct data, node);
        analyze(entry);
    }
    up_read(&tree_sem);
}

void empty_tree(struct rb_root *tree) {
    struct rb_node *node;
    struct analyze_request *ar;
    node = rb_first(tree);
    while (node != NULL) {
        ar = rb_entry_safe(node, struct analyze_request, node);
        rb_erase(node, tree);
        kzfree(ar);
        node = rb_first(tree);
    }
}

static void read_tree_free(void) {
    struct data *entry;
    if (only_name == NULL)
        return;
    down_read(&tree_sem);
    entry = rb_search(hetfs_tree, only_name);
    up_read(&tree_sem);
    down_write(&entry->read_sem);
    empty_tree(entry->read_reqs);
    up_write(&entry->read_sem);
}

static void write_tree_free(void) {
    struct data *entry;
    if (only_name == NULL)
        return;
    down_read(&tree_sem);
    entry = rb_search(hetfs_tree, only_name);
    up_read(&tree_sem);
    down_write(&entry->write_sem);
    empty_tree(entry->write_reqs);
    up_write(&entry->write_sem);
}

static void both_tree_free(void) {
    struct data *entry;
    if (only_name == NULL)
        return;
    down_read(&tree_sem);
    entry = rb_search(hetfs_tree, only_name);
    up_read(&tree_sem);
    down_write(&entry->read_sem);
    empty_tree(entry->read_reqs);
    up_write(&entry->read_sem);
    down_write(&entry->write_sem);
    empty_tree(entry->write_reqs);
    up_write(&entry->write_sem);
}

static void all_read_tree_free(void) {
    struct rb_node *node;
    struct data *entry;
    down_read(&tree_sem);
    for (node = rb_first(hetfs_tree); node; node = rb_next(node)) {
        entry = rb_entry(node, struct data, node);
        down_write(&entry->read_sem);
        empty_tree(entry->read_reqs);
        up_write(&entry->read_sem);
    }
    up_read(&tree_sem);
}

static void all_write_tree_free(void) {
    struct rb_node *node;
    struct data *entry;
    down_read(&tree_sem);
    for (node = rb_first(hetfs_tree); node; node = rb_next(node)) {
        entry = rb_entry(node, struct data, node);
        down_write(&entry->write_sem);
        empty_tree(entry->write_reqs);
        up_write(&entry->write_sem);
    }
    up_read(&tree_sem);
}

static void all_list_free(void) {
    struct rb_node *node;
    struct data *entry;
    down_read(&tree_sem);
    for (node = rb_first(hetfs_tree); node; node = rb_next(node)) {
        entry = rb_entry(node, struct data, node);
        down_write(&entry->read_sem);
        empty_tree(entry->read_reqs);
        up_write(&entry->read_sem);
        down_write(&entry->write_sem);
        empty_tree(entry->write_reqs);
        up_write(&entry->write_sem);
    }
    up_read(&tree_sem);
}

static void analyze_only(void) {
    struct data *entry;
    if (only_name == NULL)
        return;
    down_read(&tree_sem);
    entry = rb_search(hetfs_tree, only_name);
    up_read(&tree_sem);
    if (entry != NULL)
        analyze(entry);
}

struct zfs_syscalls available_syscalls[] = {
	{ "print_nodes",	print_nodes	},
	{ "print_all",		print_all	},
	{ "analyze_tree",	analyze_tree	},
	{ "change_medium",	change_medium	},
	{ "print_media",	print_media	},
	{ "print_file",	    print_file	},
	{ "read_tree_free",	    read_tree_free	},
	{ "write_tree_free",	    write_tree_free	},
	{ "both_tree_free",	    both_tree_free	},
	{ "all_read_tree_free",	    all_read_tree_free	},
	{ "all_write_tree_free",	    all_write_tree_free	},
	{ "all_list_free",	    all_list_free	},
	{ "analyze_only",	    analyze_only	},
	{ "print_region",	    print_region	},
};

static void run_syscall(struct zfs_syscalls *syscall)
{
    syscall->test_fn();
}

static int zfs_syscalls_run(unsigned long op)
{
    struct zfs_syscalls *syscall;

    if (op > ARRAY_SIZE(available_syscalls))
        return -EINVAL;

    syscall = &available_syscalls[op];
    run_syscall(syscall);

    return 0;
}

static int syscall_proc_show(struct seq_file *m, void *v)
{
	int i;
	struct zfs_syscalls *syscall;

	seq_printf(m, "Usage: echo <syscall_idx> > /proc/zfs_syscalls\n\n");
	seq_printf(m, "Test Idx    Syscall Name\n");
	seq_printf(m, "---------------------\n");
	for_each_syscall(syscall, available_syscalls, i) {
		seq_printf(m, "     %3d    %s\n", i, syscall->name);
	}

	return 0;
};

static ssize_t __zfs_syscall_write(struct file *file, const char __user *buffer,
                            size_t count, loff_t *pos)
{
    int ret;
    unsigned long val;
    char * bla;
//    procfs_buffer = kzalloc(strlen(buffer)+2, GFP_KERNEL);
    memset(procfs_buffer, '\0', (PATH_MAX+NAME_MAX+40)*sizeof(char));
    procfs_buffer[strlen(buffer)+1] = ' ';

    if (copy_from_user(procfs_buffer, buffer, strlen(buffer))) {
            return -EFAULT;
    }

/*    ret = kstrtoul_from_user(buffer, count, 10, &val);
    if (ret)
        return ret;*/
    bla = strdup(procfs_buffer);
    number = strsep(&bla, delimiters);
    ret = kstrtoul(number, 10, &val);
    if (ret)
        return ret;
    only_name = strsep(&bla, delimiters);
    start = strsep(&bla, delimiters);
    end = strsep(&bla, delimiters);
    where = strsep(&bla, delimiters);
    strsep(&bla, delimiters);
    ret = zfs_syscalls_run(val);
    if (ret)
        return ret;

    *pos += count;
//    procfs_buffer = number;

    return ret ? ret : count;
}

static ssize_t zfs_syscall_write(struct file *file, const char __user *buffer,
                            size_t count, loff_t *pos)
{
    return __zfs_syscall_write(file, buffer, count, pos);
}

static int zfs_syscall_proc_open(struct inode *inode, struct file *file)
{
    return single_open(file, syscall_proc_show, NULL);
}

static const struct file_operations zfs_syscalls_proc_fops = {
    .owner = THIS_MODULE,
    .open = zfs_syscall_proc_open,
    .read = seq_read,
    .write = zfs_syscall_write,
};

static int __init zfs_syscalls_init(void)
{

	proc_create("zfs_syscalls", 0, NULL, &zfs_syscalls_proc_fops);
	pr_info("&zfs_syscalls_proc_fops successfully initialized\n");
	return 0;
}

void zfs_syscalls_initialize(void)
{
    zfs_syscalls_init();
}
