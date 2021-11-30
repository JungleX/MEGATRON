#ifndef VTP_TYPES
#define VTP_TYPES
#include <linux/hashtable.h>

typedef struct {
    uint64_t vtable[512];
    bool is_terminal[512];
} vtp_pt_node;

typedef struct {
    // Root of the page table.
    vtp_pt_node* pt_root;


    // Cache of most recent node in findTerminalNodeAndIndex().
    vtp_pt_node* prev_find_term_node;
    uint64_t prev_va;
    uint32_t prev_depth;
    uint32_t prev_idx;
    
} vtp_pt;

typedef struct {
    uint64_t pa;
    uint64_t va;
    uint32_t pg_size;
    struct hlist_node node;
} hash_pt_node;

#endif 
