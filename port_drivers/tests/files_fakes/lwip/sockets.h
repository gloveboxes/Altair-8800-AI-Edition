#pragma once
#include <sys/socket.h>
int test_socket(int domain, int type, int protocol);
int test_connect(int socket, const struct sockaddr *address, socklen_t length);
int test_setsockopt(int socket, int level, int option, const void *value, socklen_t length);
ssize_t test_send(int socket, const void *bytes, size_t length, int flags);
ssize_t test_recv(int socket, void *bytes, size_t length, int flags);
int test_close(int socket);
#define socket test_socket
#define connect test_connect
#define setsockopt test_setsockopt
#define send test_send
#define recv test_recv
#define lwip_close test_close
