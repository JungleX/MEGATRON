#include "afu.h"
#include "optimus.h"

static inline uint64_t hash_func(uint64_t v) 
{
    return v >> 21;
}
static inline uint64_t aligned_2m(uint64_t v)
{
    return (v >> 21) << 21;
}
int hash_pt_insert_mapping(struct optimus * optimus, uint64_t va, uint64_t pa, uint32_t pg_size)
{
    hash_pt_node *entry = kzalloc(sizeof(hash_pt_node), GFP_KERNEL);
    entry->va = aligned_2m(va);
    entry->pa = pa;
    entry->pg_size = pg_size;
    hash_add(optimus->hash_pt, &entry->node, hash_func(va));
    return 0;
}

int hash_pt_remove_mapping(struct optimus * optimus, hash_pt_node *pt_node)
{
    hash_del(&pt_node->node);
    
    return 0;
}

uint64_t hash_pt_iova_to_hpa(struct optimus* optimus, uint64_t iova, uint32_t *pg_size, hash_pt_node** p_pt_node)
{
    hash_pt_node *entry;
    uint64_t aligned_iova = aligned_2m(iova);
    hash_for_each_possible(optimus->hash_pt, entry, node, hash_func(iova)) {
        if (entry->va == aligned_iova) {
            if (p_pt_node) {
                *p_pt_node = entry;
            }
            if (pg_size){
                *pg_size = entry->pg_size;
            }
            return entry->pa;
        }
    }
    return 0;
}
