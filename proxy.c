#include "csapp.h"
#include "cache.h"


#define S_PORT 80 /* Default server port*/ 

static const char *user_agent = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";
static const char *accepts = "Accept: text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8\r\n";
static const char *accept_encoding = "Accept-Encoding: gzip, deflate\r\n";
static const char *connection = "Connection: close\r\n";
static const char *proxy_connection = "Proxy-Connection: close\r\n";

void *task (void *vargp);
void doproxy(int fd);
void clienterror(int fd, char *cause, char *errnum,
        char *shortmsg, char *longmsg);
int read_requesthdrs(rio_t *rio, char *buf);
void get_reshdrs(rio_t *server, char* reshdrs);
int parse_uri(char *uri, char *furi, char *host);
void fwdreq2server(int server_fd, char *req);
void fwdres2client(int client_fd, char *res, size_t size);
void fwdobj2client(int client_fd, cacheobj *obj);

/* The cache */ 
pxycache *Pxycache;

int main(int argc, char **argv)
{
    int listenfd, port, clientlen;
    struct sockaddr_in clientaddr;
    pthread_t tid;

    /* Init the cache */ 
    Pxycache = Malloc(sizeof(pxycache));
    init_cache(Pxycache);

    Signal(SIGPIPE, SIG_IGN);

    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }
    port = atoi(argv[1]);

    listenfd = Open_listenfd(port);
    if (listenfd == -1) {
       fprintf(stderr, "The port may be unavalible\n");
       return 0;
    }

    while (1) {
        int *connfdp;
        connfdp = Malloc(sizeof(int));
        clientlen = sizeof(clientaddr);
        *connfdp = Accept(listenfd, (SA *)&clientaddr, (socklen_t *)&clientlen);
        Pthread_create(&tid, NULL, (void *)task, (void *)connfdp);
    }

    return 0;
}

/*
 * task - the job function for multithreads
 */
void *task (void *vargp) {
    int connfd = *((int *)vargp);
    Pthread_detach(pthread_self());
    Free(vargp);
    doproxy(connfd);
    Close(connfd);
    return NULL;
}

/*
 * doproxy - handle the proxy operations for a client 
 * No cache version:
 * 1. Get HTTP request and header information from client
 * 2. Forward the request and header information to the server
 * 3. Get response from server and forward it back to client
 */
void doproxy(int clientfd)
{
    int hdr_res, port;
    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE];
    char furi[MAXLINE]; /* Formated URI */ 
    char host[MAXLINE];
    char req[MAXBUF], resbuf[MAXBUF], reqbuf[MAXBUF];
    char res[MAXBUF];
    int p2s;  /* fd from proxy to server*/ 
    ssize_t size;
    rio_t rio_client, rio_server;
    char *original_uri;

    /* Get HTTP request and header information from client */
    Rio_readinitb(&rio_client, clientfd);
    if (Rio_readlineb(&rio_client, buf, MAXLINE) < 0) {
        return;
    }
    sscanf(buf, "%s %s %*s", method, uri);
    if (strcasecmp(method, "GET") != 0) {
        clienterror(clientfd, method, "501", "Not Implemented",
                "Proxy does not support method other than GET");
        return;
    }
    hdr_res = read_requesthdrs(&rio_client, reqbuf);
    if (hdr_res == -1) {
        return;
    }

    /* Forward the HTTP request to the server */
    port = parse_uri(uri, furi, host);
    port = ((port == 0) ? S_PORT:port);
    if (!hdr_res) {
        sprintf(req, "Host %s\r\n%s", host, reqbuf);
        sprintf(reqbuf, "%s", req);
    }
    sprintf(req, "GET %s HTTP/1.0\r\n%s", furi, reqbuf);
    /*dbg_printf("The request to the server is \r\n%s", req);*/

    /* If the requested object was cached, forward the object to client*/
    cacheobj *obj;
    if ((obj = get_obj_from_cache(Pxycache, uri)) != NULL) {
        printf("Cache hit !\n");
        fwdobj2client(clientfd, obj);
        obj_read_done(Pxycache);
    }
    else {
        /* If the object was not cached, send the request to server and try to
         * cache the object */
        printf("Cache miss !\n");
        p2s = Open_clientfd(host, port);

        if (p2s == -1) { 
            clienterror(clientfd, host, "400", "Bad Request",
                    "The host name or port number maybe invalid");
            return;
        }
        fwdreq2server(p2s, req);

        /* Get feed back from server */
        Rio_readinitb(&rio_server, p2s);

        /* Read the response from the server, parse the response header, create the cache obj
         * and store the object to Pxycache */ 
        get_reshdrs(&rio_server, res);
        fwdres2client(clientfd, res, strlen(res));
        char *reshdrs;
        reshdrs = Malloc(strlen(res)+1);
        strcpy(reshdrs, res);
        char content[MAX_OBJECT_SIZE];
        
        size_t tmp_size = 0;
        while((size = Rio_readnb(&rio_server, resbuf, MAXBUF)) != 0) {
            if (size < 0)
                return;
            fwdres2client(clientfd, resbuf, size);
            tmp_size += size;
            if (tmp_size <= MAX_OBJECT_SIZE)
                memcpy((char *)(content + tmp_size - size), resbuf, size);
            memset(resbuf, 0, MAXBUF);
        }

        if (tmp_size != 0) {
            /* store the original_uri*/ 
            original_uri = Malloc(strlen(uri)+1);
            strcpy(original_uri, uri);

            cacheobj *tmp_obj;
            tmp_obj = Malloc(sizeof(cacheobj));
            init_obj(tmp_obj, original_uri, content, tmp_size, reshdrs);
            insert_object(Pxycache, tmp_obj);
        }
#ifdef DEBUG
        check_cache(Pxycache);
#endif
        Close(p2s);
    }
    return;
}

/*
 * read_requesthdrs - read the header from client rio and then
 * store in req a new request header which will be forward to server later
 * Return 0 if the original header does not contain a Host
 * Return 1 otherwise on success
 * Return -1 on error
 */
int  read_requesthdrs(rio_t *rio, char *req)
{
    char buf[MAXLINE];
    char host[MAXLINE];
    int ret = 0;

    do{
        if ((Rio_readlineb(rio, buf, MAXLINE)) < 0)
            return -1;
        if (strstr(buf, "Host")) {
            ret = 1;
            sscanf(buf, "%*[^:]: %s", host);
        }
    }while(strcmp(buf, "\r\n") != 0);

    /* Format a new req which will be foward to server later*/ 
    if (ret == 1)
        sprintf(req, "Host: %s\r\n", host);
    sprintf(req, "%s%s", req, user_agent);
    sprintf(req, "%s%s", req, accepts);
    sprintf(req, "%s%s", req, accept_encoding);
    sprintf(req, "%s%s", req, connection);
    sprintf(req, "%s%s\r\n", req, proxy_connection);

    return ret;
}

/*
 * get_reshdrs - get response headers from server
 */
void get_reshdrs(rio_t *server, char* reshdrs)
{
    char buf[MAXLINE];

    strcpy(reshdrs, "");
    Rio_readlineb(server, buf, MAXLINE);
    while (strcmp(buf, "\r\n") != 0) {
        strcat(reshdrs, buf);
        Rio_readlineb(server, buf, MAXLINE);
    }
    strcat(reshdrs, "\r\n");
}

/*
 * parse_uri - parse the current uri such as http:// into a formatted
 * uri(furi) without hostname and a hostname
 * If the uri contains a port information, return the port number
 * Else retun 0
 */
int parse_uri(char *uri, char *furi, char *host)
{
    int port;

    if (strstr(uri, "//")) {
        sscanf(uri, "%*[^:]://%[^/]%s", host, furi);
    }
    else {
        sscanf(uri, "%[^/]%s", host, furi);
    }

    if (strstr(host, ":")) {
        char buf[MAXLINE];
        strcpy(buf, host);
        sscanf(buf, "%[^:]:%d", host, &port);
        return port;
    }

    return 0;
}

/*
 * fwdreq2server - forward the requeset to server
 */
void fwdreq2server(int server_fd, char *req)
{
   Rio_writen(server_fd, req, strlen(req));
}

/*
 * fwdres2client - forward the result to client
 */
void fwdres2client(int client_fd, char *res, size_t size)
{
    Rio_writen(client_fd, res, size);
}

/*
 * fwdobj2client - forward the cached object to client
 */
void fwdobj2client(int client_fd, cacheobj *obj)
{
    /* First forward back the response header */ 
    fwdres2client(client_fd, obj->reshdrs, strlen(obj->reshdrs));

    /* Forward back the content */ 
    fwdres2client(client_fd, obj->content, obj->content_size);
}

/*
 *clienterror - returns an error message to the client
 */
void clienterror(int fd, char *cause, char *errnum,
        char *shortmsg, char *longmsg)
{
    char buf[MAXLINE], body[MAXBUF];

    /* Build the HTTP response body */
    sprintf(body, "<html><title>Proxy Error</title>");
    sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
    sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
    sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
    sprintf(body, "%s<hr><em>The Proxy Server</em>\r\n", body);

    /* Print the HTTP response */
    sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-type: text/html\r\n");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content_length: %d\r\n\r\n", (int)strlen(body));
    Rio_writen(fd, buf,strlen(buf));
    Rio_writen(fd, body, strlen(body));
}
