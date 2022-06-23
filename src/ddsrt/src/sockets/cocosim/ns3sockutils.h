#ifndef COCOSIM_SOCK_UTILS_H
#define COCOSIM_SOCK_UTILS_H

#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>

void debug_print_buffer(char *name, uint8_t *buffer, int len) {
  printf("%s(%d)=", name, len);
  for (int i = 0; i < len; i++) {
    if (i > 0) printf(":");
    printf("%02X", buffer[i]);
  }
  printf("\n");
}

ssize_t cocosim_send(int sockfd, const void *buf, size_t len, int flags) {
  uint32_t msg_len = htonl(len);
  char *data_len = (char*)&msg_len;
  int err = send(sockfd, data_len, 4, 0);

  if (err < 0) return err;
  return send(sockfd, buf, len, flags);
}

ssize_t cocosim_recv(int sockfd, void *buf, size_t len, int flags) {
  uint32_t msg_len;
  char *data_len = (char*)&msg_len;
  int err = recv(sockfd, data_len, 4, 0);
  if (err < 1) return err;
  msg_len = ntohl(msg_len);

  if (msg_len > len) {
    printf("cocosim_recv: msg bigger than expected. Aborting\n");
    return -1;  
  }

  return recv(sockfd, buf, msg_len, flags);
}

#endif
