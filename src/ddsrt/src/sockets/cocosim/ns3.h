#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <inttypes.h>

// Tipos de paquete
#define SOCKET_REQUEST      1
#define SOCKET_RESPONSE     2
#define BIND_REQUEST        3
#define BIND_RESPONSE       4
#define RECVFROM_REQUEST    5
#define RECVFROM_RESPONSE   6
#define SENDTO_REQUEST      7
#define SENDTO_RESPONSE     8
#define CLOSE_REQUEST       9
#define CLOSE_RESPONSE      10

// Mensajes
struct ns3_socket_req {
  unsigned char call;
  int32_t node;
  int32_t domain;
  int32_t type;
  int32_t protocol;
};

struct ns3_bind_req {
  unsigned char call;
  int32_t sockfd;
  int32_t addrlen;
  //buffer con addr
};

struct ns3_recvfrom_req {
  unsigned char call;
  int32_t sockfd;
  int32_t len;
  int32_t flags;
  int32_t addrlen;
  //buffer con src_addr
};

struct ns3_sendto_req {
  unsigned char call;
  int32_t sockfd;
  int32_t len;
  int32_t flags;
  int32_t addrlen;
  //buffer con dest_addr
  //buffer con buf
};

struct ns3_close_req {
  unsigned char call;
  int32_t fd;
};

struct ns3_generic_res {
  unsigned char call;
  int32_t ret;
};

struct ns3_recvfrom_res {
  unsigned char call;
  int32_t ret;
  int32_t addrlen;
  //buffer con src_addr
  //buffer con buf
};

// Client side implementation of UDP client-server model
#define MAXLINE  1024 
#define PORT     12346 
#define NODE     1
  
int ns3_sockfd;

void debug_print_buffer(char *name, uint8_t *buffer, int len);

void ns3_start_service(void);

int ns3_socket(int domain, int type, int protocol);

int ns3_bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen);

ssize_t ns3_recvfrom(int sockfd, void *buf, size_t len, int flags,
                 struct sockaddr *src_addr, socklen_t *addrlen);

ssize_t ns3_sendto(int sockfd, const void *buf, size_t len, int flags,
               const struct sockaddr *dest_addr, socklen_t addrlen);

int ns3_close(int fd);
