#include "csapp.h"

#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400
#define MAX_CACHE_NUM 10

typedef char string[MAXLINE];

typedef struct {
  string url;
  char content[MAX_OBJECT_SIZE];
  int content_size;
  int timestamp;
} cache_file_t;

typedef struct {
  int using_cache_num;
  cache_file_t cache_files[MAX_CACHE_NUM];
} cache_t;

void init_cache();
int query_cache(rio_t *rio_p, string url);
int add_cache(string url, char *content, int content_size);
