#include "cache.h"

/* Static helper function */ 
static void destroy_obj(cacheobj *obj);

/*
 * insert_object - insert an object into cache
 * Return -1 on fail
 * Return 1 on success
 */
int insert_object(pxycache *Pxycache, cacheobj *obj)
{
    size_t content_size = obj->content_size;

    /* if the object size exceeds, return -1 */ 
    if (content_size > MAX_OBJECT_SIZE) {
        dbg_printf("content_size exceeds maximum, disacard!\n");
        destroy_obj(obj);
        return -1;
    }

    if ((content_size + Pxycache->cur_size) <= MAX_CACHE_SIZE) {
        if (Pxycache->head != NULL)
            Pxycache->head->prev = obj;
        obj->next = Pxycache->head;
        Pxycache->head = obj;
        if (obj->next == NULL) {
            Pxycache->rear = obj;
        }
        Pxycache->cur_size += content_size;
    }
    else { /* Need eviction */ 
        size_t tmp = 0;
        while ((content_size + Pxycache->cur_size) > MAX_CACHE_SIZE) {
            dbg_printf("Eviction! size: %d\n", (int)Pxycache->rear->content_size);
            tmp = Pxycache->rear->content_size;
            delete_object(Pxycache, Pxycache->rear);
            Pxycache->cur_size -= tmp;
        }

        if (Pxycache->head != NULL)
            Pxycache->head->prev = obj;
        obj->next = Pxycache->head;
        Pxycache->head = obj;
        if (obj->next == NULL)
            Pxycache->rear = obj;
        Pxycache->cur_size += content_size;
    }

    dbg_printf("insertion complete\n\n");
    return 1;
}

/*
 * delete_object - delete the cache object from the cache
 */
void delete_object(pxycache *Pxycache, cacheobj *obj)
{
    if (obj->next == NULL) {
        Pxycache->rear = obj->prev;
        if (obj->prev != NULL)
            obj->prev->next = NULL;
        else
            Pxycache->head = NULL;
        destroy_obj(obj);
    }
    else {
        if (obj->prev != NULL) {
            obj->prev->next = obj->next;
            obj->next->prev = obj->prev;
        }
        else {
            obj->next->prev = NULL;
            Pxycache->head = obj->next;
        }
        destroy_obj(obj);
    }
}

/*
 * iscached - search the cache for uri
 * Return 1 on cached -1 otherwise
 */
int iscached(pxycache *Pxycache, char* uri) 
{
    cacheobj *tmp;
    tmp = Pxycache->head;

    while (tmp != NULL) {
        if ((strcmp(uri, tmp->uri) == 0)) {
            dbg_printf("cache hit!\n");
            return 1;
        }
        tmp = tmp->next;
    }

    dbg_printf("cache miss!\n");
    return 0;
}

/*
 * get_obj_from_cache - search the cache for uri
 * Return the address of the obj on cached NULL otherwise
 * Notice: this function uses LRU policy
 */
cacheobj *get_obj_from_cache(pxycache *Pxycache, char* uri) 
{
    cacheobj *tmp;
    tmp = Pxycache->head;

    while (tmp != NULL) {
        if ((strcmp(uri, tmp->uri) == 0)) {
            /* LRU: put tmp at the head */ 
            if (tmp->prev != NULL) {
                if (tmp->next == NULL) {
                    Pxycache->rear = tmp->prev;
                    tmp->prev->next = NULL;
                    tmp->next = Pxycache->head;
                    tmp->prev = NULL;
                    Pxycache->head->prev = tmp;
                    Pxycache->head = tmp;
                }
                else {
                    tmp->prev->next = tmp->next;
                    tmp->next->prev = tmp->prev;
                    tmp->next = Pxycache->head;
                    tmp->prev = NULL;
                    Pxycache->head->prev = tmp;
                    Pxycache->head = tmp;
                }
            }
            return tmp;
        }
        tmp = tmp->next;
    }

    return NULL;
}

/*
 * init_cache - init the proxy cache
 */
void init_cache(pxycache *Pxycache)
{
    Pxycache->cur_size = 0;
    Pxycache->head = NULL;
    Pxycache->rear = NULL;
}

/*
 * init_obj - init a cache object
 */
void init_obj(cacheobj * obj, char *uri, char *content, size_t content_size, char *reshdrs) 
{
    obj->content_size = content_size;
    dbg_printf("the content length is %d\n", (int)content_size);
    if (content_size <= MAX_OBJECT_SIZE) {
        obj->uri = uri;
        obj->content = Malloc(content_size);
        memcpy(obj->content, content, content_size);
        obj->reshdrs = reshdrs;
    }
    else { /* If the content_size if larger than max size, make the obj a phony obj 
    with only content_size */ 
        obj->uri = NULL;
        obj->content = NULL;
        obj->reshdrs = NULL;
    }
    obj->prev = NULL;
    obj->next = NULL;
}

/*
 * check_cache - check the cache, print out error information on error
 * 1. Every object's size smaller than Maximum
 * 2. Total size smaller than Maximum
 * 3. The double link list is ok
 * 4. The content_size matches the content
 * 5. The rear is at the end
 */
void check_cache(pxycache *Pxycache)
{
    if (Pxycache->cur_size > MAX_CACHE_SIZE)
        printf("Error: current size in cache exceeds maximum\n");
    
    printf("The current size of the cache is %d\n", (int)Pxycache->cur_size);

    cacheobj *tmp = Pxycache->head;

    while(tmp != NULL) {
        if ((tmp->content == NULL) || (tmp->reshdrs == NULL) || (tmp->uri == NULL))
            printf("Error: important info missing\n");

        if (tmp->content_size > MAX_OBJECT_SIZE)
            printf("Error: the size of the content exceeds maximum\n");

        /*if (tmp->content_size != strlen(tmp->content))
            printf("Error: the content_size %d doesn't match the content %d\nThe content is\
                    \n%s", (int)tmp->content_size, (int)strlen(tmp->content), tmp->content);*/

        if (tmp->prev != NULL) {
            if (tmp->prev->next != tmp)
                printf("Link list error: prev->next doesn't match current\n");
        }

        if (tmp->next != NULL) {
            if (tmp->next->prev != tmp)
                printf("Link list error: next->prev doesn't match current\n");
        }

        if (tmp->next == NULL)
            if (Pxycache->rear != tmp)
                printf("Link list error: rear doesn't macth\n");

        tmp = tmp->next;
    }
}

/*
 * destroy_obj - destroy the obj
 */
static void destroy_obj(cacheobj *obj) 
{
    if (obj->uri != NULL)
        Free(obj->uri);
    if (obj->content != NULL)
        Free(obj->content);
    if (obj->reshdrs != NULL)
        Free(obj->reshdrs);
    Free(obj);
}


