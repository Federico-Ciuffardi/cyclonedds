#include "dds/cocosim/ros.h"

pid_t getapid(int n) {
  pid_t pid = getpid();

  while (n > 0 && pid) { // process with pid 0 has no parent

    // strlen("/proc/") == 6
    // max [pid] for 64 bits is 4194304 then strlen("[pid]") < 7
    // strlen("/stat") == 5
    // then strlen("/proc/[pid]/stat") < 6 + 7 + 5
    char proc_stat_path[6 + 7 + 5 + 1];
    sprintf(proc_stat_path, "/proc/%d/stat", pid);

    // open "/proc/<pid>/stat"
    FILE *fh = fopen(proc_stat_path, "r");
    if (fh == NULL) {
      fprintf(stderr, "Failed opening %s: ", proc_stat_path);
      perror("");
      exit(1);
    }

    // seek to last ')'
    int  c;
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

  if (n > 0)
    return -1;
  else
    return pid;
}

void get_ns(char** ns) {
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

    if (pid == getpid() || pid == getppid() || pid == getapid(2)){ // also allow if it's the pid of the grandparent process (to use gdb)
      // parse ns or name if there is no ns 
      token = strsep(&it, delim);
      if (!token) { 
        cocosim_log(LOG_FATAL, "Failed parsing ns %s\n", it);
        exit(EXIT_FAILURE);
      }
      if (*token != '\0'){
        *ns = strdup(token);
      }
      break;
    }
  }

  fclose(fp);
  free(line); 
}
