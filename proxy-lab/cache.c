#include "cache.h"
#include "csapp.h"

static cache_t cache;
static sem_t mutex, w, ts_mutex;
static int readcnt, timestamp;

void init_cache() {
  timestamp = 0;
  readcnt = 0;
  cache.using_cache_num = 0;
  sem_init(&mutex, 0, 1);
  sem_init(&w, 0, 1);
  sem_init(&ts_mutex, 0, 1);
}

int query_cache(rio_t *rio_p, string url) {
  P(&mutex);
  readcnt++;
  if (readcnt == 1)
    P(&w);
  V(&mutex);

  int hit_flag = 0;
  for (int i = 0; i < cache.using_cache_num; i++) {
    if (!strcmp(cache.cache_files[i].url, url)) {
      /* timestamp is guarded by ts_mutex, never by mutex: taking mutex
       * while holding w would invert the lock order used by add_cache. */
      P(&ts_mutex);
      cache.cache_files[i].timestamp = timestamp++;
      V(&ts_mutex);
      rio_writen(rio_p->rio_fd, cache.cache_files[i].content,
                 cache.cache_files[i].content_size);
      hit_flag = 1;
      break;
    }
  }

  P(&mutex);
  readcnt--;
  if (readcnt == 0)
    V(&w);
  V(&mutex);
  if (hit_flag)
    return 1;
  return 0;
}

int add_cache(string url, char *content, int content_size) {
  P(&w);
  int index;
  if (cache.using_cache_num < MAX_CACHE_NUM) {
    index = cache.using_cache_num++;
  } else {
    /* All slots are used: evict the least recently used entry. */
    index = 0;
    int oldest_timestamp = cache.cache_files[0].timestamp;
    for (int i = 1; i < MAX_CACHE_NUM; i++) {
      if (cache.cache_files[i].timestamp < oldest_timestamp) {
        oldest_timestamp = cache.cache_files[i].timestamp;
        index = i;
      }
    }
  }
  strcpy(cache.cache_files[index].url, url);
  memcpy(cache.cache_files[index].content, content, content_size);
  cache.cache_files[index].content_size = content_size;
  P(&ts_mutex);
  cache.cache_files[index].timestamp = timestamp++;
  V(&ts_mutex);
  V(&w);
  return 0;
}
