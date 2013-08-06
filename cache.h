/*
 * cache.h include the structure and basic functions of a cache specialy designed
 * for the web proxy. 
 * The common operations involve searching the cache, insert or delete a cached object
 * These operations are thread-safe
 */

#ifndef __CACHE_H__
#define __CACHE_H__

#include "csapp.h"

#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

typedef struct cache_object 
{
    char *uri;
    size_t content_size;
    char *content;
    char *reshdrs;    /* response headers */ 
    struct cache_object *prev;
    struct cache_object *next;
}cacheobj;

typedef struct cache
{
    size_t cur_size;
    cacheobj *head;
    cacheobj *rear;
}pxycache;

void init_cache(pxycache *Pxycache);
int insert_object(pxycache *Pxycache, cacheobj *obj);
void delete_object(pxycache *Pxycache, cacheobj *obj);
int iscached(pxycache *Pxycache, char* uri); 
cacheobj *get_obj_from_cache(pxycache *Pxycache, char *uri);
void init_obj(cacheobj * obj, char *uri, char *content, size_t content_size, char *reshdrs);
void check_cache(pxycache *Pxycache);

#endif
