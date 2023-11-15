#include <stdio.h>
#include <sys/time.h>
#include <stdlib.h>
#include <stdbool.h>
#include "util.h"
#include "tree.h"

const char* forwardingtable = "test/forwarding_table.txt";

const char* basic_lookup  = "test/lookup_file.txt";
const char* basic_compare = "test/compare_file.txt";

const char* advanced_lookup  = "test/lookup_file.txt";
const char* advanced_compare = "test/compare_file.txt";


bool check_result(uint32_t* port_vec, const char* compare_filename);

int main(void){
    struct timeval tv_start, tv_end;
    uint32_t* basic_res, *advance_res;
    
    // basic lookup
    printf("Constructing the basic tree......\n");
    create_tree(forwardingtable);

    printf("Reading data from basic lookup table......\n");
    uint32_t* basic_ip_vec = read_test_data(basic_lookup);

    printf("Looking up the basic port......\n");
    gettimeofday(&tv_start,NULL);
    basic_res = lookup_tree(basic_ip_vec);
    gettimeofday(&tv_end,NULL);

    int  basic_pass     = check_result(basic_res, basic_compare);
    long basic_interval = get_interval(tv_start,tv_end);

    // advanced
    printf("Constructing the advanced tree......\n");
    create_tree_advance(forwardingtable);

    printf("Reading data from advanced lookup table......\n");
    uint32_t* advanced_ip_vec = read_test_data(advanced_lookup);

    printf("Looking up the advanced port......\n");
    gettimeofday(&tv_start,NULL);
    advance_res = lookup_tree_advance(advanced_ip_vec);
    gettimeofday(&tv_end,NULL);

    int  advanced_pass     = check_result(advance_res, advanced_compare);
    long advanced_interval = get_interval(tv_start,tv_end);
    
    printf("Dumping result......\n");
    printf("basic_pass-%d\nbasic_lookup_time-%ldus\nadvance_pass-%d\nadvance_lookup_time-%ldus\n", \
            basic_pass,basic_interval,advanced_pass,advanced_interval);

    return 0;
}

bool check_result(uint32_t* port_vec, const char* compare_filename){
    int port;
    FILE* fp = fopen(compare_filename,"r");

    if(NULL == fp){
        perror("Open compare file fails");
        exit(1);
    }

    for(int i = 0;i < TEST_SIZE;i++){
        fscanf(fp,"%d",&port);
        if(port != port_vec[i]){
            fclose(fp);
            return false;
        }
    }
    fclose(fp);

    return true;
}
