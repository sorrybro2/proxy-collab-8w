/* $begin tinymain */
/*
 * tiny.c - A simple, iterative HTTP/1.0 Web server that uses the
 *     GET method to serve static and dynamic content.
 *
 * Updated 11/2019 droh
 *   - Fixed sprintf() aliasing issue in serve_static(), and clienterror().
 * 
 * 빌드 -> 서버실행 -> 테스트
 * gcc -Wall -O2 cgi-bin/adder.c -o cgi-bin/adder && chmod 755 cgi-bin/adder
 * gcc -O2 -Wall -Wextra tiny.c csapp.c -o tiny -lpthread
 * ./tiny 8000 & sleep 0.5 && curl -v http://127.0.0.1:8000/home.html && curl -v 'http://127.0.0.1:8000/cgi-bin/adder?x=13&y=29'
 *
 */
#include "csapp.h"

void doit(int fd);
void read_requesthdrs(rio_t *rp);
int parse_uri(char *uri, char *filename, char *cgiargs);
void serve_static(int fd, char *filename, int filesize);
void get_filetype(char *filename, char *filetype);
void serve_dynamic(int fd, char *filename, char *cgiargs);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg,
                 char *longmsg);

int main(int argc, char **argv) {
  int listenfd, connfd;
  char hostname[MAXLINE], port[MAXLINE];
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;

  /* Check command line args */
  if (argc != 2) {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }

  // 듣기 소켓 디스크럽트 오픈 후
  listenfd = Open_listenfd(argv[1]);
  // 전형적인 무한 서버 루프를 실행
  while (1) {
    clientlen = sizeof(clientaddr);

    // 반복적으로 연결 요청을 접수
    connfd = Accept(listenfd, (SA *)&clientaddr,
                    &clientlen);  // line:netp:tiny:accept
    Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
    printf("Accepted connection from (%s, %s)\n", hostname, port);
    
    // 트랜잭션을 수행
    doit(connfd);   // line:netp:tiny:doit
    // 자신 쪽의 연결 끝을 닫음
    Close(connfd);  // line:netp:tiny:close
  }
}

void doit(int fd)
{
  int is_static;
  struct stat sbuf;
  char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
  char filename[MAXLINE], cgiargs[MAXLINE];
  rio_t rio;

  /* 라인 읽고 파싱
     “파싱(parsing)”은 형식이 있는 문자열을 규칙에 따라 잘라서(토큰화) 의미 있는 필드로 해석 */

  Rio_readinitb(&rio, fd);
  // 클라이언트가 보낸 첫 줄(요청라인)을 buf로 한 줄 읽고
  Rio_readlineb(&rio, buf, MAXLINE);

  // 디버깅용으로 요청라인을 찍음 
  printf("Request headers : \n");
  printf("%s", buf);

  // 공백 기준으로 세 토큰을 뽑아 메서드, URI, HTTP 버전을 각각 넣음!
  sscanf(buf, "%s %s %s", method, uri, version);
  /*
    예 : "GET /cgi-bin/addr?x=1&y=2 HTTP/1.1"
    -> method = "GET", 
       URI = "/cgi-bin/addr?x=1&y=2", 
       version = "HTTP/1.1"
  */ 

  // 만약에 method가 "GET"이 아니면
  if (strcasecmp(method, "GET")){

    // 501 : 서버가 요청 기능 구현 안함!
    clienterror(fd, method, "501", "Not implemented",
              "Tiny does not implement this method : 서버가 요청 처리하는 기능을 구현을 안했다~");
      return;
  }
  // 위에 조건문을 통해 GET 메소드(요청)만 받음 method == GET
  read_requesthdrs(&rio);

  // 정적 컨텐츠인가 동적 컨텐츠인가
  is_static = parse_uri(uri, filename, cgiargs);

  // 404 : 이 파일이 디스크 상에 있지 않으면
  if (stat(filename, &sbuf) < 0){
    clienterror(fd, filename, "404", "Not found",
              "Tiny couldn't find this file : 파일이 없음!");
      return;
  }

  if (is_static) { // 정적 컨텐츠면
    /*
      !(S_ISREG(sbuf.st_mode) = 정규 파일이 아니거나 
      !(S_IRUSR & sbuf.st_mode) = 소유자 읽기 권한이 없으면
      403 : 권한 미달로 정적 컨텐츠로 읽을 수 없다!
    */
    if (!(S_ISREG(sbuf.st_mode)) || !(S_IRUSR & sbuf.st_mode)){
      clienterror(fd, filename, "403", "Forbidden",
                "Tiny couldn't run the file\n : 정규 파일이 아니거나 소유자 읽기 권한이 없기 때문에 정적 컨텐츠로 읽을 수 없다.");
      return;
    }
    // 에러 색출 이후 serve_static을 실행
    serve_static(fd, filename, sbuf.st_size);
  }
  else { // 동적 컨텐츠면
    /*
    위와 동문
    403 : 권한 미달로 동적 컨텐츠로 읽을 수 없다!
    */ 
    if (!(S_ISREG(sbuf.st_mode)) || !(S_IXUSR & sbuf.st_mode)){
      clienterror(fd, filename, "403", "Forbidden",
                "Tiny couldn't run the CGI Program : 권한 때문에 동적 컨텐츠로 읽을 수 없음");
      return;
    }
    // 에러 색출 이후 serve_dynamic을 실행
    serve_dynamic(fd, filename, cgiargs);
  }
}


// 에러 메세지 남기는 함수
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg)
{
  char buf[MAXLINE], body[MAXBUF];   

  /*
    HTTP 응답 body - 정적 컨텐츠 오류 메세지 출력 
    (빈 줄 \r\n 한 번) ← 헤더와 바디를 구분하는 매우 중요한 경계
    sprintf는 일반적인 출력이 아님!
    -> “문자열을 메모리에 써 넣는 함수”

    아래의 결과값
      HTTP/1.0 404 Not found\r\n
      Content-type: text/html\r\n
      Content-length: 153\r\n
      \r\n
      <html><title>Tiny Error</title><body bgcolor="ffffff">\r\n
      404: Not found\r\n
      <p>Tiny couldn’t find this file: /home.html\r\n
      <hr><em>The Tiny Web server</em>\r\n

  */
  sprintf(body, "<html><title>Tiny Error</title>");
  sprintf(body + strlen(body), "<body bgcolor=\"ffffff\">\r\n");
  sprintf(body + strlen(body), "%s: %s\r\n", errnum, shortmsg);
  sprintf(body + strlen(body), "<p>%s: %s\r\n", longmsg, cause);
  sprintf(body + strlen(body), "<hr><em>The Tiny Web server</em>\r\n");

  sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-type: text/html\r\n");
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
  Rio_writen(fd, buf, strlen(buf));
  Rio_writen(fd, body, strlen(body));
}

// 요청 헤더만 읽음
void read_requesthdrs(rio_t *rp){
  char buf[MAXLINE];

  //소켓에서 헤드 줄을 한줄씩 읽어 buf에 담음
  Rio_readlineb(rp, buf, MAXLINE);
  
  // "\r\n" <- 빈줄을 만날 때까지 계속 읽
  while(strcmp(buf, "\r\n")) {
    Rio_readlineb(rp, buf, MAXLINE);
    printf("%s", buf);
  }
  // 서버 로그에만 찍음 -> 요청 헤더를 읽고 무시
  // 의미적으로만 무시하는거일 뿐, 바이트는 끝까지 읽어서 버퍼를 비워둠(그래야 이후 단계가 헤더의 다음 위치가 될 수 있음!)
  return;
}

// 정적 컨텐츠 uri와 동적 컨텐츠 uri 구분 짓는 함수
int parse_uri(char *uri, char *filename, char *cgiargs)
{
  char *ptr;

  //cgi-bin이 포함되면 동적 없으면 정적
  if(!strstr(uri, "cgi-bin")) { // 정적 컨텐츠

    // 정적이면 cgi인자가 필요없음 -> 지움
    strcpy(cgiargs, ""); 

    // uri 경로 이름 출력 -> 맨 앞에 .만 붙이는거임
    strcpy(filename, "."); 
    strcat(filename, uri);

    // uri 가 '/'로 끝나면
    if (uri[strlen(uri)-1] == '/')
      // ./home.html
      strcat(filename, "home.html");

    return 1;
  }else{ // 동적 컨텐츠

    /*
      ?부터 uri만 따로 ptr로 끊음
      
      예시 : 
      if (ptr)이 참
      /search?q=chatgpt&lang=ko
      if (ptr)이 거짓
      /search
    */
    ptr = strchr(uri, '?');
    if (ptr){ // ?부터 존재하면
      strcpy(cgiargs, ptr+1); // cgiargs = q=chatgpt&lang=ko
      *ptr = '\0'; // uri = "/search"로 여기서 문자열을 종료!(뒤쪽은 메모리에 남아있지만 무시)
    }else{ // 안하면
      strcpy(cgiargs, ""); // cgiargs 지움 -> 없는데 왜 넣음
    }

    // cgiargs를 때버린 uri를 filename과 붙임!
    strcpy(filename, ".");
    strcat(filename, uri);
    return 0;
  }
}


// 정적 파일 서버에서 가공하여 클라이언트에 응답으로 보냄
void serve_static(int fd, char *filename, int filesize)
{
  int srcfd;
  char *srcp, filetype[MAXLINE], buf[MAXBUF];

  /* 응답 헤더를 클라이언트로 보냄 */
  get_filetype(filename, filetype);

  int n = 0;
  n += snprintf(buf + n, sizeof(buf) - n, "HTTP/1.0 200 OK\r\n");
  n += snprintf(buf + n, sizeof(buf) - n, "Server: Tiny Web Server\r\n");
  n += snprintf(buf + n, sizeof(buf) - n, "Connection: close\r\n");
  n += snprintf(buf + n, sizeof(buf) - n, "Content-length: %d\r\n", filesize);
  n += snprintf(buf + n, sizeof(buf) - n, "Content-type: %s\r\n\r\n", filetype);

  Rio_writen(fd, buf, n);
  printf("Response headers:\n%s", buf);

  /*
    응답 바디를 클라이언트로 보냄
    예시 :
     - filename = "./hello.txt"
     - 파일 내용(6바이트) : HELLO\n
     - fd = 클라이언트와 연결된 소켓
  */
  srcfd = Open(filename, O_RDONLY, 0); // filename을 읽기 전용으로 연다 -> 파일 디스크립터 srcfd 획득 (예시 : srcfd = 3)

  /*
    파일 내용을 프로세스 가상 메모리에 매핑
    srcp는 파일 첫 바이트를 가리키는 포인터
    PROT_READ(읽기 전용), MAP_PRIVATE(공유 아님 수정은 복사본? 여기에 선언 안함?)
  */
  srcp = Mmap(0, filesize, PROT_READ, MAP_PRIVATE, srcfd, 0); // Mmap -> srcp = 0x7f..(예시 주소)

  Close(srcfd);// 매핑만 유지되면 fd는 닫아도 됨 

  /*
    클라이언트 소켓(fd)로 srcp에서 filesize 바이트를 보냄 -> 파일 내용이 네트워크로 감
    소켓으로 6바이트 전송 : HELLO\n
  */
  Rio_writen(fd, srcp, filesize);
  
  Munmap(srcp, filesize); // 매핑 해제(정리)
}

//  파일 확장자에 따라 타입 선정
void get_filetype(char *filename, char *filetype)
{
  if (strstr(filename, ".html"))
    strcpy(filetype, "text/html");
  else if (strstr(filename, ".gif"))
    strcpy(filetype, "image/gif");
  else if (strstr(filename, ".png"))
    strcpy(filetype, "image/png");
  else if (strstr(filename, ".jpg"))
    strcpy(filetype, "image/jpeg");
  else if (strstr(filename, ".mpg") || strstr(filename, ".mpeg"))
     strcpy(filetype, "video/mpeg");
  else if (strstr(filename, ".mp4"))
    strcpy(filetype, "video/mp4");
  else
    strcpy(filetype, "text/plain");
}

// 동적 파일 실행 결과를 클라이언트에 전달
void serve_dynamic(int fd, char *filename, char *cgiargs)
{
  char buf [MAXLINE], *emptylist[] = { NULL };

  /* 서버가 상태줄과 일부 헤더를 먼저 전송 (CGI는 나머지 헤더/바디를 stdout으로 출력) */
  sprintf(buf, "HTTP/1.0 200 OK\r\n");
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Server: Tiny Web Server\r\n");
  Rio_writen(fd, buf, strlen(buf));

  //Fork로 자식 프로세스 생성
    if (Fork() == 0) {
      setenv("QUERY_STRING", cgiargs, 1); // CGI가 읽을 QUERY_STRING 환경변수 설정
      Dup2(fd, STDOUT_FILENO); // 자식의 표준 출력을 클라이언트 디스크럽트로 재지정
      Execve(filename, emptylist, environ); // cgi가 printf로 쓰는 모든 출력이 그대로 클라이언트로 감
    }
  wait(NULL); // 부모는 자식이 끝날때까지 기다림
}