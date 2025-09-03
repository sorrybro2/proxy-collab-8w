#include "csapp.h"
#include "safe-wrappers.h"
#include <stdio.h>
#include <strings.h>

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* 필수 헤더 4가지 */
#define HDR_HOST "Host:"
#define HDR_USER_AGENT "User-Agent:"
#define HDR_CONNECTION "Connection:"
#define HDR_PROXY_CONNECTION "Proxy-Connection:"

void doit(int fd);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg,
                 char *longmsg);

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n";
static int parse_uri_proxy(const char *uri, char *host, char *port, char *path);

int main(int argc, char **argv) {
  int listenfd, connfd;
  char hostname[MAXLINE], port[6];
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;
  /* 인자가 2개(프로그램 이름, 포트 번호)인지 확인 */
  if (argc != 2) {
    fprintf(stderr, "사용법: %s <port>\n", argv[0]);
    exit(1);
  }

  Signal(SIGPIPE, SIG_IGN); // 클라이언트가 먼저 끊어도 프로세스가 안 죽게

  listenfd = open_listenfd_sw(argv[1]);
  if (listenfd < 0) {
    exit(1); // 초기화 실패는 종료해도 OK
  }
  for (;;) {
    clientlen = sizeof(clientaddr);
    connfd = accept_sw(listenfd, (SA *)&clientaddr, &clientlen);
    if (connfd < 0) {
      continue; // 일시 오류 등은 다음 루프로
    }
    getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE,
                0);
    printf("Accepted connection from (%s, %s)\n", hostname, port);
    doit(connfd);
    close_sw(connfd);
  }
}

void doit(int fd) {
  char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
  char host[MAXLINE], port[6], path[MAXLINE];
  char request_buf[MAXLINE];
  rio_t rio;

  /* 1. Request Line 파싱 (메서드, URI, HTTP버전) */
  rio_readinitb(&rio, fd);
  if (rio_readlineb_sw(&rio, buf, MAXLINE) <= 0) {
    return;
  }
  printf("Request Line: %s", buf);
  sscanf(buf, "%s %s %s", method, uri, version);

  // 일단 GET만 받음
  if (strcasecmp(method, "GET")) {
    clienterror(fd, method, "501", "NOT implemented",
                "Proxy does not implement this method");
    return;
  }

  /* 2. URI 파싱 후 요청 라인 수정하여 서버에 전달 */
  // 우선 http://host:port/path에서 각각을 뽑아냄
  if (parse_uri_proxy(uri, host, port, path) < 0) {
    return;
  }

  // 요청 라인을 "GET path HTTP/1.0" 형식으로 수정하여 서버에 전달
  snprintf(request_buf, MAXLINE, "GET %s HTTP/1.0\r\n", path);
  // 이제 proxy가 클라이언트 역할로서 서버에 요청을 보냄
  int serverfd = open_clientfd_sw(host, port);
  if (serverfd < 0) {
    clienterror(fd, host, "502", "Bad Gateway", "Cannot connect to origin");
    return;
  }
  rio_writen_sw(serverfd, request_buf, strlen(request_buf));

  /* 3. 기타 헤더 전송 작업 */
  // 우선 필수 헤더 4가지 먼저 입맛대로 바꿔서 전송
  // Host
  if (strcmp(port, "80") == 0) {
    snprintf(request_buf, MAXLINE, "Host: %s\r\n", host);
  } else {
    snprintf(request_buf, MAXLINE, "Host: %s:%s\r\n", host, port);
  }
  rio_writen_sw(serverfd, request_buf, strlen(request_buf));
  // User-Agent
  rio_writen_sw(serverfd, user_agent_hdr, strlen(user_agent_hdr));
  // Connentcion
  static const char CONNECTION_CLOSE[] = "Connection: close\r\n";
  rio_writen_sw(serverfd, CONNECTION_CLOSE, sizeof(CONNECTION_CLOSE) - 1);
  // Proxy-Connection
  static const char PROXY_CONNECTION_CLOSE[] = "Proxy-Connection: close\r\n";
  rio_writen_sw(serverfd, PROXY_CONNECTION_CLOSE,
                sizeof(PROXY_CONNECTION_CLOSE) - 1);

  while (rio_readlineb_sw(&rio, request_buf, MAXLINE) > 0) {
    if (!strcmp(request_buf, "\r\n")) {
      break;
    }
    printf("%s", request_buf);

    if (!strncasecmp(request_buf, HDR_HOST, sizeof(HDR_HOST) - 1) ||
        !strncasecmp(request_buf, HDR_USER_AGENT, sizeof(HDR_USER_AGENT) - 1) ||
        !strncasecmp(request_buf, HDR_CONNECTION, sizeof(HDR_CONNECTION) - 1) ||
        !strncasecmp(request_buf, HDR_PROXY_CONNECTION,
                     sizeof(HDR_PROXY_CONNECTION) - 1)) {
      continue;
    }
    rio_writen_sw(serverfd, request_buf, strlen(request_buf));
  }
  // 마지막 빈 줄 전송
  rio_writen_sw(serverfd, "\r\n", 2);

  /* 서버로부터 응답받으면 일체의 수정 없이 그대로 클라이언트에게 전달 */
  rio_t s_rio;
  rio_readinitb(&s_rio, serverfd);
  ssize_t n;
  // MAXLINE만큼 계속 읽어들여서 전송 반복
  while ((n = rio_readnb_sw(&s_rio, buf, MAXLINE)) > 0) {
    rio_writen_sw(fd, buf, n);
  }
  close_sw(serverfd);
}

void clienterror(int fd, char *cause, char *errnum, char *shortmsg,
                 char *longmsg) {
  char buf[MAXLINE], body[MAXBUF];

  /* 1) HTML 본문 구성 */
  snprintf(body, sizeof(body),
           "<html><title>Tiny Error</title>"
           "<body bgcolor=\"#ffffff\">\r\n"
           "%s: %s\r\n"
           "<p>%s: %s</p>\r\n"
           "<hr><em>The Tiny Web server</em>\r\n"
           "</body></html>\r\n",
           errnum, shortmsg, longmsg, cause);

  /* 2) 헤더 전송 */
  // 응답 라인
  snprintf(buf, sizeof(buf), "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
  rio_writen_sw(fd, buf, strlen(buf));

  // Content-type 헤더
  snprintf(buf, sizeof(buf), "Content-Type: text/html; charset=utf-8\r\n");
  rio_writen_sw(fd, buf, strlen(buf));

  // Content-length 헤더와 빈 줄
  snprintf(buf, sizeof(buf), "Content-length: %zu\r\n\r\n", strlen(body));
  rio_writen_sw(fd, buf, strlen(buf));

  // 응답 본문 전송
  rio_writen_sw(fd, body, strlen(body));
}

int parse_uri_proxy(const char *uri, char *host, char *port, char *path) {
  // 현재 uri는 http://host:port/path 형태
  // parse_uri_proxy를 통해 host, port, path를 모두 분리

  char host_port[MAXLINE];

  /* 1. http:// 걷어내기 */
  if (strncasecmp(uri, "http://", 7) != 0) { // https 지원 x
    return -1;
  }
  uri += 7;

  /* 2. host:port와 path로 분리 ('/'를 기점으로) */
  const char *p;
  if ((p = strchr(uri, '/')) != NULL) {
    // host:port가 비어있는 경우 에러 처리
    if (p == uri) {
      return -1;
    }
    snprintf(host_port, MAXLINE, "%.*s", (int)(p - uri), uri);
    snprintf(path, MAXLINE, "%s", p);
  } else {
    snprintf(host_port, MAXLINE, "%s", uri);
    snprintf(path, MAXLINE, "/");
  }

  /* 3. host:port 분리 */
  if ((p = strchr(host_port, ':')) != NULL) {
    // host나 port가 비어있는 경우 에러 처리
    if (p == host_port || !*(p + 1)) {
      return -1;
    }
    snprintf(host, MAXLINE, "%.*s", (int)(p - host_port), host_port);
    snprintf(port, MAXLINE, "%s", p + 1);
  } else {
    snprintf(host, MAXLINE, "%s", host_port);
    snprintf(port, MAXLINE, "80");
  }

  return 0;
}
