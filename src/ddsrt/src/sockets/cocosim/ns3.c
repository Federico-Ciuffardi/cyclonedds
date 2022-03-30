#include "ns3.h"

// Client side implementation of UDP client-server model
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
    servaddr.sin_port = htons(PORT);

    if (connect(ns3_sockfd, (struct sockaddr*)&servaddr, sizeof(servaddr)) != 0) {
        printf("connection with the server failed...\n");
        exit(0);
    }
}

int ns3_socket(int domain, int type, int protocol) {
    struct ns3_socket_req req;
    req.call = SOCKET_REQUEST;
    req.node = NODE;
    req.domain = domain;
    req.type = type;
    req.protocol = protocol;

    send(ns3_sockfd, &req, sizeof(struct ns3_socket_req), 0);

    struct ns3_generic_res res;
    recv(ns3_sockfd, &res, sizeof(struct ns3_generic_res), 0);
    
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

int ns3_close(int fd) {
    struct ns3_close_req req;
    req.call = CLOSE_REQUEST;
    req.fd = fd;
    send(ns3_sockfd, &req, sizeof(struct ns3_close_req), 0);

    struct ns3_generic_res res;
    recv(ns3_sockfd, &res, sizeof(struct ns3_generic_res), 0);  
    return res.ret; 
}
