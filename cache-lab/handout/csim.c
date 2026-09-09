#include "cachelab.h"
#include <errno.h>
#include <getopt.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

struct {
  int verbose, s, E, b;
  char *t;
} opts;

struct {
  int hit_count, miss_count, eviction_count;
} sta;

typedef struct {
  int valid;
  uint64_t tag, last_used;
} cache_line;
cache_line *cache;

void init(void) {
  opts.verbose = opts.E = 0;
  opts.s = opts.b = -1;
  opts.t = NULL;
  sta.hit_count = sta.miss_count = sta.eviction_count = 0;
}

void print_usage(void) {
  printf("Usage: ./csim [-hv] -s <s> -E <E> -b <b> -t <tracefile>\n"
         "  -h: Optional help flag that prints usage info\n"
         "  -v: Optional verbose flag that displays trace info\n"
         "  -s <s>: Number of set index bits (S = 2^s is the number of sets)\n"
         "  -E <E>: Associativity (number of lines per set)\n"
         "  -b <b>: Number of block bits (B = 2^b is the block size)\n"
         "  -t <tracefile>: Name of the valgrind trace to replay\n");
}

void access_cache(uint64_t address, uint64_t timestamp) {
  size_t set_index = (address >> opts.b) & (((size_t)1 << opts.s) - 1);
  uint64_t tag = (address >> opts.b) >> opts.s;
  cache_line *set = cache + set_index * opts.E;
  cache_line *victim = &set[0];

  for (int index = 0; index < opts.E; ++index) {
    cache_line *line = &set[index];
    if (line->valid && line->tag == tag) {
      ++sta.hit_count;
      line->last_used = timestamp;
      if (opts.verbose)
        printf(" hit");
      return;
    }
    if (!line->valid || (victim->valid && line->last_used < victim->last_used))
      victim = line;
  }

  ++sta.miss_count;
  if (opts.verbose)
    printf(" miss");
  if (victim->valid) {
    ++sta.eviction_count;
    if (opts.verbose)
      printf(" eviction");
  }
  victim->valid = 1;
  victim->tag = tag;
  victim->last_used = timestamp;
}

int main(int argc, char **argv) {
  init();
  int opt;
  while ((opt = getopt(argc, argv, ":hvs:E:b:t:")) != -1) {
    switch (opt) {
    case 'h':
      print_usage();
      return 0;
    case 'v':
      opts.verbose = 1;
      break;
    case 's':
    case 'E':
    case 'b': {
      char *end;
      errno = 0;
      long value = strtol(optarg, &end, 10);
      if (errno || end == optarg || *end != '\0' || value < 0 ||
          value > INT_MAX) {
        fprintf(stderr, "Invalid value for -%c: %s\n", opt, optarg);
        return 1;
      }
      if (opt == 's')
        opts.s = (int)value;
      else if (opt == 'E')
        opts.E = (int)value;
      else
        opts.b = (int)value;
      break;
    }
    case 't':
      opts.t = optarg;
      break;
    case ':':
      fprintf(stderr, "Option -%c needs a value.\n", optopt);
      print_usage();
      return 1;
    case '?':
      fprintf(stderr, "Unknown option: -%c\n", optopt);
      print_usage();
      return 1;
    }
  }
  if (opts.s < 0 || opts.E <= 0 || opts.b < 0 || opts.t == NULL ||
      optind != argc) {
    fprintf(stderr, "Required options: -s <s> -E <E> -b <b> -t <tracefile>\n");
    print_usage();
    return 1;
  }
  if (opts.s + opts.b > 64) {
    fprintf(stderr, "Cache bit counts exceed the address width.\n");
    return 1;
  }

  size_t set_count = (size_t)1 << opts.s;
  if (set_count > SIZE_MAX / sizeof(cache_line) / (size_t)opts.E) {
    fprintf(stderr, "Cache is too large.\n");
    return 1;
  }
  FILE *trace = fopen(opts.t, "r");
  if (trace == NULL) {
    perror(opts.t);
    return 1;
  }
  cache = calloc(set_count * opts.E, sizeof(*cache));
  if (cache == NULL) {
    perror("Unable to allocate cache");
    fclose(trace);
    return 1;
  }

  char operation;
  uint64_t address, timestamp = 0;
  unsigned int size;
  int fields;
  while ((fields = fscanf(trace, " %c %lx,%u", &operation, &address, &size)) ==
         3) {
    if (operation == 'I')
      continue;
    if (operation != 'L' && operation != 'S' && operation != 'M')
      break;
    if (opts.verbose)
      printf("%c %lx,%u", operation, address, size);
    access_cache(address, ++timestamp);
    if (operation == 'M')
      access_cache(address, ++timestamp);
    if (opts.verbose)
      printf("\n");
  }
  int trace_error = fields != EOF || ferror(trace);
  fclose(trace);
  free(cache);
  if (trace_error) {
    fprintf(stderr, "Unable to read a valid trace from %s\n", opts.t);
    return 1;
  }

  printSummary(sta.hit_count, sta.miss_count, sta.eviction_count);
  return 0;
}
