#include "csapp.h"

// #define MAX_CACHE_SIZE 1049000
// #define MAX_OBJECT_SIZE 102400

typedef struct {
  char host[MAXLINE];
  char port[6];
  char uri[MAXLINE];
} end_server_info;

static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n";

int main(int argc, char **argv) {

  int listenfd, connfd, clientfd;
  end_server_info end_server;
  char request_buf[MAXLINE], method[MAXLINE];

  if (argc != 2) {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }

  listenfd = Open_listenfd(argv[1]);
  while (1) {
    connfd = init_listen(listenfd);
    read_request(connfd, request_buf);
    parse_request(method, request_buf, &end_server, connfd);
    int clientfd = send_request_end(connfd, method, &end_server);
    if (clientfd > 0) {
      send_response_client(connfd, clientfd);
    }
    Close(clientfd);
    Close(connfd);
  }
  return 0;
}

/* 1. 클라이언트 연결 */
int init_listen(int listenfd) {
  struct sockaddr_storage clientaddr;
  socklen_t clientlen = sizeof(clientaddr);
  return Accept(listenfd, (SA *)&clientaddr, &clientlen);
}

/* 2. 클라이언트 요청 읽기 */
void read_request(int connfd, char *buf) {
  rio_t rio;
  Rio_readinitb(&rio, connfd);
  Rio_readlineb(&rio, buf, MAXLINE);
}

/* 3. 요청 파싱 */
int parse_request(char *method, char *buf, end_server_info *end_server,
                  int fd) {
  char url[MAXLINE], version[MAXLINE];
  char *p, *host, *port, *uri;

  sscanf(buf, "%s %s %s", method, url, version);
  if (strcasecmp(method, "GET")) {
    proxy_error(fd, method, "501", "Not Implemented",
                "Proxy does not implement this method");
    return -1;
  }

  memset(end_server, 0, sizeof(end_server));

  // 절대주소/상대주소
  if ((p = strstr(url, "://")) != NULL) {
    host = p + 3;
  } else {
    host = url;
  }
  port = strchr(host, ':');
  uri = strchr(host, '/');

  // host
  snprintf(end_server->host, sizeof end_server->host, "%.*s",
           (int)(port - host), host);

  // port
  if (uri) {
    snprintf(end_server->port, sizeof end_server->port, "%.*s",
             (int)(uri - (port + 1)), port + 1);
  } else {
    snprintf(end_server->port, sizeof end_server->port, "%s",
             (int)(uri - (port + 1)), port + 1);
  }

  // uri
  if (uri) {
    snprintf(end_server->uri, sizeof end_server->uri, "%s", uri);
  } else {
    snprintf(end_server->uri, sizeof end_server->uri, "/");
  }
  return 0;
}

/* 4.엔드서버로 HTTP 요청 전달 */
int send_request_end(int connfd, char *method, end_server_info *end_server) {

  int clientfd = Open_clientfd(end_server->host, end_server->port);
  if (clientfd < 0) {
    proxy_error(connfd, "end_server", "403", "Forbidden",
                "Proxy couldn't find end server");
  } else {

    char send[MAXLINE];
    snprintf(send, sizeof send, "%s %s %s\r\n", method, end_server->uri,
             "HTTP/1.0");
    Rio_writen(clientfd, send, strlen(send));

    // 헤더
    snprintf(send, sizeof send, "Host: %s:%s\r\n", end_server->host,
             end_server->port);
    Rio_writen(clientfd, send, strlen(send));
    snprintf(send, sizeof send, "%s", user_agent_hdr);
    Rio_writen(clientfd, send, strlen(send));
    snprintf(send, sizeof send, "Connection: close\r\n");
    Rio_writen(clientfd, send, strlen(send));
    snprintf(send, sizeof send, "Proxy-Connection: close\r\n\r\n");
    Rio_writen(clientfd, send, strlen(send));
  }
  return clientfd;
}

/* 5. 클라이언트로 응답 전달*/
void send_response_client(int connfd, int clientfd) {
  rio_t rio;
  Rio_readinitb(&rio, clientfd);
  char recv_header[MAXLINE], recv_body[MAXLINE];
  int content_length = -1;

  // 헤더응답
  while (1) {
    ssize_t n = Rio_readlineb(&rio, recv_header, MAXLINE);
    // 읽기 에러 시 탈출
    if (n <= 0) {
      break;
    }
    Rio_writen(connfd, recv_header, n);

    // 헤더 끝나면 탈출
    if (!strcmp(recv_header, "\r\n")) {
      break;
    }

    // Content-Length
    if (!strncasecmp(recv_header, "Content-length", strlen("Content-length"))) {
      sscanf(recv_header, "Content-length: %d\r\n", &content_length);
    }
  }

  // 바디응답
  long remain = content_length;
  while (remain > 0) {
    ssize_t read = (remain < (long)sizeof(recv_body))
                       ? (ssize_t)remain
                       : (ssize_t)sizeof(recv_body);
    ssize_t n = Rio_readnb(&rio, recv_body, read);
    if (n <= 0) {
      break;
    }
    Rio_writen(connfd, recv_body, n);
    remain -= n;
  }
}

/* HTTP 오류 응답 */
void proxy_error(int fd, char *cause, char *errnum, char *shortmsg,
                 char *longmsg) {
  char buf[MAXLINE], body[MAXBUF];

  /* Build the HTTP response body */
  snprintf(body, sizeof body, "<html><title>Proxy Error</title>");
  snprintf(body, sizeof body,

           "%s<body bgcolor="
           "ffffff"
           ">\r\n",
           body);
  snprintf(body, sizeof body, "%s%s: %s\r\n", body, errnum, shortmsg);
  snprintf(body, sizeof body, "%s<p>%s: %s\r\n", body, longmsg, cause);
  snprintf(body, sizeof body, "%s<hr><em>The Proxy Web server</em>\r\n", body);

  /* Print the HTTP response */
  snprintf(buf, sizeof buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
  Rio_writen(fd, buf, strlen(buf));
  snprintf(buf, sizeof buf, "Content-type: text/html\r\n");
  Rio_writen(fd, buf, strlen(buf));
  snprintf(buf, sizeof buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
  Rio_writen(fd, buf, strlen(buf));
  Rio_writen(fd, body, strlen(body));
}
