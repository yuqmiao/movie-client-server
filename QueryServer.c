#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <signal.h>


#include "QueryProtocol.h"
#include "MovieSet.h"
#include "MovieIndex.h"
#include "DocIdMap.h"
#include "htll/Hashtable.h"
#include "QueryProcessor.h"
#include "FileParser.h"
#include "FileCrawler.h"

DocIdMap docs;
Index docIndex;

#define BUFFER_SIZE 1000
#define SEARCH_RESULT_LENGTH 1500
#define ARGV_NUM 3
#define SIGI_HANDLER 34
#define ARGV_SECOND 2
#define PORT_NUM 1500
#define LISTEN_NUM 10

/**
 * By Yuqing Miao
 * Date: 04-22-2019
 * Final project
 */

char movieSearchResult[SEARCH_RESULT_LENGTH];
char *port_string = "1500";

int getDesc(SearchResultIter searchResultIter, char *resArray[]);

int Cleanup();

void sigint_handler(int sig) {
  write(0, "Exit signal sent. Cleaning up...\n", SIGI_HANDLER);
  Cleanup();
  exit(0);
}


void Setup(char *dir) {
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
  if (atoi(argv[ARGV_SECOND]) != PORT_NUM) {
    printf("please input valid port number!\n");
    return 0;
  }
  // Setup graceful exit
  struct sigaction kill;

  kill.sa_handler = sigint_handler;
  kill.sa_flags = 0;  // or SA_RESTART
  sigemptyset(&kill.sa_mask);

  if (sigaction(SIGINT, &kill, NULL) == -1) {
    perror("sigaction");
    exit(1);
  }
  char *dir_to_crawl = argv[1];
  Setup(dir_to_crawl);

  // Step 1: get address/port info to open
  struct addrinfo hints, *result;
  memset(&hints, 0, sizeof(struct addrinfo));
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_PASSIVE;
  int s = getaddrinfo(NULL, port_string, &hints, &result);
  if (s != 0) {
    // fprintf (stderr, "get address info: %s\n", gai_strerror(s));
    exit(1);
  }
  // Step 2: Open socket
  int sock_fd = socket(AF_INET, SOCK_STREAM, 0);
  // Step 3: Bind socket
  if (bind(sock_fd, result->ai_addr, result->ai_addrlen) != 0) {
    perror("bind()");
    exit(1);
  }
  // Step 4: Listen on the socket
  if (listen(sock_fd, LISTEN_NUM) != 0) {
    perror("listen()");
    exit(1);
  }

  struct sockaddr_in *result_addr = (struct sockaddr_in *) result->ai_addr;
  printf("Listening on file descriptor %d, port %d\n",
  sock_fd, ntohs(result_addr->sin_port));
  printf("Waiting for connection...\n");

  // Step 5: Handle clients that connect
  while (1) {
    int client_fd = accept(sock_fd, NULL, NULL);
    printf("Connection made: client_fd=%d\n", client_fd);
    printf("Send ACK\n");
    SendAck(client_fd);
    printf("read query\n");
    char buffer[BUFFER_SIZE] = {0};
    int len = read(client_fd, buffer, BUFFER_SIZE - 1);
    buffer[len] = '\0';
    printf("Read %d chars\n", len);
    printf("======\n");
    printf("Already read %s\n", buffer);

    // get the result num
    SearchResultIter iter = FindMovies(docIndex, buffer);
    int res_num;
    char *resArray[BUFFER_SIZE];

    if (iter == NULL) {
      printf("iter is null!!!\n");
      res_num = 0;
      resArray[0] = "None";
    } else {
       res_num = NumResultsInIter(iter);
       getDesc(iter, resArray);
    }
  char resString[BUFFER_SIZE] = {0};
  sprintf(resString, "%d", res_num);
  printf("num == %d\n", res_num);
  write(client_fd, resString, strlen(resString));
  printf("Begin sending\n");

  int i = 0;
  char resp[BUFFER_SIZE];
  len = read(client_fd, resp, BUFFER_SIZE-1);
  resp[len] = '\0';
    while (CheckAck(resp) == 0) {
      printf("sending %s len is %ld \n", resArray[i], strlen(resArray[i]));
      write(client_fd, resArray[i], strlen(resArray[i]));
      len = read(client_fd, resp, BUFFER_SIZE-1);
      resp[len] = '\0';
      printf("RECEIVED %s \n", resp);
      if (i < res_num-1) {
        i++;
      } else {
        for (int i = 0;i < res_num;i++) {
          free(resArray[i]);
        }
        printf("sending goodbye\n");
        SendGoodbye(client_fd);
        break;
      }
    }
  // printf("finish for loop\n");

  // Step 6: Close the socket
  close(client_fd);
  }
  // Got Kill signal
  free(result);
  close(sock_fd);
  Cleanup();
  return 0;
}

int getDesc(SearchResultIter searchResultIter, char *resArray[]) {
  SearchResult sr = (SearchResult)malloc(sizeof(*sr));
    if (sr == NULL) {
      printf("Couldn't malloc SearchResult in main.c\n");
      return -1;
    }
  int cnt = 0;
  SearchResultGet(searchResultIter, sr);
  char dest[BUFFER_SIZE];
  CopyRowFromFile(sr, docs, dest);
  char* curRow = (char*)malloc(sizeof(char) * (strlen(dest)+1));
  strcpy(curRow, dest);
  resArray[cnt] = curRow;
  int result;

  while (SearchResultIterHasMore(searchResultIter) != 0) {
    cnt++;
    result =  SearchResultNext(searchResultIter);
    if (result < 0) {
      printf("error retrieving result\n");
      break;
      }
      SearchResultGet(searchResultIter, sr);
      CopyRowFromFile(sr, docs, dest);
      char* curRow = (char*)malloc(sizeof(char) * (strlen(dest)+1));
      strcpy(curRow, dest);
      resArray[cnt] = curRow;
    }
    free(sr);
    DestroySearchResultIter(searchResultIter);
  return 0;
}
