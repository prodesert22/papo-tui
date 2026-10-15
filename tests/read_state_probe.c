#include "config/read_state.h"
#include <curl/curl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main(int argc,char **argv)
{
    if (argc!=4 || curl_global_init(CURL_GLOBAL_DEFAULT)!=CURLE_OK) return 2;
    json_t *result=NULL;
    if (!strcmp(argv[1],"load")) result=read_state_load(argv[2],argv[3]);
    else if (!strcmp(argv[1],"save")) {
        json_t *map=json_loadfd(STDIN_FILENO,JSON_REJECT_DUPLICATES,NULL);
        result=json_pack("{s:b}","ok",read_state_save(argv[2],argv[3],map));
        json_decref(map);
    }
    int rc=!result || json_dumpf(result,stdout,JSON_COMPACT)<0;
    json_decref(result); curl_global_cleanup();
    puts(""); return rc;
}
