#ifndef COCOSIM_TYPES_H
#define COCOSIM_TYPES_H

#include <inttypes.h>

// Tipos para las llamadas

// Tipos de paquete
#define SOCKET_REQUEST        1
#define SOCKET_RESPONSE       2
#define BIND_REQUEST          3
#define BIND_RESPONSE         4
#define RECVFROM_REQUEST      5
#define RECVFROM_RESPONSE     6
#define SENDTO_REQUEST        7
#define SENDTO_RESPONSE       8
#define CLOSE_REQUEST         9
#define CLOSE_RESPONSE        10
#define GETSOCKNAME_REQUEST   11
#define GETSOCKNAME_RESPONSE  12
#define SELECT_REQUEST        13
#define SELECT_RESPONSE       14

// Mensajes
struct ns3_socket_req {
  unsigned char call;
  int32_t domain;
  int32_t type;
  int32_t protocol;
  int32_t name_size;
  // buffer con name
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

struct ns3_getsockname_req {
  unsigned char call;
  int32_t sockfd;
  int32_t addrlen;
};

struct ns3_close_req {
  unsigned char call;
  int32_t fd;
};

struct ns3_select_req {
  unsigned char call;
  int32_t readfds_size;
  //array of readfds (int32_t)
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

struct ns3_getsockname_res {
  unsigned char call;
  int32_t ret;
  int32_t addrlen;
  //buffer con addr
};

#endif
