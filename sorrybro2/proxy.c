#include <stdio.h> // 표준 입출력 함수들 (printf, fprintf 등) 
#include "csapp.h" // CS:APP 교재의 wrapper 함수들 (Open_listenfd, Accept, Rio 등)

/* 캐시 최대 크기와 객체 최대 크기 정의 (문제 3에서 사용함) */ 
#define MAX_CACHE_SIZE 1049000 // 캐시 최대 크기 정의 
#define MAX_OBJECT_SIZE 102400 // 캐시할 객체 최대 크기 정의

/* You won't lose style points for including this long line in your code */
// 과제에서 제공된 고정 User-Agent 값
static const char *user_agent_hdr = // User-Agent 헤더 문자열을 상수로 정의
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n"; // 과제에서 제공된 고정 User-Agent 값

/* 함수 선언 */
void handle_request(int connfd); 
/*
  클라이언트 요청을 처리하는 함수
  connfd: 클라이언트와의 연결 소켓 디스크립터
*/

// 파싱 = 문자열/데이터를 약속된 규칙(문법, 포맷)에 따라 의미있는 조각을 해석 후 조각화하는 과정
// 버퍼 = 데이터를 잠시 담아둘 메모리 공간 (char배열, malloc으로 확보한 메모리 덩어리)
int parse_url(char *url, char *host, char *port, char *path); 
/*
  URL을 파싱하는 함수
  url: 파싱할 절대 URL 문자열 (입력)
  host: 추출된 호스트명 (출력)
  port: 추출된 포트 번호 (출력)
  path: 추출된 경로 (출력)
  -> host, port, path도 버퍼임
*/

void collect_headers(rio_t *rio, char *headers, char *host_header);
/*
  헤더를 수집하는 함수
  rio: 클라이언트 소켓의 Rio 구조체 포인터 (입력)
  headers: 필터링된 헤더를 저장할 버퍼 (출력)
  host_header: Host 헤더만 따로 저장할 버퍼 (출력)
*/

void forward_request(int serverfd, char *method, char *path, char *headers, char *host);
/*
  서버로 요청 전달하는 함수
  serverfd: 원서버와 연결된된 소켓 디스크립터 (입력)
  method: HTTP 메소드 (보통 "GET") (입력)
  path: 요청할 경로 (예: "/index.html") (입력)
  headers: 전달할 추가 헤더들 (입력)
  host: Host 헤더에 사용할 호스트명 (입력)
*/

void forward_response(int serverfd, int cilentfd);
/*
  서버 응답을 클라이언트로 전달하는 함수
  serverfd: 원서버와 연결된 소켓 디스크립터 (입력 - 읽기용)
  clientfd: 클라이언트와의 연결 소켓 디스크립터 (출력 - 쓰기용)
*/

int main(int argc, char **argv) // 메인 함수 (argc = 인자개수, argv = 인자 배열)
{
  // argv[0] = 프로그램 이름 "./proxy", argv[1] = port
  if(argc != 2){ // 인자가 2개가 아니면 (프로그램명 + 포트 번호)

    // 인자를 잘못줬다!(stderr)라고 에러를 출력
    fprintf(stderr, "usage: %s <port>\n", argv[0]); 
    exit(1); // 프로그램 종료
  }
 
  int listenfd = Open_listenfd(argv[1]);// 지정된 포트에서 듣기 소켓 디스크립터 생성
  int connfd; // 클라이언트 연결용 소켓 디스크립터 선언
  char hostname[MAXLINE], port[MAXLINE]; // 클라이언트 정보 저장용 버퍼
  socklen_t clientlen; // 클라이언트 주소 구조체 크기
  struct sockaddr_storage clientaddr; // 클라이언트 주소 구조체 -> 주소 저장할 공간

  printf("Proxy server is running on port %s", argv[1]); // 프록시 서버 시작 메세지

  // 순차적 처리 (Iterative)
  while(1){ // 무한 루프로 클라이언트 요청 대기기
    clientlen = sizeof(clientaddr); // 클라이언트 주소 구조체 크기 설정
    connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen); // 클라이언트 연결 수락

    Getnameinfo((SA *)&clientaddr, clientlen, // 클라이언트 ip/포트
                hostname, MAXLINE, // 호스트/IP 문자열 버퍼 + 그 크기
                port, MAXLINE, // 포트 문자열 버퍼 + 그 크기
                0); // 플래그 예: NI_NUMERICHOST, NI_NUMERICSERV

    printf("Accepted connection from (%s, %s)\n", hostname, port); // 연결 정보 출력력
    handle_request(connfd); // 요청 처리 함수 호출
    close(connfd); // 클라이언트 연결 종료
  }
  return 0; // 프로그램 정상 종료
}

/*
 * handle_request - 클라이언트 요청을 처리하는 메인 함수
 */
void handle_request(int connfd) { // 클라이언트 소켓을 매개변수로 받음
  char buf[MAXLINE], method[MAXLINE], url[MAXLINE], version[MAXLINE]; // 요청 라인 파싱용 버퍼들
  char host[MAXLINE], port[MAXLINE], path[MAXLINE]; // URL 파싱용 버퍼들
  char headers[MAXLINE], host_header[MAXLINE];       // 헤더 저장용 버퍼들
  rio_t rio; // 요청 읽기용 버퍼 (rio_t 구조체)
  int serverfd; // 원서버와 연결된 소켓 디스크립터

  // 클라이언트로부터 요청 읽기
  Rio_readinitb(&rio, connfd);  // Rio 구조체를 클라이언트 소켓으로 초기화
  if (!Rio_readlineb(&rio, buf, MAXLINE)) {  // 요청라인을 한 줄 읽기 (실패시 0 반환)
    return;                         // 읽기 실패하면 함수 종료
  }

  printf("Request line: %s", buf);    // 받은 요청라인 출력 (디버깅용)

  // 요청 라인 파싱: GET http://host[:port]/path HTTP/1.1
  sscanf(buf, "%s %s %s", method, url, version); // 공백으로 구분하여 3개 필드 파싱
  /*
    method: 메소드 (예: GET, POST, HEAD 등)
    url: 요청 URL (예: http://host[:port]/path)
    version: HTTP 버전 (예: HTTP/1.1)
  */
  
  // method가 GET 메소드만 허용
  if (strcasecmp(method, "GET")) {    // GET이 아니면 (strcasecmp는 대소문자 무시 비교)
    printf("Not implemented: %s method\n", method);  // 에러 메시지 출력
    return;                         // 함수 종료
  }
  
  // url 파싱
  if (parse_url(url, host, port, path) < 0) { // url을 host, port, path로 분리
    printf("Error parsing URL: %s\n", url); // 파싱 실패 시 에러 메세지
    return; // 함수 종료
  }
  
  // 헤더 수집
  collect_headers(&rio, headers, host_header); // 클라이언트 헤더들을 읽어서 필터링

  // 원서버에 연결
  serverfd = Open_clientfd(host, port); // 파싱된 host, port로 서버에 연결
  if (serverfd < 0) {
    printf("Error connecting to server: %s\n", host); // 연결 실패 시 에러 메세지
    return; // 함수 종료
  }
  
  // 요청 전달
  forward_request(serverfd, method, path, headers,   // 서버로 HTTP 요청 전송
                strlen(host_header) > 0 ? host_header : host);  // Host 헤더 처리

  // 응답 전달
  forward_response(serverfd, connfd); // 서버 응답을 클라이언트로 중계

  Close(serverfd);                    // 서버 연결 종료
}

// url: 요청 URL (예: http://host[:port]/path)
parse_url(char *url, char *host, char *port, char *path) {
char *ptr;

  // url이 http://로 시작하지 않으면 에러 반환
  if (strncasecmp(url, "http://", 7) != 0){
    return -1;
  }

  // http:// 이후의 문자열 위치
  ptr = url + 7;

  /* path 따로 분리 */
  char *slash_pos = strchr(ptr, '/'); // 첫 번째 '/' 위치 찾기
  if (slash_pos == NULL){ // '/'가 없으면
     // path가 없는 경우 "/"로 설정
    strcpy(path, '/'); // 기본 path를 "/"로 설정
    strcpy(host, ptr); // 나머지 전체를 host로 복사
  }else{ // /가 있으면
    strcpy(path, slash_pos); // / 이후 값을 path로 복사
    *slash_pos = '\0'; // '/' 위치에 널 문자 삽입
    strcpy(host, ptr); // host:port 부분을 ptr에에 복사
    *slash_pos = '/'; // 다시 '/'로 복귀
  }

  /* host에서 port 분리 */
  char *colon_pos = strchr(host, ':'); // ':' 위치 찾기기
  if(colon_pos == NULL){ // :가 없으면
    // 포트가 없으면 기본값 80
    strcpy(port, "80");
  }else{ // ':' 가 있으면
    strcpy(port, colon_pos); // port에 ':' 이후 값 복사
    *colon_pos = '\0'; // ':' 위치에 널 문자 삽입
  }

  printf("Parsed URL - Host : %s, Port : %s, Path : %s\n", host, port, path); // 파싱 결과
  return 0; // 성공 반환!
}