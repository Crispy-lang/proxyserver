/* 
 * proxy.c - A simple HTTP Proxy that caches web objects
 *
 * Timothy Kaboya - tkaboya
 *
 * Key Functionalities
 *    1. Accept connections and parse requests back to clients
 *    2. Handles multiple concurrent connections
 *    3. Caches some object using a Most Recently Used List
 *
 */

#include <stdio.h>
#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400
#define MAX_PORT_SIZE 6 

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\n";

void doit(int clientfd);
void read_requesthdrs(rio_t *rp);
void build_get(char *http_hdr, char * method, char *path, char *version); 
void build_requesthdrs(rio_t *rpi, char *http_hdr, char *host); 
void read_n_send(int serverfd, int clientfd);
void parse_uri(char *uri, char *host, char *port, char *path);
void serve_static(int fd, char *filename, int filesize);
void get_filetype(char *filename, char *filetype);
void serve_dynamic(int fd, char *filename, char *cgiargs);
void clienterror(int fd, char *cause, char *errnum, 
        char *shortmsg, char *longmsg);


int main(int argc, char **argv) 
{
    int listenfd, connfd;
    char hostname[MAXLINE], port[MAXLINE];
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;

    /* Default code */
    printf("%s\n", user_agent_hdr); 


    /* Check command line args */
    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }

    listenfd = Open_listenfd(argv[1]);
    while (1) {
        clientlen = sizeof(clientaddr);
        connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen); //line:netp:tiny:accept
        Getnameinfo((SA *) &clientaddr, clientlen, hostname, MAXLINE, 
                port, MAXLINE, 0);
        printf("Accepted connection from (%s, %s)\n", hostname, port);
        doit(connfd);                                             //line:netp:tiny:doit
        Close(connfd);                                            //line:netp:tiny:close
    }
}

/*
 * doit - handle one HTTP request/response transaction
 */
/* $begin doit */
void doit(int clientfd) 
{
    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
    char http_hdr[MAXLINE], host[MAXLINE], path[MAXLINE];
    char port[MAX_PORT_SIZE]; 

    int serverfd; 

    rio_t rio_c, rio_s;

    /* Read request line and headers */
    Rio_readinitb(&rio_c, clientfd);
    if (!Rio_readlineb(&rio_c, buf, MAXLINE))  //line:netp:doit:readrequest
        return;
    sscanf(buf, "%s %s %s", method, uri, version);       //line:netp:doit:parserequest
    strcpy(version, "HTTP/1.0");                    
    if (strcasecmp(method, "GET")) {                     //line:netp:doit:beginrequesterr
        clienterror(clientfd, method, "501", "Not Implemented",
                "Proxy Server does not implement this method");
        return;
    }                                                    //line:netp:doit:endrequesterr

    /* Parse URL into host, path, port  */
    parse_uri(uri, host, port, path);       //line:netp:doit:staticcheck
    /* Form new HTTP Request and send it to server */ 
    build_get(http_hdr, method, path, version);
    build_requesthdrs(&rio_c, http_hdr, host); 
    printf("%s", http_hdr);

    /* Open Connection to Server. TODO: Error check */ 
    if((serverfd = open_clientfd(host, port)) < 0) {
        clienterror(clientfd, method, "400", "Bad Request",
                "Malformed URL");
        return;
    }
    /* Listen for response from server and forward to client fd */ 
    Rio_readinitb(&rio_s, serverfd); 
    Rio_writen(serverfd, http_hdr, strlen(http_hdr));

    /* Reads from server and sends to client */
    read_n_send(serverfd, clientfd);

    Close(serverfd);
}
/* $end doit */

/* 
 * build_get - Adds custom GET to new http Request
 *  Uses HTTP/1.0 
 */
void build_get(char *http_hdr, char *method, char *path, char *version)
{
    strcpy(http_hdr, method);
    strcat(http_hdr, " ");
    strcat(http_hdr, path); 
    strcat(http_hdr, " "); 
    strcat(http_hdr, version); 
    strcat(http_hdr, "\n"); 
}


/*
 * read_requesthdrs - read HTTP request headers and output them.
 */
/* $begin read_requesthdrs */
void read_requesthdrs(rio_t *rp) 
{
    char buf[MAXLINE];

    Rio_readlineb(rp, buf, MAXLINE);
    printf("%s", buf);
    while(strcmp(buf, "\r\n")) {          //line:netp:readhdrs:checkterm
        Rio_readlineb(rp, buf, MAXLINE);
        printf("%s", buf);
    }
    return;
}
/* $end read_requesthdrs */

/*
 * build_requesthdrs - build HTTP request headers, print it then send
 * to server.
 */
/* $begin build_requesthdrs */
void build_requesthdrs(rio_t *rp, char *http_hdr, char *host) 
{
    char buf[MAXLINE];

    Rio_readlineb(rp, buf, MAXLINE);
    if (!strstr(buf, "Host: ")) {
        strcat(http_hdr, "Host: ");
        strcat(http_hdr, host);
        strcat(http_hdr, "\n");
    }
    else 
        strcat(http_hdr, buf);

    while(strcmp(buf, "\r\n")) {          //line:netp:buildhdrs:checkterm
        Rio_readlineb(rp, buf, MAXLINE);

        /* Changes to header, change User$-Agent and Connection hdrs */
        if (strstr(buf, "User-Agent: "))
            strcat(http_hdr, user_agent_hdr);
        else if (strstr(buf, "Connection: ")) {
            strcat(http_hdr, "Connection: close\n");
            strcat(http_hdr, "Proxy-Connection: close\n");
        }
        else 
            strcat(http_hdr, buf);
    }
    return;
}
/* $end build_requesthdrs */


/*
 * read_n_send - Reads from server and forwards to client
 */
/* $begin read_n_send */
void read_n_send(int serverfd, int clientfd)
{
    int n;
    char buf[MAXLINE];

    /* Read from server and send to client */
    while((n = rio_readn(serverfd, buf, MAXLINE)) > 0) {
        if((rio_writen(clientfd, buf, n) != n)) {
            clienterror(clientfd, "GET", "400", "Bad Request",
                    "Client not understood due to malformed syntax");
            return;
        }
    }

    /* Handling invalid response from upstream server */
    if (n < 0) {
        clienterror(clientfd, "GET", "502", "Bad Gateway",
                "Client not understood due to malformed syntax");
    }

}
/* $end read_n_send */

/*
 * parse_uri - parse URI into host, path and port
 * Function can handle URLs with http://, without it, 
 * Unless specified, Default port and path are 80, and /
 *
 * This method does alot of string parsing/manipulation
 * Each string is build letter by letter and a null terminator
 * added at the end. 
 */
/* $begin parse_uri */
void parse_uri(char *uri, char *host, char *port, char *path) 
{
    printf("Url: %s\n", uri);
    char *curr, *next;
    *port = '\0';
    *path = '\0';
    *host = '\0';
    curr = uri;
    /* Skip over http if in uri */
    if ((strstr(uri, "http://") || (strstr(uri, "HTTP://")))) {
        curr += strlen("http://");
    }
    /* Parsing host with port*/
    if ((next = strpbrk(curr, ":"))) {
        strncpy(host, curr, next - curr);
        host[next-curr] = 0;
        
        /* Skipping over ":", we dont keep this */
        next++;
        curr = next; 
        /* Parsing Port*/
        if((next = strpbrk(curr, "/"))) {
            strncpy(port, curr, next - curr);
            port[next-curr] = 0;
            curr = next;
            /* Parsing remaining path */
            strcpy(path, curr);
            path[strlen(curr)] = 0;
        }
        /* Host has no path, so we just build the port */
        else {
            strcpy(port, curr);
            port[strlen(curr)] = 0;
        }
    }
    /* Parsing host with no port */
    else {
        /* Parse host now */
        if ((next = strpbrk(curr, "/"))) { 
            strncpy(host, curr, next - curr);      
            host[next-curr] = 0;

            curr = next;
            /* Parsing path now */
            strcpy(path, curr);
            strcat(path, "\0");
        }
        /* Host has no path */
        else {
            strcpy(host, curr);
            host[strlen(curr)] = 0;
        }
    }

    /* If path or port still empty, use default values */
    if (*path == 0) {
        strncpy(path, "/", 1);
        path[1] = 0;
    }
    if (*port == 0) {
        strncpy(port, "80", 2);
        port[2] = 0;
    }

    printf("\nHost+Port+Path: %s+%s+%s\n\n", host, port, path);
}
/* $end parse_uri */

/* $end serve_dynamic */

/*
 * clienterror - returns an error message to the client
 */
/* $begin clienterror */
void clienterror(int fd, char *cause, char *errnum, 
        char *shortmsg, char *longmsg) 
{
    char buf[MAXLINE], body[MAXBUF];

    /* Build the HTTP response body */
    sprintf(body, "<html><title>Proxy Server Error</title>");
    sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
    sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
    sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
    sprintf(body, "%s<hr><em>Tim's Proxy Web server</em>\r\n", body);

    /* Print the HTTP response */
    sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-type: text/html\r\n");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
    Rio_writen(fd, buf, strlen(buf));
    Rio_writen(fd, body, strlen(body));
}
/* $end clienterror */
