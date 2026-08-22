#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#define PORT 8080
#define MAX_EVENTS 10

int set_nonblocking(int fd) {
  int flags = fcntl(fd, F_GETFL, 0);
  if (flags == -1)
    return -1;
  return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

void handle_client(int client_fd, const char *buffer) {
  char method[16] = {0};
  char path[256] = {0};
  char protocol[16] = {0};

  if (sscanf(buffer, "%15s %255s %15s", method, path, protocol) < 3) {
    const char *bad_request =
        "HTTP/1.1 400 Bad Request\r\nConnection: close\r\n\r\n";
    send(client_fd, bad_request, strlen(bad_request), 0);
    return;
  }

  printf("Method: %s, Path: %s, Protocol: %s\n", method, path, protocol);

  if (strcmp(path, "/") == 0) {
    const char *response = "HTTP/1.1 200 OK\r\n"
                           "Content-Type: text/html\r\n"
                           "Content-Length: 38\r\n"
                           "Connection: close\r\n"
                           "\r\n"
                           "<h1>Hello</h1>";
    send(client_fd, response, strlen(response), 0);
  } else {
    const char *not_found = "HTTP/1.1 404 Not Found\r\n"
                            "Content-Type: text/plain\r\n"
                            "Content-Length: 13\r\n"
                            "Connection: close\r\n"
                            "\r\n"
                            "404 Not Found";
    send(client_fd, not_found, strlen(not_found), 0);
  }
}

int main(void) {
  int server_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (server_fd < 0) {
    perror("Socket creation failed");
    return EXIT_FAILURE;
  }

  int opt = 1;
  setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

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

  int listen_status = listen(server_fd, 10);
  if (listen_status < 0) {
    perror("Listen failed");
    close(server_fd);
    return EXIT_FAILURE;
  }

  if (set_nonblocking(server_fd) < 0) {
    perror("Failed to set non-blocking mode");
    close(server_fd);
    return EXIT_FAILURE;
  }

  int epoll_fd = epoll_create1(0);
  if (epoll_fd < 0) {
    perror("Epoll failed");
    close(server_fd);
    return EXIT_FAILURE;
  }

  struct epoll_event ev;
  ev.events = EPOLLIN;
  ev.data.fd = server_fd;
  int control = epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &ev);

  if (control == -1) {
    perror("Epoll ctl failed");
    close(server_fd);
    close(epoll_fd);
    return EXIT_FAILURE;
  }

  struct epoll_event events[MAX_EVENTS];

  while (1) {
    int nfds = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
    if (nfds < 0) {
      perror("Eppol_wait failed");
      break;
    }

    for (int i = 0; i < nfds; i++) {
      if (events[i].data.fd == server_fd) {
        int client_fd = accept(server_fd, NULL, NULL);
        if (client_fd < 0) {
          perror("Accept failed");
          continue;
        }

        if (set_nonblocking(client_fd) < 0) {
          perror("Failed to set client socket non-blocking");
          close(client_fd);
          continue;
        }

        struct epoll_event client_ev;
        client_ev.events = EPOLLIN;
        client_ev.data.fd = client_fd;

        if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &client_ev) < 0) {
          perror("Failed to add client socket to epoll");
          close(client_fd);
          continue;
        }

        printf("%d\n", client_fd);
      } else {
        int client_fd = events[i].data.fd;
        char buffer[1024] = {0};

        ssize_t bytes_read = recv(client_fd, buffer, sizeof(buffer) - 1, 0);

        if (bytes_read <= 0) {
          printf("Client disconnected %d\n", client_fd);
          epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_fd, NULL);
          close(client_fd);
        } else {
          handle_client(client_fd, buffer);
          epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_fd, NULL);
          close(client_fd);
        }
      }
    }
  }
  close(server_fd);
  return EXIT_SUCCESS;
}
