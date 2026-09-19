#include "cache.h"
#include "csapp.h"

#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <unistd.h>

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n";

typedef char string[MAXLINE];
typedef struct {
  string host;
  string port;
  string path;
} url_t;

int parse_url(string url, url_t *url_info) {
  const size_t http_prefix_len = strlen("http://");
  if (strncasecmp(url, "http://", http_prefix_len)) {
    fprintf(stderr, "Not http protocol: %s\n", url);
    return -1;
  }
  char *host_start = url + http_prefix_len;
  char *path_start = strchr(host_start, '/');
  /* The host ends at the path, or at the end of the URL if there is none. */
  char *host_end = path_start ? path_start : host_start + strlen(host_start);
  /* Only a ':' in front of the path can be the port separator. */
  char *port_start = memchr(host_start, ':', host_end - host_start);

  size_t host_len = port_start ? (size_t)(port_start - host_start)
                               : (size_t)(host_end - host_start);
  if (host_len == 0 || host_len >= MAXLINE)
    return -1;
  memcpy(url_info->host, host_start, host_len);
  url_info->host[host_len] = '\0';

  if (port_start) {
    size_t port_len = (size_t)(host_end - port_start - 1);
    if (port_len == 0 || port_len >= MAXLINE)
      return -1;
    memcpy(url_info->port, port_start + 1, port_len);
    url_info->port[port_len] = '\0';
  } else {
    strcpy(url_info->port, "80");
  }

  if (path_start == NULL)
    strcpy(url_info->path, "/");
  else
    strcpy(url_info->path, path_start);

  return 0;
}

/* Room kept aside for the header lines the proxy always appends itself. */
#define HEADER_TAIL_RESERVE 256

/* Append src to dst, keeping dst NUL-terminated and at most cap bytes long.
 * A line that does not fit is dropped rather than overflowing dst. */
static void append_capped(string dst, size_t *used, const char *src,
                          size_t cap) {
  size_t len = strlen(src);
  if (*used + len + 1 > cap)
    return;
  memcpy(dst + *used, src, len + 1);
  *used += len;
}

int parse_header(rio_t *client_rio_p, string header_info, string host) {
  string buf;
  size_t used = 0;
  int has_host_flag = 0;
  size_t cap = MAXLINE - HEADER_TAIL_RESERVE;

  header_info[0] = '\0';
  while (1) {
    if (rio_readlineb(client_rio_p, buf, MAXLINE) <= 0)
      break;
    if (strcmp(buf, "\r\n") == 0)
      break;
    if (!strncasecmp(buf, "Host:", strlen("Host:")))
      has_host_flag = 1;
    if (!strncasecmp(buf, "Connection:", strlen("Connection:")) ||
        !strncasecmp(buf, "Proxy-Connection:", strlen("Proxy-Connection:")) ||
        !strncasecmp(buf, "User-Agent:", strlen("User-Agent:"))) {
      continue;
    }
    append_capped(header_info, &used, buf, cap);
  }
  if (!has_host_flag) {
    snprintf(buf, MAXLINE, "Host: %s\r\n", host);
    append_capped(header_info, &used, buf, cap);
  }
  append_capped(header_info, &used, "Connection: close\r\n", MAXLINE);
  append_capped(header_info, &used, "Proxy-Connection: close\r\n", MAXLINE);
  append_capped(header_info, &used, user_agent_hdr, MAXLINE);
  append_capped(header_info, &used, "\r\n", MAXLINE);
  return 0;
}

void do_get(rio_t *client_rio_p, string url) {
  if (query_cache(client_rio_p, url)) {
    return;
  }
  url_t url_info;
  if (parse_url(url, &url_info) < 0) {
    fprintf(stderr, "Parse url error\n");
    return;
  }
  string header_info;
  parse_header(client_rio_p, header_info, url_info.host);

  int server_fd = open_clientfd(url_info.host, url_info.port);
  if (server_fd < 0) {
    fprintf(stderr, "Open connect to %s:%s error\n", url_info.host,
            url_info.port);
    return;
  }

  rio_t server_rio;
  rio_readinitb(&server_rio, server_fd);
  string buf;
  /* Emit the request line and the headers in pieces: the path goes straight
   * to the socket, so a long URL can never overflow a fixed-size buffer. */
  if (rio_writen(server_fd, "GET ", 4) != 4 ||
      rio_writen(server_fd, url_info.path, strlen(url_info.path)) !=
          strlen(url_info.path) ||
      rio_writen(server_fd, " HTTP/1.0\r\n", 11) != 11 ||
      rio_writen(server_fd, header_info, strlen(header_info)) !=
          strlen(header_info)) {
    fprintf(stderr, "Send request line and header error\n");
    close(server_fd);
    return;
  }

  int resp_total = 0, resp_current = 0;
  char file_cache[MAX_OBJECT_SIZE];
  int client_fd = client_rio_p->rio_fd;

  // response loop
  while ((resp_current = rio_readnb(&server_rio, buf, MAXLINE))) {
    if (resp_current < 0) {
      fprintf(stderr, "Read server response error\n");
      close(server_fd);
      return;
    }
    if (resp_total + resp_current < MAX_OBJECT_SIZE) {
      memcpy(file_cache + resp_total, buf, resp_current);
    }
    resp_total += resp_current;
    if (rio_writen(client_fd, buf, resp_current) != resp_current) {
      fprintf(stderr, "Send response to client error\n");
      close(server_fd);
      return;
    }
  }
  if (resp_total < MAX_OBJECT_SIZE) {
    add_cache(url, file_cache, resp_total);
  }
  close(server_fd);
  return;
}

void *thread(void *vargp) {
  pthread_detach(pthread_self());
  int client_fd = *((int *)vargp);
  free(vargp);
  printf("someone connected!\n");

  size_t n;
  char buf[MAXLINE];
  rio_t client_rio;
  rio_readinitb(&client_rio, client_fd);
  if ((n = rio_readlineb(&client_rio, buf, MAXLINE)) <= 0) {
    fprintf(stderr, "Read request line error: %s\n", strerror(errno));
    close(client_fd);
    printf("someone disconnected!\n");
    return NULL;
  }

  string method, url, http_version;
  if (sscanf(buf, "%s %s %s", method, url, http_version) != 3) {
    fprintf(stderr, "Parse request line error: %s\n", strerror(errno));
    close(client_fd);
    printf("someone disconnected!\n");
    return NULL;
  }

  if (!strcasecmp(method, "GET")) {
    do_get(&client_rio, url);
  }

  close(client_fd);
  printf("someone disconnected!\n");
  return NULL;
}

int main(int argc, char **argv) {
  if (argc != 2) {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }

  signal(SIGPIPE, SIG_IGN);
  init_cache();

  pthread_t tid;
  int listenfd, *connfd;
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;

  listenfd = open_listenfd(argv[1]);
  for (;;) {
    clientlen = sizeof(clientaddr);
    connfd = (int *)malloc(sizeof(int));
    *connfd = accept(listenfd, (SA *)&clientaddr, &clientlen);
    if (*connfd < 0) {
      fprintf(stderr, "Accept Error: %s\n", strerror(errno));
      continue;
    }
    pthread_create(&tid, NULL, thread, connfd);
  }
  close(listenfd);
}
