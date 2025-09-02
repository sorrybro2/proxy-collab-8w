#pragma once
#include "csapp.h"
#include <pthread.h>
#include <sys/socket.h>

void log_perror(const char *where);

int accept_sw(int s, struct sockaddr *addr, socklen_t *addrlen);
int open_clientfd_sw(const char *hostname, const char *port);
int open_listenfd_sw(const char *port);
int close_sw(int fd);

ssize_t rio_writen_sw(int fd, const void *usrbuf, size_t n);
ssize_t rio_readlineb_sw(rio_t *rp, void *usrbuf, size_t maxlen);
ssize_t rio_readnb_sw(rio_t *rp, void *usrbuf, size_t n);