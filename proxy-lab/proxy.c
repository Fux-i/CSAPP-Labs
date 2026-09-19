#include "csapp.h"
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

void *thread(void *vargp) {
  pthread_detach(pthread_self());
  int connfd = *((int *)vargp);
  free(vargp);
  printf("someone connected!\n");

  size_t n;
  char buf[MAXLINE];
  rio_t rio;
  Rio_readinitb(&rio, connfd);
  while ((n = Rio_readlineb(&rio, buf, MAXLINE)) != 0) {
    printf("server received %zu bytes\n", n);
    Rio_writen(connfd, buf, n);
  }

  printf("someone disconnected!\n");
  return NULL;
}

int main(int argc, char **argv) {
  if (argc != 2) {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }

  signal(SIGPIPE, SIG_IGN);

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
