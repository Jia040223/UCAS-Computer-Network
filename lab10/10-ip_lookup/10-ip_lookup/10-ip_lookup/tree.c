#include "tree.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

node_t* root;
TrieNodeOpt* unmatch;

TrieNodeOpt *Trie_map[MAP_NUM];

uint32_t* read_test_data(const char* lookup_file);
void insert_tree_node(node_t * root, char* binaryArray, int prefix, uint32_t port);

void Trie_map_init();
void insert_node_advance(TrieNodeOpt *root, uint32_t ip, int port, int prefix);

// Get the prefix from an unsigned int (inclusive)
static inline uint32_t get_prefix_ip(uint32_t ip, int prefix) {
    uint32_t mask = (uint32_t)(~(0xFFFFFFFF >> prefix));
    return ip & mask;
}

// Get the bits from start to end (inclusive)
static inline uint32_t get_bit(uint32_t uip, int bit) {
    uint32_t res = ((uip >> bit) & 0x1) ? 1 : 0;
    return res;
}

// return an array of ip represented by an unsigned integer, the length of array is TEST_SIZE
uint32_t* read_test_data(const char* lookup_file){
    //fprintf(stderr,"TODO:%s",__func__);
    //return NULL;
    uint32_t* ip_vec = (uint32_t*)malloc(sizeof(uint32_t) * TEST_SIZE);
    int idx = 0;
    char line[100];

    FILE* file = fopen(lookup_file, "r"); 
    if (file == NULL) {
        fprintf(stderr, "open lookup file failed\n");
        return NULL;
    }

    while (fgets(line, sizeof(line), file)) { 
        uint32_t binaryIP = 0;
        int ip_values[4];

        sscanf(line, "%d.%d.%d.%d", &ip_values[0], &ip_values[1], &ip_values[2], &ip_values[3]);

        for (int i = 0; i < 4; i++) {
            binaryIP <<= 8;
            binaryIP |= ip_values[i];
        }
    
        //printf("IP地址 %s 的二进制序列为: %u\n", line, binaryIP);
        ip_vec[idx++] = binaryIP;
    }

    fclose(file); // 关闭文件
    return ip_vec;
}

// Constructing an basic trie-tree to lookup according to `forward_file`
void create_tree(const char* forward_file){
    //fprintf(stderr,"TODO:%s",__func__);
    root =(node_t*)malloc(sizeof(node_t));
    root->type = I_NODE;
    root->lchild = NULL;
    root->rchild = NULL;

    FILE *file;
    char line[100];

    file = fopen(forward_file, "r"); 
    if (file == NULL) {
        fprintf(stderr, "open forward file failed\n");
        return;
    }

    while (fgets(line, sizeof(line), file)) { 
        char ip[12];
        int prefix;
        int port;

        sscanf(line, "%s %d %d", &ip[0], &prefix, &port);

        int ip_values[4];
        char binaryArray[33];
        sscanf(ip, "%d.%d.%d.%d", &ip_values[0], &ip_values[1], &ip_values[2], &ip_values[3]);

        int index = 0;
        for (int i = 0; i < 4; i++) {
            for (int j = 7; j >= 0; j--) {
                binaryArray[index++] = (ip_values[i] >> j) & 1 ? '1' : '0';
            }
        }
        binaryArray[32] = '\0'; 

        insert_tree_node(root, binaryArray, prefix, (uint32_t)port); 
    }

    fclose(file); // 关闭文件
    return;
}

// Look up the ports of ip in file `ip_to_lookup.txt` using the basic tree, input is read from `read_test_data` func 
uint32_t *lookup_tree(uint32_t* ip_vec){
    //fprintf(stderr,"TODO:%s",__func__);
    //return NULL;

    uint32_t* res = (uint32_t*)malloc(sizeof(uint32_t) * TEST_SIZE);

    for(int i = 0;i < TEST_SIZE;i++){
        uint32_t ip = ip_vec[i];
        char binaryArray[33];

        for (int j = 31; j >= 0; j--) {
            binaryArray[j] = (ip & 1) ? '1' : '0';
            ip >>= 1;
        }
        binaryArray[32] = '\0';
        //printf("%s\n", binaryArray);

        uint32_t match_port = -1;
        node_t* current_node = root;
        
        for(int k = 0;k < 32; k++){
            if(current_node->type == M_NODE){
                match_port = current_node->port;
            }

            if(binaryArray[k] == LEFT){
                if(current_node->lchild){
                    current_node = current_node->lchild;
                }
                else{
                    break;
                }
            }
            else{
                if(current_node->rchild){
                    current_node = current_node->rchild;
                }
                else{
                    break;
                }
            }
        }

        res[i] =  match_port;
        //printf("%d\n", match_port);
    }

    return res;
}

void insert_tree_node(node_t * root, char* binaryArray, int prefix, uint32_t port) {
    node_t* current_node = root;

    for(int i = 0;i < prefix;i++){
        if(binaryArray[i] == LEFT){
            if(current_node->lchild){
                current_node = current_node->lchild;
            }
            else{
                current_node->lchild = (node_t*)malloc(sizeof(node_t));
                current_node = current_node->lchild;

                current_node->type = I_NODE;
                current_node->lchild = NULL;
                current_node->rchild = NULL;
            }
        }
        else if(binaryArray[i] == RIGHT){
            if(current_node->rchild){
                current_node = current_node->rchild;
            }
            else{
                current_node->rchild = (node_t*)malloc(sizeof(node_t));
                current_node = current_node->rchild;

                current_node->type = I_NODE;
                current_node->lchild = NULL;
                current_node->rchild = NULL;
            }
        }
        else{
            printf("error\n");
        }
    }

    current_node->port = port;
    current_node->type = M_NODE;
}

//-----------------------------------------ADVANCE TREE------------------------------------------------------------

// Constructing an advanced trie-tree to lookup according to `forward_file`
void create_tree_advance(const char* forward_file){
    //fprintf(stderr,"TODO:%s",__func__);

    Trie_map_init();

    FILE *file;
    char line[100];

    file = fopen(forward_file, "r"); 
    if (file == NULL) {
        fprintf(stderr, "open forward file failed\n");
        return;
    }

    while (fgets(line, sizeof(line), file)) { 
        //printf("%s\n", line);
        char ip[20];
        int prefix;
        int port;

        sscanf(line, "%s %d %d", &ip[0], &prefix, &port);
        //printf("%s %d %d\n", ip, prefix, port);

        uint32_t binaryIP = 0;
        int ip_values[4];

        sscanf(line, "%d.%d.%d.%d", &ip_values[0], &ip_values[1], &ip_values[2], &ip_values[3]);

        for (int i = 0; i < 4; i++) {
            binaryIP <<= 8;
            binaryIP |= ip_values[i];
        }

        if (prefix >= MAP_SHIFT) {
            TrieNodeOpt *root = Trie_map[(0xffff0000 & binaryIP) >> MAP_SHIFT];
            insert_node_advance(root, binaryIP, port, prefix);
        } else {
            uint32_t mask = (~(0xFFFFFFFF >> prefix));
            uint32_t start = (binaryIP & mask) >> MAP_SHIFT;
            uint32_t end = start + (1 << (MAP_SHIFT - prefix));
            for (int i = start; i < end; i++) {
                if (Trie_map[i]->type == I_NODE || (Trie_map[i]->prefix_diff >= 16 - prefix)) {
                    Trie_map[i]->type = M_NODE;
                    Trie_map[i]->port = port;
                    Trie_map[i]->prefix_diff = 16 - prefix;
                }
            }
        } 
    }

    fclose(file); // 关闭文件
    return;
}

// Look up the ports of ip in file `ip_to_lookup.txt` using the advanced tree input is read from `read_test_data` func 
uint32_t *lookup_tree_advance(uint32_t* ip_vec){
    //fprintf(stderr,"TODO:%s",__func__);
    //return NULL;

    uint32_t* res = (uint32_t*)malloc(sizeof(uint32_t) * TEST_SIZE);

    for(int i = 0;i < TEST_SIZE;i++){
        uint32_t ip = ip_vec[i];
        
        TrieNodeOpt *curr = Trie_map[ip >> MAP_SHIFT];
        TrieNodeOpt *match = unmatch;

        uint32_t curr_bit;
        int cur_prefix = 32 - MAP_SHIFT;

        if (curr->prefix_diff > 0) {
            if (curr->type) {
                match = curr;
            }
            curr_bit = (ip & (0x3 << 14)) >> 14;
            curr = curr->children[curr_bit];
            cur_prefix += 2;
        }

        while(curr) {
            if (curr->type) {
                match = curr;
            }
            int off = 30 - cur_prefix;
            curr_bit = (ip & (0x3 << off)) >> off;
            curr = curr->children[curr_bit];
            cur_prefix += 2;
        }

        res[i] = match->port;
        //printf("%u\n", res[i]);
    }

    return res;
}

// Init Trie_map
void Trie_map_init() {
    unmatch = (TrieNodeOpt*)malloc(sizeof(TrieNodeOpt));
    unmatch->port = -1;
    for (int i = 0; i < MAP_NUM; i++) {
        Trie_map[i] = (TrieNodeOpt*)malloc(sizeof(TrieNodeOpt));
        Trie_map[i]->type = I_NODE;
        Trie_map[i]->port = 0;
        Trie_map[i]->prefix_diff = 0;
        for (int j = 0; j < 4; j++) {
            Trie_map[i]->children[j] = NULL;
        }
    }
}

// insert node to advance_tree
void insert_node_advance(TrieNodeOpt *root, uint32_t ip, int port, int prefix) {
    TrieNodeOpt *curr = root, *next = NULL;
    int off;
    int cur_bit;
    int cur_prefix;

    for (cur_prefix = 32 - MAP_SHIFT; cur_prefix < prefix - 1; cur_prefix += 2) {
        off = 30 - cur_prefix;
        cur_bit = (ip & (0x3 << off)) >> off; 
        next = curr->children[cur_bit];
        // If the children node's space type not been allocated.
        if (next == NULL) {
            next = (TrieNodeOpt*)malloc(sizeof(TrieNodeOpt));
            next->port = 0;
            next->type = I_NODE;
            next->children[0] = next->children[1] = next->children[2] = next->children[3] = NULL;
            curr->children[cur_bit] = next;
        }
        curr = next;
    }

    if (cur_prefix == prefix - 1) {
        off = 30 - cur_prefix;
        cur_bit = (ip & (0x3 << off)) >> off; 
        int start_bit = cur_bit & 0x2;
        for(int i = 0;i < 2;i++){
            next = curr->children[start_bit + i]; //最低位设置为0
            if (next == NULL) {
                next = (TrieNodeOpt*)malloc(sizeof(TrieNodeOpt));
                next->port = port;
                next->type = M_NODE;
                next->children[0] = next->children[1] = next->children[2] = next->children[3] = NULL;
                next->prefix_diff = 1;
                curr->children[start_bit + i] = next;
            }
        }
    }
    else{
        curr->port = port;
        curr->type = M_NODE;
    }
}
