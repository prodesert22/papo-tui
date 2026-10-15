#define _POSIX_C_SOURCE 200809L
#include "config/read_state.h"
#include "model/history.h"
#include "net/session_time.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

#define STATE_BYTES (4U * 1024U * 1024U)
#define CONTEXT_LIMIT 64U
static const char *file_name="read-cursors.json";
static const char *text(const json_t *object,const char *key,size_t limit)
{
    json_t *value=json_object_get(object,key);
    const char *s=json_string_value(value);
    return s && json_string_length(value)<=limit && strlen(s)==json_string_length(value) ? s : NULL;
}
static bool scope_valid(const char *url,const char *user)
{
    if (!url || strlen(url)>2048 || !history_uuid(user)) return false;
    char reason[192]; char *canonical=auth_normalize_url(url,reason,sizeof(reason));
    bool ok=canonical && !strcmp(canonical,url);
    curl_free(canonical); return ok;
}
static bool cursor_valid(const json_t *cursor)
{
    uint64_t time;
    return json_is_object(cursor) && json_object_size(cursor)==2 &&
        history_uuid(text(cursor,"id",36)) && session_time_parse(text(cursor,"created_at",64),&time);
}
static bool map_valid(const json_t *map)
{
    if (!json_is_object(map) || json_object_size(map)>READ_STATE_LIMIT) return false;
    const char *id; json_t *cursor;
    json_object_foreach((json_t *)map,id,cursor)
        if (!history_uuid(id) || !cursor_valid(cursor)) return false;
    return true;
}
static bool root_valid(json_t *root)
{
    json_t *contexts=json_object_get(root,"contexts");
    if (!json_is_object(root) || json_object_size(root)!=2 ||
        !json_is_integer(json_object_get(root,"version")) || json_integer_value(json_object_get(root,"version"))!=1 ||
        !json_is_array(contexts) || json_array_size(contexts)>CONTEXT_LIMIT) return false;
    size_t i; json_t *context;
    json_array_foreach(contexts,i,context) {
        if (!json_is_object(context) || json_object_size(context)!=3 ||
            !scope_valid(text(context,"url",2048),text(context,"user_id",36)) ||
            !map_valid(json_object_get(context,"messages"))) return false;
        for (size_t j=0;j<i;++j) {
            json_t *prior=json_array_get(contexts,j);
            if (!strcmp(text(prior,"url",2048),text(context,"url",2048)) &&
                !strcmp(text(prior,"user_id",36),text(context,"user_id",36))) return false;
        }
    }
    return true;
}
static bool private_file(int fd)
{
    struct stat st;
    return fstat(fd,&st)==0 && S_ISREG(st.st_mode) && st.st_uid==getuid() && st.st_nlink==1 &&
           (st.st_mode&07777)==0600;
}
static int directory_open(void)
{
    const char *xdg=getenv("XDG_STATE_HOME"),*home=getenv("HOME");
    char path[4096];
    if (xdg && *xdg) {
        if (*xdg!='/' || snprintf(path,sizeof(path),"%s",xdg)>=(int)sizeof(path)) return -1;
    } else if (!home || *home!='/' || snprintf(path,sizeof(path),"%s/.local/state",home)>=(int)sizeof(path)) return -1;
    for (char *p=path+1;*p;++p) if (*p=='/') {
        *p=0; int rc=mkdir(path,0700); *p='/'; if (rc && errno!=EEXIST) return -1;
    }
    if (mkdir(path,0700) && errno!=EEXIST) return -1;
    int parent=open(path,O_RDONLY|O_DIRECTORY|O_CLOEXEC);
    if (parent<0) return -1;
    if (mkdirat(parent,"papo-tui",0700) && errno!=EEXIST) { close(parent); return -1; }
    int fd=openat(parent,"papo-tui",O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC); close(parent);
    struct stat st;
    if (fd>=0 && (fstat(fd,&st) || st.st_uid!=getuid() || (st.st_mode&07777)!=0700)) { close(fd); return -1; }
    return fd;
}
static json_t *load(int directory)
{
    int fd=openat(directory,file_name,O_RDONLY|O_NOFOLLOW|O_CLOEXEC|O_NONBLOCK);
    if (fd<0) return errno==ENOENT ? json_pack("{s:i,s:[]}","version",1,"contexts") : NULL;
    struct stat st;
    if (!private_file(fd) || fstat(fd,&st) || st.st_size<1 || st.st_size>STATE_BYTES) { close(fd); return NULL; }
    size_t limit=(size_t)st.st_size,length=0;
    char *buffer=malloc(limit+1);
    if (!buffer) { close(fd); return NULL; }
    bool ok=true;
    while (length<=limit) {
        ssize_t n=read(fd,buffer+length,limit+1-length);
        if (n<0 && errno==EINTR) continue;
        if (n<0) ok=false;
        if (n<=0) break;
        length+=(size_t)n;
        if (length>limit) { ok=false; break; }
    }
    close(fd);
    json_t *root=ok ? json_loadb(buffer,length,JSON_REJECT_DUPLICATES,NULL) : NULL;
    free(buffer);
    if (root && !root_valid(root)) { json_decref(root); root=NULL; }
    return root;
}
static size_t find_context(json_t *contexts,const char *url,const char *user)
{
    size_t i; json_t *context;
    json_array_foreach(contexts,i,context)
        if (!strcmp(text(context,"url",2048),url) && !strcmp(text(context,"user_id",36),user)) return i;
    return SIZE_MAX;
}
json_t *read_state_load(const char *url,const char *user)
{
    if (!scope_valid(url,user)) return json_object();
    int directory=directory_open();
    json_t *root=directory>=0 ? load(directory) : NULL;
    if (directory>=0) close(directory);
    json_t *contexts=json_object_get(root,"contexts");
    size_t index=find_context(contexts,url,user);
    json_t *map=index!=SIZE_MAX ? json_deep_copy(json_object_get(json_array_get(contexts,index),"messages")) : json_object();
    json_decref(root); return map;
}
static int compare(const json_t *a,const json_t *b)
{
    int order=0; session_time_compare(text(a,"created_at",64),text(b,"created_at",64),&order);
    return order ? order : strcasecmp(text(a,"id",36),text(b,"id",36));
}
static bool save(int directory,json_t *root)
{
    char *data=json_dumps(root,JSON_COMPACT);
    while (data && strlen(data)>STATE_BYTES && json_array_size(json_object_get(root,"contexts"))>1) {
        free(data); json_array_remove(json_object_get(root,"contexts"),0); data=json_dumps(root,JSON_COMPACT);
    }
    if (!data || strlen(data)>STATE_BYTES) { free(data); return false; }
    char temporary[96]; int fd=-1;
    for (int attempt=0;attempt<16 && fd<0;++attempt) {
        snprintf(temporary,sizeof(temporary),"read-cursors.%ld.%d.tmp",(long)getpid(),attempt);
        fd=openat(directory,temporary,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);
        if (fd<0 && errno!=EEXIST) break;
    }
    bool ok=fd>=0; size_t length=strlen(data),written=0;
    while (ok && written<length) {
        ssize_t n=write(fd,data+written,length-written);
        if (n<0 && errno==EINTR) continue;
        if (n<=0) ok=false; else written+=(size_t)n;
    }
    free(data);
    if (ok && fsync(fd)) ok=false;
    if (fd>=0 && close(fd)) ok=false;
    if (ok && renameat(directory,temporary,directory,file_name)) ok=false;
    if (ok && fsync(directory)) ok=false;
    if (!ok && fd>=0) unlinkat(directory,temporary,0);
    return ok;
}
bool read_state_save(const char *url,const char *user,const json_t *messages)
{
    if (!scope_valid(url,user) || !map_valid(messages)) return false;
    int directory=directory_open(); if (directory<0) return false;
    int lock=openat(directory,"read-cursors.lock",O_RDWR|O_CREAT|O_NOFOLLOW|O_CLOEXEC|O_NONBLOCK,0600);
    if (lock<0 || !private_file(lock) || flock(lock,LOCK_EX|LOCK_NB)) {
        if (lock>=0) close(lock);
        close(directory); return false;
    }
    json_t *root=load(directory),*contexts=json_object_get(root,"contexts");
    size_t index=find_context(contexts,url,user);
    json_t *merged=index!=SIZE_MAX ? json_deep_copy(json_object_get(json_array_get(contexts,index),"messages")) : json_object();
    bool ok=root && merged;
    const char *id; json_t *cursor;
    json_object_foreach((json_t *)messages,id,cursor) {
        json_t *previous=json_object_get(merged,id);
        if (ok && (!previous || compare(cursor,previous)>0)) ok=json_object_set(merged,id,cursor)==0;
    }
    while (ok && json_object_size(merged)>READ_STATE_LIMIT)
        json_object_del(merged,json_object_iter_key(json_object_iter(merged)));
    json_t *context=ok ? json_pack("{s:s,s:s,s:O}","url",url,"user_id",user,"messages",merged) : NULL;
    if (context) {
        if (index!=SIZE_MAX) json_array_remove(contexts,index);
        ok=json_array_append(contexts,context)==0;
        while (json_array_size(contexts)>CONTEXT_LIMIT) json_array_remove(contexts,0);
        if (ok) ok=save(directory,root);
    } else ok=false;
    json_decref(context); json_decref(merged); json_decref(root);
    close(lock); close(directory); return ok;
}
