/*
 * Copyright(c) 2006 to 2018 ADLINK Technology Limited and others
 *
 * This program and the accompanying materials are made available under the
 * terms of the Eclipse Public License v. 2.0 which is available at
 * http://www.eclipse.org/legal/epl-2.0, or the Eclipse Distribution License
 * v. 1.0 which is available at
 * http://www.eclipse.org/org/documents/edl-v10.php.
 *
 * SPDX-License-Identifier: EPL-2.0 OR BSD-3-Clause
 */
#include <assert.h>
#include <string.h>
#include <unistd.h>
#include <math.h>

#include "dds/ddsrt/log.h"
#include "dds/ddsrt/misc.h"
#include "dds/ddsrt/sockets.h"
#include "dds/ddsrt/sockets_priv.h"
#include "ns3calls.h"
#include <stdio.h>
#include <stdlib.h>

#if defined(__VXWORKS__)
#include <vxWorks.h>
#include <sockLib.h>
#include <ioLib.h>
#else
#include <sys/fcntl.h>
#endif /* __VXWORKS__ */
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#ifdef __sun
#include <fcntl.h>
#endif

#if defined(__APPLE__) || defined(__FreeBSD__)
#include <sys/sockio.h>
#endif /* __APPLE__ || __FreeBSD__ */

////////////
// Config //
////////////
#define COCOSIM_NS_PID_FILE_PATH "/tmp/cocosim_ns_pid"
bool useNS3 = true;

/////////////////////////
// ddsrt_socket_t list //
/////////////////////////
typedef struct socket_list_node {
  ddsrt_socket_t v;
  struct socket_list_node *next;
} socket_list_node;

typedef struct socket_list_node* socket_list;

inline void insert(socket_list *sl,ddsrt_socket_t v){
  socket_list new = (socket_list) malloc(sizeof(socket_list_node));
  new->next = *sl;
  new->v = v;
  *sl = new;
}

inline bool find(socket_list sl, ddsrt_socket_t v){
  socket_list it;
  for(it = sl; it != NULL && it->v != v; it=it->next);
  return it != NULL;
}

inline void print(socket_list sl){
  socket_list it;
  cocosim_log(LOG_INFO,"Sock list:");
  for(it = sl; it != NULL; it=it->next){
    cocosim_log_printf(LOG_INFO,"=%d", it->v);
  }
  cocosim_log_printf(LOG_INFO,"\n");
}

/////////
// log //
/////////
void vcocosim_log_printf(int level, const char* format, va_list args){
  if (level & DEBUG_LEVEL) {
    vfprintf(DEBUG_STREAM, format, args);
  }
}

void cocosim_log_printf(int level, const char* format, ...){
  if (level & DEBUG_LEVEL) {
    va_list(args);
    va_start(args, format);
    vfprintf(DEBUG_STREAM, format, args); // same as vcocosim_log_printf, avoid overhead
    va_end(args);
  }
}

void vcocosim_log(int level, const char* format, va_list args){
  if (level & DEBUG_LEVEL) {
    char levelstr[6];
    switch (level) {
      case LOG_FATAL : strcpy(levelstr,"FATAL"); break;
      case LOG_ERROR : strcpy(levelstr,"ERROR"); break;
      case LOG_WARN  : strcpy(levelstr,"WARN "); break;
      case LOG_INFO  : strcpy(levelstr,"INFO "); break;
      case LOG_DEBUG : strcpy(levelstr,"DEBUG"); break;
      default        : strcpy(levelstr,"?????"); break;
    }
    fprintf(DEBUG_STREAM,"CCS_%s: ",levelstr);
    vfprintf(DEBUG_STREAM, format, args); // same as vcocosim_log_printf, avoid overhead
    fflush(DEBUG_STREAM);
  }
}

void cocosim_log(int level, const char* format, ...){
  if (level & DEBUG_LEVEL) {
    va_list(args);
    va_start(args, format);
    vcocosim_log(level, format, args);
    va_end(args);
    fflush(DEBUG_STREAM);
  }
}

bool cocosim_log_call_init(bool to_ns3, int* ret, const char* format, ...){
  if (LOG_DEBUG & DEBUG_LEVEL && (LOG_DEBUG2 & DEBUG_LEVEL || !ret)) {
    if(to_ns3){
      cocosim_log(LOG_DEBUG, "[ns-3] ");
    }else{
      cocosim_log(LOG_DEBUG, "[posix] ");
    }
    if(ret){
      fprintf(DEBUG_STREAM, "%d = ", *ret);
    }
    va_list(args);
    va_start(args, format);
    vfprintf(DEBUG_STREAM, format, args); // same as vcocosim_log_printf, avoid overhead
    va_end(args);
    return true;
  }else{
    return false;
  }
}

///////////
// Utils //
///////////
/* Get the process ID of the calling process's nth ancestor.  */
inline pid_t getapid(int n) {
  pid_t pid = getpid();

  while(n>0 && pid){ // process with pid 0 has no parent

    // strlen("/proc/") == 6
    // max [pid] for 64 bits is 4194304 then strlen("[pid]") < 7
    // strlen("/stat") == 5
    // then strlen("/proc/[pid]/stat") < 6 + 7 + 5
    char proc_stat_path[6+7+5+1];
    sprintf(proc_stat_path, "/proc/%d/stat", pid);

    // open "/proc/<pid>/stat"
    FILE *fh = fopen(proc_stat_path, "r");
    if (fh == NULL) {
      fprintf(stderr, "Failed opening %s: ", proc_stat_path);
      perror("");
      exit(1);
    }

    // seek to last ')'
    int c;
    long pos = 0;
    while ((c = fgetc(fh)) != EOF) {
      if (c == ')')
        pos = ftell(fh);
    }
    fseek(fh, pos, SEEK_SET);

    // get parent 
    fscanf(fh, " %*c %d", &pid);

    // close "/proc/<pid>/stat"
    fclose(fh);

    // decrement n
    n--;
  }

  if(n>0)
    return -1;
  else
    return pid;
}

//////////
// main //
//////////
bool connectedToNS3 = false;
char* namespace = NULL;

socket_list ns3_sockets = NULL;


dds_return_t
ddsrt_socket(ddsrt_socket_t *sockptr, int domain, int type, int protocol)
{
  if (useNS3 && !connectedToNS3) {
    FILE* fp = fopen(COCOSIM_NS_PID_FILE_PATH,"r");
    if (!fp) {
      cocosim_log(LOG_FATAL, "Failed opening %s", COCOSIM_NS_PID_FILE_PATH);
      perror("");
      exit(EXIT_FAILURE);
    }

    const char delim[2] = ":";
    char* line = NULL;
    size_t len = 0;
    sleep(5); // wait for initialization (TODO sync properly)
    while (getline(&line, &len, fp) != -1) {
      // remove white spaces
      size_t i = 0;
      size_t k = 0;
      while(i+k < len){
        if(line[i+k] == ' '){
          k++;
        }else{
          line[i] = line[i+k];
          i++;
        }
      }
      line[i] = '\0';

      // parse pid
      char* it = line;
      char* token = strsep(&it, delim);
      if (!token) { 
        cocosim_log(LOG_FATAL, "Failed parsing pid from %s\n", it);
        exit(EXIT_FAILURE);
      }
      int pid = atoi(token);

      if (pid == getpid() || pid == getapid(2)){ // also allow if it's the pid of the grandparent process (to use gdb)
        // parse namespace or name if there is no namespace 
        token = strsep(&it, delim);
        if (!token) { 
          cocosim_log(LOG_FATAL, "Failed parsing namespace %s\n", it);
          exit(EXIT_FAILURE);
        }
        if (*token != '\0'){
          namespace = strdup(token);
        }
        break;
      }
    }

    fclose(fp);
    free(line); 

    cocosim_log(LOG_INFO, "namespace: %s\n",namespace);

    if(namespace) {
      cocosim_log(LOG_INFO, "ns3_start_service()\n");
      ns3_start_service();
      connectedToNS3 = true;
    }else{
      useNS3 = false;
    }
  }

  ddsrt_socket_t sock;

  assert(sockptr != NULL);

  bool to_ns3 = useNS3 && type == SOCK_DGRAM;
  cocosim_log_call_init(to_ns3, NULL, "socket(%d, %d, %d)\n", domain, type, protocol);
  if(to_ns3){
    sock = ns3_socket(domain, type, protocol, namespace);
    insert(&ns3_sockets, sock);
  }else{
    sock = socket(domain, type, protocol);
  }
  cocosim_log_call_init(to_ns3, &sock, "socket(%d, %d, %d)\n", domain, type, protocol);

  if (sock != -1) {
    *sockptr = sock;
    return DDS_RETCODE_OK;
  }

  switch (errno) {
    case EACCES:
      return DDS_RETCODE_NOT_ALLOWED;
    case EAFNOSUPPORT:
    case EINVAL:
      return DDS_RETCODE_BAD_PARAMETER;
    case EMFILE:
    case ENFILE:
    case ENOBUFS:
    case ENOMEM:
      return DDS_RETCODE_OUT_OF_RESOURCES;
    default:
      break;
  }

  return DDS_RETCODE_ERROR;
}

dds_return_t
ddsrt_close(
  ddsrt_socket_t sock)
{
  cocosim_log(LOG_DEBUG, "close(%d)\n", sock);
  if (close(sock) != -1)
    return DDS_RETCODE_OK;

  switch (errno) {
    case EBADF:
      return DDS_RETCODE_BAD_PARAMETER;
    case EINTR:
      return DDS_RETCODE_INTERRUPTED;
    default:
      break;
  }

  return DDS_RETCODE_ERROR;
}

dds_return_t
ddsrt_bind(
  ddsrt_socket_t sock,
  const struct sockaddr *addr,
  socklen_t addrlen)
{
  int rc;

  bool to_ns3 = useNS3 && find(ns3_sockets, sock);
  cocosim_log_call_init(to_ns3, NULL, "bind(%d,%s,%d)\n", sock, inet_ntoa(((struct sockaddr_in*) addr)->sin_addr), addrlen);
  if(to_ns3){
    ((struct sockaddr_in*) addr)->sin_addr.s_addr = htonl (INADDR_ANY);
    rc = ns3_bind(sock, addr, addrlen);
  }else{
    rc = bind(sock, addr, addrlen);
  }
  cocosim_log_call_init(to_ns3, &rc, "bind(%d,%s,%d)\n", sock, inet_ntoa(((struct sockaddr_in*) addr)->sin_addr), addrlen);

  if (rc == 0)
    return DDS_RETCODE_OK;

  switch (errno) {
    case EACCES:
      return DDS_RETCODE_NOT_ALLOWED;
    case EADDRINUSE:
      return DDS_RETCODE_PRECONDITION_NOT_MET;
    case EBADF:
    case EINVAL:
    case ENOTSOCK:
      return DDS_RETCODE_BAD_PARAMETER;
    default:
      break;
  }

  return DDS_RETCODE_ERROR;
}

dds_return_t
ddsrt_listen(
  ddsrt_socket_t sock,
  int backlog)
{
  cocosim_log(LOG_DEBUG, "[posix] listen(%d,%d)\n", sock, backlog);
  if (listen(sock, backlog) == 0)
    return DDS_RETCODE_OK;

  switch (errno) {
    case EADDRINUSE:
      return DDS_RETCODE_PRECONDITION_NOT_MET;
    case EBADF:
      return DDS_RETCODE_BAD_PARAMETER;
    case ENOTSOCK:
    case EOPNOTSUPP:
      return DDS_RETCODE_ILLEGAL_OPERATION;
    default:
      break;
  }

  return DDS_RETCODE_ERROR;
}

dds_return_t
ddsrt_connect(
  ddsrt_socket_t sock,
  const struct sockaddr *addr,
  socklen_t addrlen)
{
  cocosim_log(LOG_DEBUG, "[posix] connect(%d,_,%d)\n", sock, /*addr,*/ addrlen);
  if (connect(sock, addr, addrlen) == 0)
    return DDS_RETCODE_OK;

  switch (errno) {
    case EACCES:
    case EPERM:
    case EISCONN:
      return DDS_RETCODE_NOT_ALLOWED;
    case EADDRINUSE:
    case EADDRNOTAVAIL:
      return DDS_RETCODE_PRECONDITION_NOT_MET;
    case EAFNOSUPPORT:
    case EBADF:
    case ENOTSOCK:
    case EPROTOTYPE:
      return DDS_RETCODE_BAD_PARAMETER;
    case EAGAIN:
      return DDS_RETCODE_OUT_OF_RESOURCES;
    case EALREADY:
      return DDS_RETCODE_TRY_AGAIN;
    case ECONNREFUSED:
    case ENETUNREACH:
      return DDS_RETCODE_NO_CONNECTION;
    case EINPROGRESS:
      return DDS_RETCODE_IN_PROGRESS;
    case EINTR:
      return DDS_RETCODE_INTERRUPTED;
    case ETIMEDOUT:
      return DDS_RETCODE_TIMEOUT;
    default:
      break;
  }

  return DDS_RETCODE_ERROR;
}

dds_return_t
ddsrt_accept(
  ddsrt_socket_t sock,
  struct sockaddr *addr,
  socklen_t *addrlen,
  ddsrt_socket_t *connptr)
{
  ddsrt_socket_t conn;

  cocosim_log(LOG_DEBUG, "[posix] accept(%d,_,_)\n", sock/*, addr, addrlen*/);
  if ((conn = accept(sock, addr, addrlen)) != -1) {
    *connptr = conn;
    return DDS_RETCODE_OK;
  }

  switch (errno) {
    case EAGAIN:
#if EAGAIN != EWOULDBLOCK
    case EWOULDBLOCK:
#endif
      return DDS_RETCODE_TRY_AGAIN;
    case EBADF:
    case EFAULT:
    case EINVAL:
      return DDS_RETCODE_BAD_PARAMETER;
    case ECONNABORTED:
      return DDS_RETCODE_NO_CONNECTION;
    case EINTR:
      return DDS_RETCODE_INTERRUPTED;
    case EMFILE:
    case ENFILE:
    case ENOBUFS:
    case ENOMEM:
      return DDS_RETCODE_OUT_OF_RESOURCES;
    case ENOTSOCK:
    case EOPNOTSUPP:
      return DDS_RETCODE_ILLEGAL_OPERATION;
    case EPROTO:
      return DDS_RETCODE_ERROR;
    case EPERM:
      return DDS_RETCODE_NOT_ALLOWED;
    default:
      break;
  }

  return DDS_RETCODE_ERROR;
}

dds_return_t
ddsrt_getsockname(
  ddsrt_socket_t sock,
  struct sockaddr *addr,
  socklen_t *addrlen)
{
    int rc;

    bool to_ns3 = useNS3 && find(ns3_sockets, sock);
    cocosim_log_call_init(to_ns3, NULL, "getsockname(%d,_,_)\n", sock);
    if(to_ns3){
      rc = ns3_getsockname(sock, addr, addrlen);
    }else{
      rc = getsockname(sock, addr, addrlen);
    }
    cocosim_log_call_init(to_ns3, &rc, "getsockname(%d,_,_)\n", sock);

    if (rc == 0)
      return DDS_RETCODE_OK;

    switch (errno) {
      case EBADF:
      case EFAULT:
      case EINVAL:
      case ENOTSOCK:
        return DDS_RETCODE_BAD_PARAMETER;
      case ENOBUFS:
        return DDS_RETCODE_OUT_OF_RESOURCES;
      default:
        break;
    }

    return DDS_RETCODE_ERROR;
}

dds_return_t
ddsrt_getsockopt(
  ddsrt_socket_t sock,
  int32_t level,
  int32_t optname,
  void *optval,
  socklen_t *optlen)
{
  int rc;

  bool to_ns3 = useNS3 && find(ns3_sockets, sock);
  cocosim_log_call_init(to_ns3, NULL, "getsockopt(%d,%d,%d,_,_)\n", sock, level, optname);
  if(to_ns3){
    rc = -2; // fail with DDS_RETCODE_BAD_PARAMETER
  }else{
    rc = getsockopt(sock, level, optname, optval, optlen);
  }
  cocosim_log_call_init(to_ns3, &rc, "getsockopt(%d,%d,%d,_,_)\n", sock, level, optname);

  if (rc == 0)
    return DDS_RETCODE_OK;
  else if (rc == -2)
    return DDS_RETCODE_BAD_PARAMETER;

  switch (errno) {
    case EBADF:
    case EFAULT:
    case EINVAL:
    case ENOPROTOOPT:
    case ENOTSOCK:
      return DDS_RETCODE_BAD_PARAMETER;
    default:
      break;
  }

  return DDS_RETCODE_ERROR;
}

dds_return_t
ddsrt_setsockopt(
  ddsrt_socket_t sock,
  int32_t level,
  int32_t optname,
  const void *optval,
  socklen_t optlen)
{
  switch (optname) {
    case SO_SNDBUF:
    case SO_RCVBUF:
      /* optlen == 4 && optval == 0 does not work. */
      if (!(optlen == 4 && *((unsigned *)optval) == 0)) {
        break;
      }
      /* falls through */
    case SO_DONTROUTE:
      /* SO_DONTROUTE causes problems on macOS (e.g. no multicasting). */
      return DDS_RETCODE_OK;
  }


  int rc;

  char optvalhexa[(optlen*2)+1];
  char* ptr = optvalhexa;
  for (socklen_t i = 0; i < optlen; i++) {
    ptr += sprintf(ptr, "%02x", ((unsigned char*) optval)[i]);
  }

  bool to_ns3 = useNS3 && find(ns3_sockets, sock);
  cocosim_log_call_init(to_ns3, NULL, "setsockopt(%d,%d,%d,0x%s,%d)\n", sock, level, optname, optvalhexa, optlen);
  if(to_ns3){
    rc = 0; // do nothing
  }else{
    rc = setsockopt(sock, level, optname, optval, optlen);
  }
  cocosim_log_call_init(to_ns3, &rc, "setsockopt(%d,%d,%d,0x%s,%d)\n", sock, level, optname, optvalhexa, optlen);

  if (rc == -1) {
    goto err_setsockopt;
  }

#if defined(__APPLE__) || defined(__FreeBSD__)
  if (level == SOL_SOCKET && optname == SO_REUSEADDR) {
    cocosim_log_call_init(to_ns3, NULL, "setsockopt(%d,%d,%d,0x%s,%d)\n", sock, level, SO_REUSEPORT, optvalhexa, optlen);
    if(to_ns3){
      rc = 0; // do nothing
    }else{
      rc = setsockopt(sock, level, SO_REUSEPORT, optval, optlen);
    }
    cocosim_log_call_init(to_ns3, &rc, "setsockopt(%d,%d,%d,0x%s,%d)\n", sock, level, SO_REUSEPORT, optvalhexa, optlen);
  }

  if (rc == -1) {
    goto err_setsockopt;
  }
#endif /* __APPLE__ || __FreeBSD__ */

  return DDS_RETCODE_OK;
err_setsockopt:
  switch (errno) {
    case EBADF:
    case EINVAL:
    case ENOPROTOOPT:
    case ENOTSOCK:
      return DDS_RETCODE_BAD_PARAMETER;
    default:
      break;
  }

  return DDS_RETCODE_ERROR;
}

dds_return_t
ddsrt_setsocknonblocking(
  ddsrt_socket_t sock,
  bool nonblock)
{
  int flags;

  cocosim_log(LOG_FATAL,"\"fcntl\" not implemented)\n");
  exit(-1);

  cocosim_log(LOG_DEBUG, "fcntl(%d,%d,%d)\n", sock, F_GETFL, 0);
  flags = fcntl(sock, F_GETFL, 0);
  if (flags == -1) {
    goto err_fcntl;
  } else {
    if (nonblock) {
      flags |= O_NONBLOCK;
    } else {
      flags &= ~O_NONBLOCK;
    }
    cocosim_log(LOG_DEBUG, "fcntl(%d,%d,%d)\n", sock, F_SETFL, flags);
    if (fcntl(sock, F_SETFL, flags) == -1) {
      goto err_fcntl;
    }
  }

  return DDS_RETCODE_OK;
err_fcntl:
  switch (errno) {
    case EACCES:
    case EAGAIN:
    case EPERM:
      return DDS_RETCODE_ERROR;
    case EBADF:
    case EINVAL:
      return DDS_RETCODE_BAD_PARAMETER;
    default:
      break;
  }

  return DDS_RETCODE_ERROR;
}

static inline dds_return_t
recv_error_to_retcode(int errnum)
{
  switch (errnum) {
    case EAGAIN:
#if EAGAIN != EWOULDBLOCK
    case EWOULDBLOCK:
#endif
      return DDS_RETCODE_TRY_AGAIN;
    case EBADF:
    case EFAULT:
    case EINVAL:
    case ENOTSOCK:
      return DDS_RETCODE_BAD_PARAMETER;
    case ECONNREFUSED:
      return DDS_RETCODE_NO_CONNECTION;
    case EINTR:
      return DDS_RETCODE_INTERRUPTED;
    case ENOMEM:
      return DDS_RETCODE_OUT_OF_RESOURCES;
    case ENOTCONN:
      return DDS_RETCODE_ILLEGAL_OPERATION;
    default:
      break;
  }

  return DDS_RETCODE_ERROR;
}

dds_return_t
ddsrt_recv(
  ddsrt_socket_t sock,
  void *buf,
  size_t len,
  int flags,
  ssize_t *rcvd)
{

  ssize_t n;

  cocosim_log(LOG_FATAL,"\"recv\" not implemented)\n");
  exit(-1);

  cocosim_log(LOG_DEBUG, "recv(%d,_,%zu,%d)\n", sock, /*buf,*/ len, flags);
  if ((n = recv(sock, buf, len, flags)) != -1) {
    assert(n >= 0);
    *rcvd = n;
    return DDS_RETCODE_OK;
  }

  return recv_error_to_retcode(errno);
}

dds_return_t
ddsrt_recvmsg(
  ddsrt_socket_t sock,
  ddsrt_msghdr_t *msg,
  int flags,
  ssize_t *rcvd)
{
    ssize_t n;

    bool to_ns3 = useNS3 && find(ns3_sockets, sock);
    cocosim_log_call_init(to_ns3, NULL, "recvmsg(%d,_,%d)\n", sock, flags);
    if(to_ns3){
      n = ns3_recvmsg(sock, msg, flags);
    }else{
      n = recvmsg(sock, msg, flags);
    }
    /* int n_int = (int) n; */ 
    /* cocosim_log_call_init(to_ns3, &n_int, "recvmsg(%d,_,%d)\n", sock, flags); */

    if (n != -1) {
      assert(n >= 0);
      *rcvd = n;
      return DDS_RETCODE_OK;
    }

    return recv_error_to_retcode(errno);
}

static inline dds_return_t
send_error_to_retcode(int errnum)
{
  switch (errnum) {
    case EACCES:
    case EPERM:
      return DDS_RETCODE_NOT_ALLOWED;
    case EAGAIN:
#if EAGAIN != EWOULDBLOCK
    case EWOULDBLOCK:
#endif
    case EALREADY:
      return DDS_RETCODE_TRY_AGAIN;
    case EBADF:
    case EFAULT:
    case EINVAL:
    case ENOTSOCK:
    case EOPNOTSUPP:
      return DDS_RETCODE_BAD_PARAMETER;
    case ECONNRESET:
      return DDS_RETCODE_NO_CONNECTION;
    case EDESTADDRREQ:
    case EISCONN:
    case ENOTCONN:
    case EPIPE:
      return DDS_RETCODE_ILLEGAL_OPERATION;
    case EINTR:
      return DDS_RETCODE_INTERRUPTED;
    case EMSGSIZE:
      return DDS_RETCODE_NOT_ENOUGH_SPACE;
    case ENOBUFS:
    case ENOMEM:
      return DDS_RETCODE_OUT_OF_RESOURCES;
    case EHOSTUNREACH:
    case EHOSTDOWN:
      return DDS_RETCODE_NO_CONNECTION;
    default:
      break;
  }

  return DDS_RETCODE_ERROR;
}

dds_return_t
ddsrt_send(
  ddsrt_socket_t sock,
  const void *buf,
  size_t len,
  int flags,
  ssize_t *sent)
{
  ssize_t n;

  cocosim_log(LOG_FATAL,"\"send\" not implemented)\n");
  exit(-1);

  cocosim_log(LOG_DEBUG, "send(%d,_,%zu,%d)\n", sock, /*buf,*/ len, flags);
  if ((n = send(sock, buf, len, flags)) != -1) {
    assert(n >= 0);
    *sent = n;
    return DDS_RETCODE_OK;
  }

  return send_error_to_retcode(errno);
}

dds_return_t
ddsrt_sendmsg(
  ddsrt_socket_t sock,
  const ddsrt_msghdr_t *msg,
  int flags,
  ssize_t *sent)
{
  ssize_t n;

  bool to_ns3 = useNS3 && find(ns3_sockets, sock);
  cocosim_log_call_init(to_ns3, NULL, "sendmsg(%d,_,%d)\n", sock, flags);
  if(to_ns3){
    n = ns3_sendmsg(sock, msg, flags);
  }else{
    n = sendmsg(sock, msg, flags);
  }
  int n_int = (int) n;
  cocosim_log_call_init(to_ns3, &n_int, "sendmsg(%d,_,%d)\n", sock, flags);

  if (n != -1) {
    assert(n >= 0);
    *sent = n;
    return DDS_RETCODE_OK;
  }

  return send_error_to_retcode(errno);
}

inline void print_fdset(int32_t nfds, fd_set *fds) {
  if(fds){
    bool first = true;
    for (int i = 0; i < nfds; i++) {
      if (FD_ISSET(i, fds)) {
        if (first){
          first = false;
        }else{
          cocosim_log_printf(LOG_DEBUG, "|");
        }
        cocosim_log_printf(LOG_DEBUG, "%d", i);
      }
    }
  }else{
    cocosim_log_printf(LOG_DEBUG, "NULL");
  }
}

inline void cocosim_log_select(
  int32_t nfds,
  fd_set *readfds,
  fd_set *writefds,
  fd_set *errorfds,
  struct timeval *timeout)
{
  cocosim_log_printf(LOG_DEBUG, "select(%d,", nfds);
  print_fdset(nfds, readfds);
  cocosim_log_printf(LOG_DEBUG, ",");
  print_fdset(nfds, writefds);
  cocosim_log_printf(LOG_DEBUG, ",");
  print_fdset(nfds, errorfds);
  cocosim_log_printf(LOG_DEBUG, ",");
  cocosim_log_printf(LOG_DEBUG, "%d)\n",timeout);
}

dds_return_t
ddsrt_select(
  int32_t nfds,
  fd_set *readfds,
  fd_set *writefds,
  fd_set *errorfds,
  dds_duration_t reltime,
  int32_t *ready)
{

  int n;
  struct timeval tv, *tvp = NULL;

  tvp = ddsrt_duration_to_timeval_ceil(reltime, &tv);

  bool to_ns3 = useNS3; // if using NS3 -> all sockets are managed by ns-3 (TODO improve)
  if( cocosim_log_call_init(to_ns3, NULL, "")){
    cocosim_log_select(nfds, readfds, writefds, errorfds, tvp);
  }
  if(to_ns3){ 
    n = ns3_select(nfds, readfds, writefds, errorfds, tvp);
  }else{
    n = select(nfds, readfds, writefds, errorfds, tvp);
  }
  if( cocosim_log_call_init(to_ns3, &n, "")){
    cocosim_log_select(nfds, readfds, writefds, errorfds, tvp);
  }

  if (n != -1) {
    *ready = n;
    return (n == 0 ? DDS_RETCODE_TIMEOUT : DDS_RETCODE_OK);
  }

  switch (errno) {
    case EINTR:
      return DDS_RETCODE_INTERRUPTED;
    case EBADF:
    case EINVAL:
      return DDS_RETCODE_BAD_PARAMETER;
    case ENOMEM:
      return DDS_RETCODE_OUT_OF_RESOURCES;
    default:
      break;
  }

  return DDS_RETCODE_ERROR;
}
