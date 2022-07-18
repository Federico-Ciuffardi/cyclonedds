#ifndef COCOSIM_SOCKLIST_H
#define COCOSIM_SOCKLIST_H

#include <stdbool.h>
#include <stdlib.h>
#include "dds/cocosim/log.h"

#if _WIN32
#include "dds/ddsrt/sockets/windows.h"
#else
#include "dds/ddsrt/sockets/posix.h"
#endif

typedef struct socklist_node {
  ddsrt_socket_t v;
  struct socklist_node *next;
} socklist_node;

typedef struct socklist_node* socklist;

void insert(socklist *sl,ddsrt_socket_t v);

bool find(socklist sl, ddsrt_socket_t v);

void print(socklist sl);

#endif /* COCOSIM_SOCKLIST_H */
