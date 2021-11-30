#include "afu.h"
#include "optimus.h"

static const uint32_t depth_max = 4;


int vtp_pt_init(vtp_pt **pt)
{
    vtp_pt *new_pt = kzalloc(sizeof(vtp_pt), GFP_KERNEL);
    optimus_info("vtp pt init \n");
    if (!new_pt) 
    {
        optimus_err("allocate pt failed!");
        return -1;
    }
    new_pt->pt_root = kzalloc(sizeof(vtp_pt_node),GFP_KERNEL);
    if (!new_pt->pt_root) 
    {
        optimus_err("allocate pt root failed!");
        return -1;
    }

    memset(new_pt->pt_root, -1, sizeof(vtp_pt_node));
    

    *pt = new_pt;

    return 0;
}
static inline bool node_entry_exist(vtp_pt_node *node, uint64_t idx)
{
    return (node->vtable[idx] != (uint64_t)-1);
}
static inline bool node_entry_is_terminal(vtp_pt_node *node, uint64_t idx)
{
    return (node->is_terminal[idx]);
}
static inline vtp_pt_node* get_child_node(vtp_pt_node* node, uint64_t idx)
{
    return (vtp_pt_node *)(node->vtable[idx]);
}
static inline void inval_pt_walk_cache(vtp_pt *pt)
{
    pt->prev_find_term_node = NULL;
} 
static inline bool node_is_empty(vtp_pt_node *node) 
{
    int idx;
    for (idx=0; idx< 512; idx++) 
    {
        if (node->vtable[idx] != (uint64_t)-1) return false;
    }
    return true;
}
int vtp_pt_end(vtp_pt_node *node, uint32_t depth) 
{
    //vtp_pt_node *child = NULL;
    int idx;
    //optimus_info("vtp pt end\n");
    for (idx = 0; idx < 512; idx++)
    {
        if (node_entry_exist(node, idx))
        {
            if (node_entry_is_terminal(node, idx)) 
            {
                node->vtable[idx] = -1;
                node->is_terminal[idx] = false;
                // should be freed by guest
                
            } else {
                if (depth == 1) 
                {
                    optimus_err("not terminal depth = 1");
                    return -1;
                }
                vtp_pt_end(get_child_node(node, idx), depth - 1);
                
            }
        }
    }
    kfree(node);
    return 0;
}

int vtp_pt_uinit(vtp_pt **pt)
{
    
    vtp_pt *pt_p = *pt; 
    optimus_info("vtp pt uinit");
    if (pt_p->pt_root) 
    {
        vtp_pt_end(pt_p->pt_root, depth_max);
        kfree(pt_p->pt_root);
    }
    kfree(pt_p);
    *pt = NULL;
    
    return 0;
}
int vtp_pt_inval_hwtlb_iova(struct optimus *optimus, uint64_t iova) {
    //vtp_pt *pt = optimus->pt;
    writeq(iova/CL(1), &optimus->pafu_mmio[VTP_BASE_MMIO + CCI_MPF_VTP_CSR_INVAL_PAGE_VADDR]);
    return 0;
}
static inline uint64_t mask_from_depth(uint32_t depth) {
    uint64_t mask = ~(uint64_t)0;
    mask >>= (12 + 9 * depth);
    mask <<= (12 + 9 * depth);
    return mask;
}

static inline uint64_t idx_from_depth_addr(uint32_t depth, uint64_t addr){
    uint64_t idx;
    idx = addr >> (12 + 9 * depth);
    idx = idx & 0x1ff;
    return idx;
}

static inline uint64_t page_size_from_depth(uint32_t depth) 
{
    switch (depth) {
        case 0: return PGSIZE_4K;
        case 1: return PGSIZE_2M;
        default: return PGSIZE_4K;
    }
}
uint64_t vtp_pt_iova_to_hpa(vtp_pt *pt, uint64_t iova, uint64_t *pg_size) {
    uint64_t pa = 0;
    vtp_pt_node *node;
    uint32_t depth;
    uint64_t mask;
    uint64_t idx;

    //find terminal node and index
    depth = pt->prev_depth + 1;
    node = pt->prev_find_term_node;
    mask = mask_from_depth(pt->prev_depth);
    if (!node | ((mask & (pt->prev_va ^ iova)) != 0)) {
        depth = depth_max;
        node = pt->pt_root;
    }
    while (depth)
    {
        depth--;
        // Index in the current level
        idx = idx_from_depth_addr(depth, iova);

        if (!node_entry_exist(node,idx)) return 0;

        if (node_entry_is_terminal(node,idx))
        {
            pt->prev_depth = depth;
            pt->prev_va = iova;
            pt->prev_find_term_node = node;
            pt->prev_idx = idx;

            break;
        }

        // Walk down to child. We already know that the child exists since
        // the code above proves that the entry at idx exists and is not
        // terminal.
        node = (vtp_pt_node *)node->vtable[idx];
    }
    if (pg_size) 
    {
        *pg_size = page_size_from_depth(depth);
    }
    pa = node->vtable[idx];
    
    return pa;
}

int vtp_pt_insert_mapping(vtp_pt *pt, uint64_t va, uint64_t pa, uint32_t pg_size)
{
    vtp_pt_node *node = pt->pt_root;
    uint32_t depth = depth_max, iter = pg_size == PGSIZE_2M ? 2 : 3;
    uint64_t idx;
    while (iter) 
    {
        depth--;
        iter--;
        idx = idx_from_depth_addr(depth, va);
        //optimus_info("%s: vtp insert mapping va %llx idx %llx depth %llx\n", __func__, va,idx,depth);
        if (!node_entry_exist(node,idx)) 
        {
            vtp_pt_node *temp = kzalloc(sizeof(vtp_pt_node),GFP_KERNEL);
            if (!temp) 
            {
                optimus_err("VTP PT NO MEMORY!");
                return -1;
            }
            memset(temp, -1, sizeof(vtp_pt_node));
            node->vtable[idx] = (uint64_t)temp;
            node->is_terminal[idx] = false;
        }
    
        // already mapped?
        if (node_entry_is_terminal(node,idx)) 
        {
            optimus_err("map before unmap!");
            return -1;
        }
        node = get_child_node(node, idx);
        
    }
    //depth--;
    idx = idx_from_depth_addr(depth - 1,va);
    //optimus_info("%s: vtp insert mapping va %llx idx %llx depth %llx\n", __func__, va,idx,depth-1);
    inval_pt_walk_cache(pt);

    if (node_entry_exist(node,idx))
    {
        if ((depth >= 2) && !node_entry_is_terminal(node, idx))
        {
            vtp_pt_node *child_node = get_child_node(node, idx);
            if (! node_is_empty(child_node)) 
            {
                optimus_err("map before unmap!");
                return -1;
            }
            kfree(child_node);
        } else  
        {
            optimus_err("pt structure error");
            return -1;
        }
    }
    node->vtable[idx] = (uint64_t)pa;
    node->is_terminal[idx] = true;
    optimus_info("%s: vtp map va %llx to pa %llx pgsize %x idx %llx depth %llx\n",
                   __func__, va, pa, pg_size,idx,depth);
    return 0;
}

int vtp_pt_remove_mapping(vtp_pt *pt, uint64_t va)
{
    vtp_pt_node *node = pt->pt_root;
    uint32_t depth = depth_max;
    uint64_t idx;
    while (depth) 
    {
        depth--;
        idx = idx_from_depth_addr(depth, va);
        //optimus_info("%s: vtp remove mapping va %llx idx %llx depth %llx\n", __func__, va,idx,depth);
        if (!node_entry_exist(node, idx))
        {
            optimus_err("remove page not found");
            return -1;
        }
        if (node_entry_is_terminal(node, idx))
        {
            optimus_info("%s: vtp remove mapping va %llx to pa %llx idx %llx depth %llx\n", __func__, va,node->vtable[idx],idx,depth);
            node->vtable[idx] = (uint64_t) -1;
            node->is_terminal[idx] = false;
           
            return 0;
        }
        node = get_child_node(node, idx);
    }
    return -1;
}