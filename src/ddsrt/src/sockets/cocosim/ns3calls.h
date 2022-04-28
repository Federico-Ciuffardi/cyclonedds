#ifndef COCOSIM_CALLS_H
#define COCOSIM_CALLS_H

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include "ns3types.h"

int ns3_sockfd;

void debug_print_buffer(char *name, uint8_t *buffer, int len) {
    printf("%s(%d)=", name, len);
    for (int i = 0; i < len; i++) {
        if (i > 0) printf(":");
        printf("%02X", buffer[i]);
    }
    printf("\n");
}

void ns3_start_service() {
    struct sockaddr_in servaddr;
    
    ns3_sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (ns3_sockfd == -1) {
        printf("socket creation failed...\n");
        exit(0);
    }

    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = inet_addr("127.0.0.1");
    servaddr.sin_port = htons(8080);

    if (connect(ns3_sockfd, (struct sockaddr*)&servaddr, sizeof(servaddr)) != 0) {
        printf("connection with the server failed...\n");
        exit(0);
    }
}

int ns3_socket(int domain, int type, int protocol, char *name) {
    struct ns3_socket_req req;
    req.call = SOCKET_REQUEST;
    req.domain = domain;
    req.type = type;
    req.protocol = protocol;
    req.name_size = strlen (name);

    int buffer_size = sizeof (struct ns3_socket_req) + req.name_size;
    unsigned char *buffer = (unsigned char *) malloc (buffer_size);
    memcpy (buffer, &req, sizeof (struct ns3_socket_req));
    memcpy (buffer + sizeof (struct ns3_socket_req), name, req.name_size);
    send (ns3_sockfd, buffer, buffer_size, 0);

    struct ns3_generic_res res;
    recv (ns3_sockfd, &res, sizeof(struct ns3_generic_res), 0);
    
    return res.ret;
}

int ns3_bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen) {
    struct ns3_bind_req req;
    req.call = BIND_REQUEST;
    req.sockfd = sockfd;
    req.addrlen = addrlen;

    int buffer_size = sizeof(struct ns3_bind_req) + addrlen;
    unsigned char *buffer = (unsigned char *) malloc(buffer_size);
    memcpy(buffer, &req, sizeof(struct ns3_bind_req));
    memcpy(buffer + sizeof(struct ns3_bind_req), addr, addrlen);
    send(ns3_sockfd, buffer, buffer_size, 0);

    struct ns3_generic_res res;
    recv(ns3_sockfd, &res, sizeof(struct ns3_generic_res), 0);
    
    return res.ret; 
}

ssize_t ns3_recvfrom(int sockfd, void *buf, size_t len, int flags,
                 struct sockaddr *src_addr, socklen_t *addrlen) {
    struct ns3_recvfrom_req req;
    req.call = RECVFROM_REQUEST;
    req.sockfd = sockfd;
    req.len = len;
    req.flags = flags;
    req.addrlen = *addrlen;

    int buffer_size = sizeof(struct ns3_recvfrom_req) + *addrlen;
    unsigned char *buffer = (unsigned char *) malloc(buffer_size);
    memcpy(buffer, &req, sizeof(struct ns3_recvfrom_req));
    memcpy(buffer + sizeof(struct ns3_recvfrom_req), src_addr, *addrlen);
    send(ns3_sockfd, buffer, buffer_size, 0);
    
    struct ns3_recvfrom_res res;
    unsigned char *buffer_recv = (unsigned char *) malloc(10000);
    recv(ns3_sockfd, buffer, 10000, 0);
    memcpy(&res, buffer, sizeof(struct ns3_recvfrom_res));
    *addrlen = res.addrlen;
    memcpy(src_addr, buffer + sizeof(struct ns3_recvfrom_res), res.addrlen);
    memcpy(buf, buffer + sizeof(struct ns3_recvfrom_res) + res.addrlen, res.ret);
    
    return res.ret; 
}

ssize_t ns3_recvmsg(int sockfd, struct msghdr *msg, int flags) {
    return ns3_recvfrom(sockfd, msg->msg_iov[0].iov_base, msg->msg_iov[0].iov_len,
                flags, (struct sockaddr *)msg->msg_name, &msg->msg_namelen);
}

ssize_t ns3_sendto(int sockfd, const void *buf, size_t len, int flags,
               const struct sockaddr *dest_addr, socklen_t addrlen) {

    struct ns3_sendto_req req;
    req.call = SENDTO_REQUEST;
    req.sockfd = sockfd;
    req.len = len;
    req.flags = flags;
    req.addrlen = addrlen;

    int buffer_size = sizeof(struct ns3_sendto_req) + addrlen + len;
    unsigned char *buffer = (unsigned char *) malloc(buffer_size);
    memcpy(buffer, &req, sizeof(struct ns3_sendto_req));
    memcpy(buffer + sizeof(struct ns3_sendto_req), dest_addr, addrlen);
    memcpy(buffer + sizeof(struct ns3_sendto_req) + addrlen, buf, len);
    send(ns3_sockfd, buffer, buffer_size, 0);

    struct ns3_generic_res res;
    recv(ns3_sockfd, &res, sizeof(struct ns3_generic_res), 0);
    
    return res.ret; 
}

ssize_t ns3_sendmsg(int sockfd, struct msghdr *msg, int flags) {
    ssize_t ret = 0;
    for (int i = 0; i < msg->msg_iovlen; i++) {
        ret += ns3_sendto(sockfd, msg->msg_iov[i].iov_base, msg->msg_iov[i].iov_len,
                flags, (struct sockaddr *)msg->msg_name, msg->msg_namelen);
    }
    return ret;
}

int ns3_close(int fd) {
    struct ns3_close_req req;
    req.call = CLOSE_REQUEST;
    req.fd = fd;
    send(ns3_sockfd, &req, sizeof(struct ns3_close_req), 0);

    struct ns3_generic_res res;
    recv(ns3_sockfd, &res, sizeof(struct ns3_generic_res), 0);  
    return res.ret; 
}

int ns3_getsockname(int sockfd, struct sockaddr *addr, socklen_t *addrlen) {
    struct ns3_getsockname_req req;
    req.call = GETSOCKNAME_REQUEST;
    req.sockfd = sockfd;
    req.addrlen = *addrlen;
    send (ns3_sockfd, &req, sizeof (struct ns3_getsockname_req), 0);

    struct ns3_getsockname_res res;
    unsigned char *buffer = (unsigned char *) malloc (1000);
    recv(ns3_sockfd, buffer, 1000, 0);
    memcpy (&res, buffer, sizeof (struct ns3_getsockname_res));
    int max_len = req.addrlen >= res.addrlen ? req.addrlen : res.addrlen;
    memcpy (addr, buffer + sizeof (struct ns3_getsockname_res), max_len);
    *addrlen = res.addrlen; 

    return res.ret;
}

// For now assumes writefds = exceptfds = NULL, and timeout = NULL (infinity)
// Assume infinity timeout for now
int ns3_select(int nfds, fd_set *readfds, fd_set *writefds, 
                fd_set *exceptfds, struct timeval *timeout) {
    if (writefds != NULL || exceptfds != NULL) {
        printf("ns3_select is not implemented with writefds != NULL or exceptfds != NULL\n");
        return -1;
    }

    if (readfds == NULL) {
        printf("ns3_select is not implemented as replacement for sleep\n");
        return -1;
    }

    struct ns3_select_req req;
    req.call = SELECT_REQUEST;
    req.readfds_size = 0;

    int32_t *readfds_array = (int32_t*) malloc(sizeof(int32_t) * nfds);
    for (int i = 0; i < nfds; i++) {
        if (FD_ISSET(i, readfds)) {
            readfds_array[req.readfds_size] = i;
            req.readfds_size++;
        }
    }

    int buffer_size = sizeof(int32_t) * req.readfds_size + sizeof(struct ns3_select_req);
    unsigned char *buffer = (unsigned char *) malloc(buffer_size);
    memcpy(buffer, &req, sizeof(struct ns3_select_req));
    memcpy(buffer + sizeof(struct ns3_select_req), readfds_array, sizeof(int32_t) * req.readfds_size);
    send(ns3_sockfd, buffer, buffer_size, 0);

    struct ns3_select_req res;
    /* free(buffer); */
    buffer = (unsigned char *) malloc (1000);
    recv(ns3_sockfd, buffer, 1000, 0);
    memcpy(&res, buffer, sizeof (struct ns3_select_req));
    memcpy(readfds_array, buffer + sizeof (struct ns3_select_req), sizeof(int32_t) * res.readfds_size);
    FD_ZERO(readfds);
    for (int i = 0; i < res.readfds_size; i++) {
        FD_SET(readfds_array[i], readfds);
    }
    
    free(readfds_array);
    return res.readfds_size;
}

#endif
