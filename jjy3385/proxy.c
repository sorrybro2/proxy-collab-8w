#include "csapp.h"

#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400
#define DELIM_CHARS "/:"

static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n";

int main(int argc, char **argv) {

  int listenfd, connfd, clientfd;
  char hostname[MAXLINE], port[MAXLINE];
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;

  if (argc != 2) {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }

  listenfd = Open_listenfd(argv[1]);
  while (1) {

    rio_t rio, end_rio;
    char buf[MAXLINE], method[MAXLINE], url[MAXLINE], version[MAXLINE];
    clientlen = sizeof(clientaddr);

    /* 1.리스닝 */
    connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
    Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE,
                0);
    printf("Accepted connection from (%s, %s)\n", hostname, port);

    Rio_readinitb(&rio, connfd);
    Rio_readlineb(&rio, buf, MAXLINE);

    /* 2.파싱 */
    // HTTP 요청
    sscanf(buf, "%s %s %s", method, url, version);
    if (strcasecmp(method, "GET")) {
      proxy_error(connfd, method, "501", "Not Implemented",
                  "Proxy does not implement this method");
    }

    char *next_ptr;
    strtok_r(url, DELIM_CHARS, &next_ptr);
    char *end_host = strtok_r(NULL, DELIM_CHARS, &next_ptr);
    char *end_port = strtok_r(NULL, DELIM_CHARS, &next_ptr);
    char *uri = strtok_r(NULL, DELIM_CHARS, &next_ptr);

    char result_uri[MAXLINE];
    if (uri == NULL) {
      result_uri[0] = '/';
    } else {
      sprintf(result_uri, "/%s", uri);
    }

    /* 3.엔드서버로 HTTP 요청 전달 */
    int clientfd = Open_clientfd(end_host, end_port);
    if (clientfd < 0) {
      proxy_error(connfd, "end_server", "403", "Forbidden",
                  "Proxy couldn't find end server");
    } else {
      char send[MAXLINE];
      sprintf(send, "%s %s %s\r\n", method, result_uri, version);
      Rio_writen(clientfd, send, strlen(send));

      // 헤더
      sprintf(send, "Host: %s:%s\r\n", end_host, end_port);
      Rio_writen(clientfd, send, strlen(send));
      sprintf(send, "%s", user_agent_hdr);
      Rio_writen(clientfd, send, strlen(send));
      sprintf(send, "Connection: close\r\n");
      Rio_writen(clientfd, send, strlen(send));
      sprintf(send, "Proxy-Connection: close\r\n\r\n");
      Rio_writen(clientfd, send, strlen(send));

      /* 4. 응답처리 */
      Rio_readinitb(&end_rio, clientfd);
      char recv_header[MAXLINE], recv_body[MAXLINE];
      int content_length = -1;

      // 헤더응답
      while (1) {
        ssize_t n = Rio_readlineb(&end_rio, recv_header, MAXLINE);
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
        if (!strncasecmp(recv_header, "Content-length",
                         strlen("Content-length"))) {
          sscanf(recv_header, "Content-length: %d\r\n", &content_length);
        }
      }

      // 바디응답
      long remain = content_length;
      while (remain > 0) {
        ssize_t read = (remain < (long)sizeof(recv_body))
                           ? (ssize_t)remain
                           : (ssize_t)sizeof(recv_body);
        ssize_t n = Rio_readnb(&end_rio, recv_body, read);
        if (n <= 0) {
          break;
        }
        Rio_writen(connfd, recv_body, n);
        remain -= n;
      }

      Close(clientfd);
    }
    Close(connfd);
  }

  return 0;
}

void proxy_error(int fd, char *cause, char *errnum, char *shortmsg,
                 char *longmsg) {
  char buf[MAXLINE], body[MAXBUF];

  /* Build the HTTP response body */
  sprintf(body, "<html><title>Proxy Error</title>");
  sprintf(body,
          "%s<body bgcolor="
          "ffffff"
          ">\r\n",
          body);
  sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
  sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
  sprintf(body, "%s<hr><em>The Proxy Web server</em>\r\n", body);

  /* Print the HTTP response */
  sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-type: text/html\r\n");
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
  Rio_writen(fd, buf, strlen(buf));
  Rio_writen(fd, body, strlen(body));
}
