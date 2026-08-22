#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <unistd.h>

#define PORT 8080

int main(void) {
  int server_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (server_fd < 0) {
    perror("Socket creation failed");
    return EXIT_FAILURE;
  }

  struct sockaddr_in address;
  address.sin_family = AF_INET;
  address.sin_port = htons(PORT);
  address.sin_addr.s_addr = INADDR_ANY;

  int res = bind(server_fd, (struct sockaddr *)&address, sizeof(address));
  if (res < 0) {
    perror("Bind failed");
    close(server_fd);
    return EXIT_FAILURE;
  }

  close(server_fd);
  return EXIT_SUCCESS;
}
