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
#include <sys/syscall.h>
#include "ns3types.h"
#include "ns3sockutils.h"

int get_sockfd() {
    struct sockaddr_in servaddr;

    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1) {
        printf("socket creation failed...\n");
        exit(0);          
    }

    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = inet_addr("127.0.0.1");
    servaddr.sin_port = htons(8080);

    if (connect(sockfd, (struct sockaddr*)&servaddr, sizeof(servaddr)) != 0) {
        printf("connection with the server failed...\n");
        exit(0);
    }
    return sockfd;
}

int ns3_socket(int domain, int type, int protocol, char *name) {
    int ns3_sockfd = get_sockfd();

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
    cocosim_send (ns3_sockfd, buffer, buffer_size, 0);

    struct ns3_generic_res res;
    cocosim_recv (ns3_sockfd, &res, sizeof(struct ns3_generic_res), 0);
    
    close(ns3_sockfd);
    return res.ret;
}

int ns3_bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen) {
    int ns3_sockfd = get_sockfd();

    struct ns3_bind_req req;
    req.call = BIND_REQUEST;
    req.sockfd = sockfd;
    req.addrlen = addrlen;

    int buffer_size = sizeof(struct ns3_bind_req) + addrlen;
    unsigned char *buffer = (unsigned char *) malloc(buffer_size);
    memcpy(buffer, &req, sizeof(struct ns3_bind_req));
    memcpy(buffer + sizeof(struct ns3_bind_req), addr, addrlen);
    cocosim_send(ns3_sockfd, buffer, buffer_size, 0);

    struct ns3_generic_res res;
    cocosim_recv(ns3_sockfd, &res, sizeof(struct ns3_generic_res), 0);
    
    close(ns3_sockfd);
    return res.ret; 
}

ssize_t ns3_recvfrom(int sockfd, void *buf, size_t len, int flags,
                 struct sockaddr *src_addr, socklen_t *addrlen) {
    int ns3_sockfd = get_sockfd();

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
    cocosim_send(ns3_sockfd, buffer, buffer_size, 0);
    
    struct ns3_recvfrom_res res;
    unsigned char *buffer_recv = (unsigned char *) malloc(10000);
    cocosim_recv(ns3_sockfd, buffer, 10000, 0);
    memcpy(&res, buffer, sizeof(struct ns3_recvfrom_res));
    *addrlen = res.addrlen;
    memcpy(src_addr, buffer + sizeof(struct ns3_recvfrom_res), res.addrlen);
    memcpy(buf, buffer + sizeof(struct ns3_recvfrom_res) + res.addrlen, res.ret);
    
    close(ns3_sockfd);
    return res.ret; 
}

ssize_t ns3_recvmsg(int sockfd, struct msghdr *msg, int flags) {
    return ns3_recvfrom(sockfd, msg->msg_iov[0].iov_base, msg->msg_iov[0].iov_len,
                flags, (struct sockaddr *)msg->msg_name, &msg->msg_namelen);
}

ssize_t ns3_sendto(int sockfd, const void *buf, size_t len, int flags,
               const struct sockaddr *dest_addr, socklen_t addrlen) {
    
    int ns3_sockfd = get_sockfd();

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
    cocosim_send(ns3_sockfd, buffer, buffer_size, 0);

    struct ns3_generic_res res;
    cocosim_recv(ns3_sockfd, &res, sizeof(struct ns3_generic_res), 0);
    
    close(ns3_sockfd);
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
    int ns3_sockfd = get_sockfd();

    struct ns3_close_req req;
    req.call = CLOSE_REQUEST;
    req.fd = fd;
    cocosim_send(ns3_sockfd, &req, sizeof(struct ns3_close_req), 0);

    struct ns3_generic_res res;
    cocosim_recv(ns3_sockfd, &res, sizeof(struct ns3_generic_res), 0);  

    close(ns3_sockfd);
    return res.ret; 
}

int ns3_getsockname(int sockfd, struct sockaddr *addr, socklen_t *addrlen) {
    int ns3_sockfd = get_sockfd();

    struct ns3_getsockname_req req;
    req.call = GETSOCKNAME_REQUEST;
    req.sockfd = sockfd;
    req.addrlen = *addrlen;
    cocosim_send (ns3_sockfd, &req, sizeof (struct ns3_getsockname_req), 0);

    struct ns3_getsockname_res res;
    unsigned char *buffer = (unsigned char *) malloc (1000);
    cocosim_recv(ns3_sockfd, buffer, 1000, 0);
    memcpy (&res, buffer, sizeof (struct ns3_getsockname_res));
    int max_len = req.addrlen >= res.addrlen ? req.addrlen : res.addrlen;
    memcpy (addr, buffer + sizeof (struct ns3_getsockname_res), max_len);
    *addrlen = res.addrlen; 

    close(ns3_sockfd);
    return res.ret;
}

// For now assumes writefds = exceptfds = NULL, and timeout = NULL (infinity)
// Assume infinity timeout for now
int ns3_select(int nfds, fd_set *readfds, fd_set *writefds, 
                fd_set *exceptfds, struct timeval *timeout) {
    int ns3_sockfd = get_sockfd();

    // Exit early in cases it is not implemented yet

    if (writefds != NULL || exceptfds != NULL) {
        printf("ns3_select is not implemented with writefds != NULL or exceptfds != NULL\n");
        return -1;
    }
    if (readfds == NULL) {
        printf("ns3_select is not implemented as replacement for sleep\n");
        return -1;
    }

    // Divide fds from ns-3 from others fds

    int cant_fds_pipe = 0; // fds con número <100, que no vienen de ns-3
    int cant_fds_sock = 0; // fds con número >100 vienen de ns-3
    int32_t fds_pipe[100];
    int32_t fds_sock[nfds];

    for (int i = 0; i <= 100; i++) {
        if (FD_ISSET(i, readfds)) {
            fds_pipe[cant_fds_pipe] = i;
            cant_fds_pipe++;
        }
    }
    for (int i = 101; i < nfds; i++) {
        if (FD_ISSET(i, readfds)) {
            fds_sock[cant_fds_sock] = i;
            cant_fds_sock++;
        }
    }

    // We add all fds_pipe (if any) to a new fd_set

    fd_set fdset;
    int maxfd = 0;
    FD_ZERO(&fdset);
    for (int i = 0; i < cant_fds_pipe; i++) {
        FD_SET(fds_pipe[i], &fdset);
        maxfd = fds_pipe[i];
    }

    // If there are fds from ns-3 we send msg to ns-3 service

    if (cant_fds_sock > 0) {
        struct ns3_select_req req;
        req.call = SELECT_REQUEST;
        req.readfds_size = cant_fds_sock;

        int buffer_ns3_send_size = sizeof(int32_t) * cant_fds_sock + sizeof(struct ns3_select_req);
        unsigned char buffer_ns3_send[buffer_ns3_send_size];
        memcpy(buffer_ns3_send, &req, sizeof(struct ns3_select_req));
        memcpy(buffer_ns3_send + sizeof(struct ns3_select_req), fds_sock, sizeof(int32_t) * cant_fds_sock);
        cocosim_send(ns3_sockfd, buffer_ns3_send, buffer_ns3_send_size, 0);

        FD_SET(ns3_sockfd, &fdset);
        maxfd = maxfd > ns3_sockfd ? maxfd : ns3_sockfd;
    }

    // We make a select to the SO and check 

    select(maxfd + 1, &fdset, NULL, NULL, NULL);
    FD_ZERO(readfds); // Lo redefinimos para poner los valores de vuelta
    int ret = 0;

    // Handle whatever comes from ns-3
    if (FD_ISSET(ns3_sockfd, &fdset)) {
        struct ns3_select_req res;
        unsigned char buffer_ns3_recv[1000];
        cocosim_recv(ns3_sockfd, buffer_ns3_recv, 1000, 0);
        memcpy(&res, buffer_ns3_recv, sizeof (struct ns3_select_req));
        memcpy(fds_sock, buffer_ns3_recv + sizeof (struct ns3_select_req), sizeof(int32_t) * res.readfds_size);

        for (int i = 0; i < res.readfds_size; i++) {
            FD_SET(fds_sock[i], readfds);
        }
        ret += res.readfds_size;
    } else {
        //TODO: Handle this case
    }

    // Add pipes to return
    for (int i = 0; i < cant_fds_pipe; i++) {
        if (FD_ISSET(fds_pipe[i], &fdset)) {
            FD_SET(fds_pipe[i], readfds);
        }
    }
    
    close(ns3_sockfd);
    return ret;
}

#endif
