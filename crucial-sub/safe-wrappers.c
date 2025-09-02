#include "safe-wrappers.h"
#include "csapp.h"

/*
 * [오류 로그]
 * 지정된 위치(where)와 현재 errno를 기반으로 표준 에러를 출력
 */
void log_perror(const char *where) {
  int e = errno; // snapshot
  fprintf(stderr, "[ERR] %s: (%d) %s\n", where, e, strerror(e));
  errno = e; // 상위 로직이 errno를 참고한다면 복원
}

/*
 * [소켓] accept()의 안전한 래퍼 함수
 * EINTR (시그널에 의한 중단)이나 ECONNABORTED (클라이언트 연결 중단) 같은
 * 일시적이거나 흔한 오류 발생 시, 연결을 재시도
 * 그 외의 오류 발생 시 로그를 남기고 -1을 반환
 */
int accept_sw(int s, struct sockaddr *addr, socklen_t *addrlen) {
  int connfd;
  while (1) {
    connfd = accept(s, addr, addrlen);
    if (connfd >= 0) {
      return connfd; // 성공 시 연결 식별자 반환
    }

    // accept 실패 시 오류 원인 확인
    if (errno == EINTR) { // 시그널에 의해 방해받음 -> 재시도
      continue;
    }
    if (errno == ECONNABORTED) { // 클라이언트가 연결 수립 중 포기 -> 재시도
      continue;
    }
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      // non-blocking이라 아직 연결 없음: 상위 레벨로 넘김
      return -1;
    }

    // 이 호출 안에서는 추가 복구 시도 없이 상위에 위임할 에러
    // (서버 전체 종료 의미 아님; 상위에서 backoff/계속 루프/종료 중 결정)
    log_perror("accept_sw");
    return -1;
  }
}

/*
 * [소켓] open_clientfd()의 안전한 래퍼 함수
 * 지정된 hostname과 port로 서버에 연결을 시도
 */
int open_clientfd_sw(const char *hostname, const char *port) {
  int clientfd = open_clientfd((char *)hostname, (char *)port);
  if (clientfd < 0) {
    log_perror("open_clientfd_sw");
  }
  return clientfd;
}

/*
 * [소켓] open_listenfd()의 안전한 래퍼 함수
 * 지정된 포트로 리스닝 소켓을 생성하고 준비
 */
int open_listenfd_sw(const char *port) {
  int listenfd = open_listenfd((char *)port);
  if (listenfd < 0) {
    log_perror("open_listenfd_sw");
  }
  return listenfd;
}

/*
 * [소켓] close()의 안전한 래퍼 함수
 */
int close_sw(int fd) {
  if (close(fd) == 0) {
    return 0;
  }
  if (errno == EINTR) {
    return 0; // 재시도 X: 성공 취급
  }
  log_perror("close_sw"); // 그 외 에러만 로그
  return -1;
}

/*
 * [RIO] rio_readlineb()의 안전한 래퍼 함수
 * 한 줄을 읽어들임 (최대 maxlen)
 * 실패 시 로그를 남기고 -1을, EOF에서는 0을 반환
 */
ssize_t rio_readlineb_sw(rio_t *rp, void *usrbuf, size_t maxlen) {
  ssize_t rc;
  if ((rc = rio_readlineb(rp, usrbuf, maxlen)) < 0) {
    log_perror("rio_readlineb_sw");
  }
  return rc; // 성공 시 읽은 바이트 수, EOF 시 0, 에러 시 -1
}

/*
 * [RIO] rio_readnb()의 안전한 래퍼 함수
 * rp에서 최대 n바이트를 읽어 usrbuf에 저장
 * 실패 시 로그를 남기고 -1을, EOF에서는 실제 읽은 바이트 수를 반환
 */
ssize_t rio_readnb_sw(rio_t *rp, void *usrbuf, size_t n) {
  ssize_t rc;
  if ((rc = rio_readnb(rp, usrbuf, n)) < 0) {
    log_perror("rio_readnb_sw");
  }
  return rc; // 성공 시 읽은 바이트 수 (n), EOF 시 0 <= rc < n, 에러 시 -1
}

/*
 * [RIO] rio_writen()의 안전한 래퍼 함수
 * fd로 usrbuf에서 정확히 n바이트 전송 시도
 * 실패 시(short count 포함) 로그를 남기고 -1을 반환
 */
ssize_t rio_writen_sw(int fd, const void *usrbuf, size_t n) {
  ssize_t bytes_written = rio_writen(fd, (void *)usrbuf, n);
  if (bytes_written < 0) {
    // errno == EPIPE일 수 있음: 연결만 정리하고 요청 종료
    log_perror("rio_writen_sw");
  }
  return bytes_written; // 성공: n, 실패: -1
}

/*
 * [메모리] malloc()의 안전한 래퍼 함수
 * 메모리 할당 실패 시 로그를 남기고 NULL을 반환
 */
void *malloc_sw(size_t size) {
  void *p;
  if ((p = malloc(size)) == NULL) {
    log_perror("malloc_sw");
  }
  return p;
}

/*
 * [쓰레드] pthread_create의 안전한 래퍼 함수
 * 쓰레드 생성 실패 시 로그를 남기고 -1 반환
 */
int pthread_create_sw(pthread_t *tidp, pthread_attr_t *attrp,
                      void *(*routine)(void *), void *argp) {
  int rc = pthread_create(tidp, attrp, routine, argp);
  if (rc != 0) {
    errno = rc;
    log_perror("pthread_create");
    return -1;
  }
  return 0;
}

/*
 * [쓰레드] pthread_detach 안전한 래퍼 함수
 * 쓰레드 분리 실패 시 로그를 남기고 -1 반환
 */
int pthread_detach_sw(pthread_t tid) {
  int rc = pthread_detach(tid);
  if (rc != 0) {
    errno = rc;
    log_perror("Pthread_detach");
    return -1;
  }
  return 0;
}