#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>
#include <errno.h>


#include "QueryProtocol.h"
#include "MovieSet.h"
#include "MovieIndex.h"
#include "DocIdMap.h"
#include "htll/Hashtable.h"
#include "QueryProcessor.h"
#include "FileParser.h"
#include "FileCrawler.h"

#define BUFFER_SIZE 1000
#define SIGCH_HANDLE_NUM 20
#define SIGINT_HANDLE_NUM 14
#define ARGV_NUM 3
#define ARGV_SECOND 2
#define LISTEN_NUM 10
#define SEARCH_RESULT_LENGTH 1500
#define STRING_LENGTH 12
#define SLEEP_NUM 3
/**
 * By Yuqing Miao
 * Date: 04-22-2019
 * Final project
 */
int Cleanup();

DocIdMap docs;
Index docIndex;

char movieSearchResult[SEARCH_RESULT_LENGTH];

void sigchld_handler(int s) {
  write(0, "Handling zombies...\n", SIGCH_HANDLE_NUM);
  // waitpid() might overwrite errno, so we save and restore it:
  int saved_errno = errno;

  while (waitpid(-1, NULL, WNOHANG) > 0);

  errno = saved_errno;
}

void sigint_handler(int sig) {
  write(0, "Ahhh! SIGINT!\n", SIGINT_HANDLE_NUM);
  Cleanup();
  exit(0);
}

int CheckACK(int sock_fd) {
  char resp[BUFFER_SIZE];
  int len = read(sock_fd, resp, BUFFER_SIZE - 1);
  resp[len] = '\0';
  return CheckAck(resp);
}

int HandleConnections(int sock_fd) {
  // Step 5: Accept connection
  // Fork on every connection.
  struct sockaddr_storage their_addr;
  socklen_t sin_size;
  while (1) {
    printf("Waiting for connection...\n");
    sin_size = sizeof their_addr;
    int client_fd = accept(sock_fd, (struct sockaddr *)&their_addr, &sin_size);
    if (client_fd == -1) {
      perror("accept");
      continue;
    }
    if (!fork()) {
      sleep(SLEEP_NUM);
      close(sock_fd);
      printf("client connected\n");
      SendAck(client_fd);
      char buffer[BUFFER_SIZE];
      int len = read(client_fd, buffer, sizeof(buffer) - 1);
      buffer[len] = '\0';
      printf("received query: %s \n", buffer);

      SearchResultIter iter  = FindMovies(docIndex, buffer);
      int res_num;
      if (iter == NULL) {
        res_num = 0;
      } else {
        res_num = NumResultsInIter(iter);
      }
      // int res_num = NumResultsInIter(iter);
      printf("number of result for term \"%s\" : %d\n", buffer, res_num);
      char res_str[BUFFER_SIZE];
      sprintf(res_str, "%d", res_num);
      write(client_fd, res_str, strlen(res_str));
      if (CheckACK(client_fd) == -1) {
        return -1;
      }

      char temp[BUFFER_SIZE];

      if (iter == NULL) {
        write(client_fd, "NO RESULT!!!\n", STRING_LENGTH);
        printf("No results for this term. Please try another.\n");
        return -1;
      } else {
        SearchResult sr = (SearchResult)malloc(sizeof(*sr));
        if (sr == NULL) {
          printf("Couldn't malloc SearchResult\n");
        } else {
          while (res_num > 0) {
            SearchResultGet(iter, sr);
            CopyRowFromFile(sr, docs, temp);
            write(client_fd, temp, strlen(temp));
            if (CheckACK(client_fd) == -1) {
              return -1;
            }
            res_num--;
            SearchResultNext(iter);
          }
        }
        free(sr);
      }
      DestroySearchResultIter(iter);

      SendGoodbye(client_fd);
      close(client_fd);
      printf("Client disconnected\n");
      exit(0);
    }
    close(client_fd);
  }
  return 0;
}

void Setup(char *dir) {
  struct sigaction sa;

  sa.sa_handler = sigchld_handler;  // reap all dead processes
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = SA_RESTART;
  if (sigaction(SIGCHLD, &sa, NULL) == -1) {
    perror("sigaction");
    exit(1);
  }

  struct sigaction kill;

  kill.sa_handler = sigint_handler;
  kill.sa_flags = 0;  // or SA_RESTART
  sigemptyset(&kill.sa_mask);

  if (sigaction(SIGINT, &kill, NULL) == -1) {
    perror("sigaction");
    exit(1);
  }

  printf("Crawling directory tree starting at: %s\n", dir);
  // Create a DocIdMap
  docs = CreateDocIdMap();
  CrawlFilesToMap(dir, docs);
  printf("Crawled %d files.\n", NumElemsInHashtable(docs));

  // Create the index
  docIndex = CreateIndex();

  // Index the files
  printf("Parsing and indexing files...\n");
  ParseTheFiles(docs, docIndex);
  printf("%d entries in the index.\n", NumElemsInHashtable(docIndex->ht));
}

int Cleanup() {
  DestroyOffsetIndex(docIndex);
  DestroyDocIdMap(docs);
  return 0;
}

int main(int argc, char **argv) {
  // Get args
  if (argc != ARGV_NUM) {
    printf("please run server as \"./queryserver [datadir] [port]\"\n");
    return 0;
  }
  char *dir_to_crawl = argv[1];
  char *port = argv[ARGV_SECOND];
  Setup(dir_to_crawl);

  // Step 1: Get address stuff
  struct addrinfo hints, *res;
  memset(&hints, 0, sizeof(struct addrinfo));
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_PASSIVE;
  int s = getaddrinfo("localhost", port, &hints, &res);
  if (s != 0) {
    fprintf(stderr, "get address info: %s\n", gai_strerror(s));
    exit(1);
  }

  // Step 2: Open socket
  int sock_fd = socket(AF_INET, SOCK_STREAM, 0);

  // Step 3: Bind socket
  if (bind(sock_fd, res->ai_addr, res->ai_addrlen) != 0) {
    perror("bind()");
    exit(1);
  }

  // Step 4: Listen on the socket
  if (listen(sock_fd, LISTEN_NUM) != 0) {
    perror("listen()");
    exit(1);
  }

  // Step 5: Handle the connections
  HandleConnections(sock_fd);

  // Got Kill signal
  close(sock_fd);
  Cleanup();
  return 0;
}
