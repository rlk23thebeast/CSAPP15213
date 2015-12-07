#include <stdio.h>
#include "csapp.h"
#include "cache.h"

#define DEBUG
#ifdef DEBUG
# define dbg_printf(...) printf(__VA_ARGS__)
#else
# define dbg_printf(...)
#endif

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/*
 * Defined indices for identify flags in the flag array
 * to check wether the request header contains the corresponding fields
 */
#define HOST             0
#define USER_AGENT       1
#define ACCEPT           2
#define ACCEPT_ENCODING  3
#define CONNECTION       4
#define PROXY_CONNECTION 5

/* Constant strings for constructing request/response header */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 \
(X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";
static const char *accept_str = "Accept: text/html,\
application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8\r\n";
static const char *accept_encoding_str = "Accept-Encoding: gzip, deflate\r\n";
static const char *connection_str = "Connection: close\r\n";
static const char *proxy_connection_str = "Proxy-Connection: close\r\n";
static const char *get_str = "GET ";
static const char *version_str = " HTTP/1.0\r\n";
static const char *method_error_str = "Not implemented.\
Server only implements GET method.\r\n";
static const char *uri_error_str = "Not found. Invalid URI.\r\n";
static const char *invalid_request_response_str = "HTTP/1.1 400 \
Bad Request\r\nConnection: close\r\nContent-Type: text/html\r\n\r\n \
<html><head></head><body><p>Webpage not found.</p></body></html>";

/* cache for the proxy */
cache_list_t* cache_list = NULL;

int main(int argc, char **argv) {

    int listenfd, *connfd, port;
    struct sockaddr_in clientaddr;
    socklen_t clientlen;
    pthread_t tid;

    sem_t mutex;

    Signal(SIGPIPE, SIG_IGN);   // ignore SIGPIPE signal

    // check whether the input argument is legal
    if (argc != 2) {
        printf("Illegal Argument.\n");
        exit(0);
    }

    // check whether the input port is legal numbers
    if (!isValidPort(argv[1])) {
        printf("Illegal Port.\n");
        exit(0);
    }

    port = atoi(argv[1]);   // get the port number

    // check whether the port is within the legal range
    if ((port < 1000) || (port > 65535)) {
        printf("Illegal port.\n");
    }

    Sem_init(&mutex, 0, 1);
    cache_list = init_cache_list();     // initialize cache list
    listenfd = Open_listenfd(port);     // ready for client request

    while (1) {

        clientlen = sizeof(struct sockaddr_in);
        connfd = Malloc(sizeof(int));
        connfd = Accept(listenfd, (SA *) &clientaddr, &clientlen);
        Pthread_create(&tid, NULL, thread, connfd);

    }

    return 0;

}

/*
 * thread - Thread routine
 */
void *thread(void *vargp) {

    int connfd = *((int *)vargp);
    Pthread_detach(pthead_self);
    Free(vargp);
    echo(connfd);   // main function for the proxy behavior
    Close(connfd);
    return NULL;

}

void echo(int fd) {
    rio_t rio;
    char buf[MAXLINE], req_buf[MAXLINE], req_header_buf[MAXLINE];
    char method[MAXLINE], uri[MAXLINE], version[MAXLINE];
    char protocal[MAXLINE];
    char resource[MAXLINE];
    char remote_host_name[MAXLINE], remote_host_port[MAXLINE];
    char uri_check[7];
    char cache_id[MAXLINE], cache_content[MAX_OBJECT_SIZE];
    int server_forward_fd;

    int flag[6];    // flag array to indentify request head settings
    for (int i = 0; i < 6; i++) {
        flag[i] = 0;
    }

    // read the request from the connfd
    Rio_readinitb(&rio, fd);
    if (Rio_readlineb(&rio, buf, MAXLINE) == -1) {
        printf("Null request.\n");
        return NULL;
    }

    // get the content of the request
    sscanf(buf, "%s %s %s", method, uri, version);

    // check whether the request method is legal (only implement GET)
    if (strcmp(method, "GET")) {
        if (Rio_writen(fd, method_error_str, strlen(method_error_str)) == -1) {
            printf("rio_writen error.\n");
            return;
        }
        printf("Not implemented. Sever only implements GET method.\n");
        return;
    }

    // check whether the request uri is legal
    strncpy(uri, uri_check, 7);
    if (strcmp(uri_check, "http://")) {
        if (Rio_writen(fd, uri_error_str, strlen(uri_error_str)) == -1) {
            printf("rio_writen error.\n");
            return;
        }
        printf("Not found. Invalid URI.\n");
        return;
    }
    // parse needed information from the client request
    parse_uri(uri, remote_host_name, remote_host_port, protocol, resource);

    // generate cache id (GET www.cmu.edu:80/home.html HTTP/1.0)
    strcpy(cache_id, method);
    strcat(cache_id, " ");
    strcat(cache_id, remote_host_name);
    strcat(cache_id, ":");
    strcat(cache_id, remote_host_port);
    strcat(cache_id, resource);
    strcat(cache_id, " ");
    strcat(cache_id, version);

    /*
     * check whether the request page is in cache
     * if hit, return to the client directly; if not, request from server
     */
    if (read_cache_list(cache_list, cache_id, cache_content)) {
        if (Rio_writen(fd, cache_content, strlen(cache_content)) < 0) {
            printf("rio_writen error from cache.\n");
        }
    } else {

        // generate request line
        strcpy(req_buf, method);
        strcat(req_buf, " ");
        strcat(req_buf, resource);
        strcat(req_buf, " ");
        strcat(req_buf, version);
        strcat(req_buf, "\r\n");
        // generate request headers according to the client header
        while (Rio_readlineb(&rio, buf, MAXLINE) != 0) {
            generate_request_header(buf, req_header_buf, flag);
        }
        // check whether request header contains all the required information
        check_request_header(req_header_buf, flag, remote_host_name);
        // generate complete request string
        strcat(req_buf, request_header);
        strcat(req_buf, "\r\n");

        // TODO: read from server and write to cache
    }

}

static int request_from_server(int* clientfd, char* req_buf, cache_list_t* list) {

    int serverfd;

    if (clientfd == NULL) {
        printf("clientfd error.\n");
        return 0;
    }

    if (req_buf == NULL) {
        printf("request error.\n");
        return 0;
    }

    if (list == NULL) {
        printf("cache list empty error.\n");
        return 0;
    }

    /*
     * Request to server
     */
    P(&mutex);
    // open listenfd to establish connection to server
    if (server_forward_fd = Open_listenfd(remote_host_name, remote_host_port) < 0) {
        printf("Connection to server error.\n");
        if (Rio_writen(fd, invalid_request_response_str, strlen(invalid_request_response_str)) == -1) {
            printf("rio_writen error.\n");
        }
        // when error happens, safely close client connfd and server connfd, then exit
        close_fd(&fd, &server_forward_fd);
        Pthread_exit(NULL);
    } else {
        if (Rio_writen(server_forward_fd, req_buf, strlen(req_buf)) < 0) {
            printf("Request to server error.\n");
            if (Rio_writen(fd, invalid_request_response_str, strlen(invalid_request_response_str)) == -1) {
                printf("rio_writen error.\n");
            }
            close_fd(&fd, &server_forward_fd);
            Pthread_exit(NULL);
        }

        if (generate_response(fd, server_forward_fd) == -1) {
            printf("Generate client response error.\n");
        }

        close_fd(&fd, &server_forward_fd);
        Pthread_exit(NULL);

    }
    V(&mutex);
}

/*
 * generate_response - helper function to generate response to the client
 */
static int generate_response(int clientfd, int serverfd) {
    rio_t rio;
    char buf[MAXLINE];
    unsigned int line_length = 0;

    // asscociate the serverfd with the read buffer
    Rio_readinitb(&rio, serverfd);

    // read the server response status line
    if (Rio_readlineb(&rio, buf, MAXLINE) == -1) {
        // read response status error
        printf("rio_readline response status error.\n");
        return -1;
    }
    if (Rio_writen(clientfd, buf, strlen(buf)) == -1) {
        // write to the clientfd error
        printf("rio_writen response status error.\n");
        return -1;
    }

    // read the server response header
    if (Rio_readlineb(&rio, buf, MAXLINE) == -1) {
        printf("rio_readline response header error.\n");
        return -1;
    }
    while (strcmp(buf, "\r\n") != 0) {

        // write a line of header to the clientfd
        if (Rio_writen(clientfd, buf, strlen(buf)) == -1) {
            printf("rio_writen response header error.\n");
            return -1;
        }
        // keep reading lines from the serverfd
        if (Rio_readlineb(&rio, buf, MAXLINE) == -1) {
            printf("rio_readline response header error.\n");
            return -1;
        }

    }

    // read the server response body
    while ((line_length = Rio_readnb(&rio, buf, MAXLINE)) > 0) {
        if (Rio_writen(clientfd, buf, line_length) == -1) {
            printf("rio_writen response body error.\n");
            return -1;
        }
    }
    if (line_length == -1) {
        printf("rio_readnb response body error.\n");
        return -1;
    }
    return 0;
}

/*
 * generate_request_header - helper function to generate request header
 */
static void generate_request_header(char* buf, char* request_header, int* flag) {

    if (strcmp(buf, "\r\n") == 0) {
        return flag;
    } else {
        if (strstr(buf, "Host:") != NULL) {
            strcat(request_header, buf);
            flag[HOST] = 1;
        } else if (strstr(buf, "User-Agent:") != NULL) {
            strcat(request_header, user_agent_hdr);
            flag[USER_AGENT] = 1;
        } else if (strstr(buf, "Accept:") != NULL) {
            strcat(request_header, accept_str);
            flag[ACCEPT] = 1;
        } else if (strstr(buf, "Accept-Encoding:") != NULL) {
            strcat(request_header, accept_encoding_str);
            flag[ACCEPT_ENCODING] = 1;
        } else if (strstr(buf, "Connection:") != NULL) {
            strcat(request_header, connection_str);
            flag[CONNECTION] = 1;
        } else if (strstr(buf, "Proxy-Connection:") != NULL) {
            strcat(request_header, proxy_connection_str);
            flag[PROXY_CONNECTION] = 1;
        } else {
            strcat(request_header, buf);
        }

    }
    return flag;
}

/*
 * check_request_header - check to ensure that required information is all contained
 *                        in the request header
 */
static void check_request_header(char* request_header, int *flag, char* remote_host_name) {

    if (!flag[HOST]) {
        strcat(request_header, "Host: ");
        strcat(request_header, remote_host_name);
        strcat("\r\n");
        flag[HOST] = 1;
    }
    if (!flag[USER_AGENT]) {
        strcat(request_header, user_agent_hdr);
        flag[USER_AGENT] = 1;
    }
    if (!flag[ACCEPT]) {
        strcat(request_header, accept_str);
        flag[ACCEPT] = 1;
    }
    if (!flag[ACCEPT_ENCODING]) {
        strcat(request_header, accept_encoding_str);
        flag[ACCEPT_ENCODING] = 1;
    }
    if (!flag[CONNECTION]) {
        strcat(request_header, connection_str);
        flag[CONNECTION] = 1;
    }
    if (!flag[PROXY_CONNECTION]) {
        strcat(request_header, proxy_connection_str);
        flag[PROXY_CONNECTION] = 1;
    }

}

/*
 * parse_uri - helper function to parse the fields in the request uri string
 */
static void parse_uri(char *uri, char *host_name, char *host_port, char *protocal, char *resource) {

    char host_name_port[MAXLINE];
    char *tmp;
    // check whether the uri contains a protocal
    if (strstr(uri, "://") != NULL) {
        // the uri contains a protocal
        sscanf(uri, "%[^:]://%[^/]%s", protocol, host_name_port, resource);
    } else {
        // the uri doesn't contain a protocal
        sscanf(uri, "%[^/]%s", host_name_port, resource);
    }

    // get the remote host name and remot host port
    tmp = strstr(host_name_port, ":");
    if (tmp != NULL) {
        // if there is a host port, cut the str to "name" "port"
        *tmp = "\0";
        // get the host port
        strcpy(host_port, tmp + 1);
    } else {
        // if there is no host port, set it as default 80
        strcpy(host_port, "80");
    }
    // set the host name
    strcpy(host_name_port, host_name);

}

/*
 * isValidPort - Validate the given port is valid Number.
 *               return 1, if valid; return 0, if not.
 */
static int isValidPort(char *port) {

    int flag = 1;

    for (int i = 0; i < strlen(port); i++) {
        // determine each char is within the ASCII of 0-9
        if (*port < '0' || *port > '9') {
            flag = 0;
            break;
        }
        port++;

    }

    return flag;
}
