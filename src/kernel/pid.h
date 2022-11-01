#define PID_MAX 0x8000
 
typedef struct pidmap {
    unsigned int free_num;
    char map[4096];
} pidmap_t;

pidmap_t pidmap;

int alloc_pid();
void free_pid(int pid);