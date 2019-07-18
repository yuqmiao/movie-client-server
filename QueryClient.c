#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <arpa/inet.h>

#include "includes/QueryProtocol.h"

char *port_string = "1500";
unsigned short int port;
char *ip = "127.0.0.1";

#define BUFFER_SIZE 1000
#define ARGV_NUM 3
#define ARGV_SECOND 2
#define EXIT_NUM 2
/**
 * By Yuqing Miao
 * Date: 04-22-2019
 * Final project
 */

void RunPrompt();

void RunQuery(char *query) {
  int sock_fd, s, c;
  struct addrinfo hints, *res;

  memset(&hints, 0, sizeof(struct addrinfo));
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_PASSIVE;

  // Find the address
  s = getaddrinfo(ip, port_string, &hints, &res);
  if (s != 0) {
    fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(s));
    exit(1);
  }

  // Create the socket
  sock_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (sock_fd == -1) {
    printf("Socket error");
  }

  // Connect to the server
  c = connect(sock_fd, res->ai_addr, res->ai_addrlen);
  if (c == -1) {
    perror("Connect error!");
    exit(EXIT_NUM);
  }

  // check ACK.
  char resp[BUFFER_SIZE];
  int len = read(sock_fd, resp, BUFFER_SIZE - 1);
  resp[len] = '\0';
  if (CheckAck(resp) == -1) {
    return;
  }
  // Do the query-protocol
  printf("SENDING: %s\n", query);
  printf("==========\n");

  // sending query.
  write(sock_fd, query, strlen(query));

  // get response.
  len = read(sock_fd, resp, BUFFER_SIZE - 1);
  resp[len] = '\0';
  int respNum = atoi(resp);
  printf("receive num of response: %d\n", respNum);

  // send ACK
  SendAck(sock_fd);
  // get response
  len = read(sock_fd, resp, BUFFER_SIZE - 1);
  resp[len] = '\0';
  printf("the resp is %s\n", resp);
  char* message = resp;
  while (strcmp(message, GOODBYE) != 0) {
    SendAck(sock_fd);
    char resp[BUFFER_SIZE];
    len = read(sock_fd, resp, BUFFER_SIZE-1);
    resp[len] = '\0';
    if (strcmp(resp, GOODBYE) != 0) {
      printf("the resp is %s\n", resp);
    } else {
      printf("Recieved %s.\n", resp);
    }
    message = resp;
  }
  CheckGoodbye(message);
  free(res);
  close(sock_fd);
  return;
}

void RunPrompt() {
  char input[BUFFER_SIZE];

  while (1) {
    printf("Enter a term to search for, or q to quit: ");
    scanf("%s", input);
    printf("input was: %s\n", input);
    if (strlen(input) == 1) {
      if (input[0] == 'q') {
        printf("Thanks for playing! \n");
        return;
      }
    }
    printf("\n\n");
    RunQuery(input);
  }
}

int main(int argc, char **argv) {
  // Check/get arguments
  if (argc != ARGV_NUM) {
    printf("please run client as \"./queryserver [ip_address] [port]\"\n");
    fprintf(stderr, "usage: showip hostname\n");
    return 1;
  }

  // Get info from user
  ip = argv[1];
  port_string = argv[ARGV_SECOND];

  // Run Query
  RunPrompt();
  return 0;
}
