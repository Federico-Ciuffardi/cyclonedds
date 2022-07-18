#include "dds/cocosim/socklist.h"

/////////////////////////
// ddsrt_socket_t list //
/////////////////////////

inline void insert(socklist *sl,ddsrt_socket_t v){
  socklist new = (socklist) malloc(sizeof(socklist_node));
  new->next = *sl;
  new->v = v;
  *sl = new;
}

inline bool find(socklist sl, ddsrt_socket_t v){
  socklist it;
  for(it = sl; it != NULL && it->v != v; it=it->next);
  return it != NULL;
}

inline void print(socklist sl){
  socklist it;
  cocosim_log(LOG_INFO,"Sock list:");
  for(it = sl; it != NULL; it=it->next){
    cocosim_log_printf(LOG_INFO,"=%d", it->v);
  }
  cocosim_log_printf(LOG_INFO,"\n");
}
