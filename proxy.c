#define DEBUG
#include "csapp.h"

#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

#define S_PORT 80 /* Default server port*/ 

static const char *user_agent = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";
static const char *accepts = "Accept: text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8\r\n";
static const char *accept_encoding = "Accept-Encoding: gzip, deflate\r\n";
static const char *connection = "Connection: close\r\n";
static const char *proxy_connection = "Proxy-Connection: close\r\n";

void doproxy(int fd);
void clienterror(int fd, char *cause, char *errnum,
        char *shortmsg, char *longmsg);
int read_requesthdrs(rio_t *rio, char *buf);
int parse_uri(char *uri, char *furi, char *host);
void fwdreq2server(int server_fd, char *req);
void fwdres2client(int client_fd, char *res, size_t size);

/*void sigpipe_handler(int sig)
{
    dbg_printf("Receive the sigpipe signal!\n");
}*/

int main(int argc, char **argv)
{
    int listenfd, connfd, port, clientlen;
    struct sockaddr_in clientaddr;

    Signal(SIGPIPE, SIG_IGN);

    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }
    port = atoi(argv[1]);

    listenfd = Open_listenfd(port);
    while (1) {
        clientlen = sizeof(clientaddr);
        connfd = Accept(listenfd, (SA *)&clientaddr, (socklen_t *)&clientlen);
        doproxy(connfd);
        Close(connfd);
    }

    return 0;
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
    char req[MAXBUF], res[MAXBUF], reqbuf[MAXBUF];
    int p2s;  /* fd from proxy to server*/ 
    ssize_t size;
    rio_t rio_client, rio_server;

    /* Get HTTP request and header information from client */
    Rio_readinitb(&rio_client, clientfd);
    Rio_readlineb(&rio_client, buf, MAXLINE);
    sscanf(buf, "%s %s %*s", method, uri);
    if (strcasecmp(method, "GET")) {
        clienterror(clientfd, method, "501", "Not Implemented",
                "Proxy does not support method other than GET");
        return;
    }
    hdr_res = read_requesthdrs(&rio_client, reqbuf);

    /* Forward the HTTP request to the server */
    port = parse_uri(uri, furi, host);
    if(!hdr_res) {
        sprintf(req, "Host %s\r\n%s", host, reqbuf);
        sprintf(reqbuf, "%s", req);
    }
    sprintf(req, "GET %s HTTP/1.0\r\n%s", furi, reqbuf);
    dbg_printf("The request to the server is \r\n%s", req);
    if(port == 0)
        p2s = open_clientfd(host, S_PORT);    
    else
        p2s = open_clientfd(host, port);

    if (p2s == -1) { 
        return;
    }
    fwdreq2server(p2s, req);

    /* Get feed back from server */
    Rio_readinitb(&rio_server, p2s);
    while((size = Rio_readnb(&rio_server, res, MAXBUF)) != 0) {
        fwdres2client(clientfd, res, size);
    }
}

/*
 * open_clientfdr - a wrapper function of open_clientfd()
 * 404 not found
 */
int open_clientfdr(char *hostname, int port)
{
    int res = open_clientfd(hostname, port);

    /* Handle the error condition, the server should run forever*/ 
    if (res == -1) {
        return -1;
    }
    else if (res == -2) {
        return -1;
    }
    else 
        return res;
}

/*
 * read_requesthdrs - read the header from client rio and then
 * store in req a new request header which will be forward to server later
 * Return 0 if the original header does not contain a Host
 * Return 1 otherwise on success
 */
int  read_requesthdrs(rio_t *rio, char *req)
{
    char buf[MAXLINE];
    char host[MAXLINE];
    int ret = 0;

     /* todo: SIGPIPE should be handle */ 
    do{
        rio_readlineb(rio, buf, MAXLINE);
        if (strstr(buf, "Host")) {
            ret = 1;
            sscanf(buf, "%*[^:]: %s", host);
        }
    }while(!strcmp(buf, "\r\n"));

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
        sscanf(host, "%[^:]:%d", host, &port);
        return port;
    }

    return 0;
}

/*
 * fwdreq2server - forward the requeset to server
 */
void fwdreq2server(int server_fd, char *req)
{
    rio_writen(server_fd, req, strlen(req));
}

/*
 * fwdres2client - forward the result to client
 */
void fwdres2client(int client_fd, char *res, size_t size)
{
    rio_writen(client_fd, res, size);
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
