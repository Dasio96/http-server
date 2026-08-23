#include <fcntl.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#define PORT 8080
#define MAX_EVENTS 10
#define NUM_WORKERS 4
#define QUEUE_CAPACITY 256
#define REQUEST_BUF_SIZE 1024

typedef struct {
  int client_fd;
  char request[REQUEST_BUF_SIZE];
} task_t;

task_t *task_queue[QUEUE_CAPACITY] = {0};
int task_count = 0;
pthread_mutex_t queue_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t queue_cond = PTHREAD_COND_INITIALIZER;

int enqueue_task(int client_fd, const char *request) {
  task_t *task = malloc(sizeof(task_t));
  if (!task) {
    return -1;
  }
  task->client_fd = client_fd;
  memcpy(task->request, request, REQUEST_BUF_SIZE);

  pthread_mutex_lock(&queue_mutex);
  if (task_count >= QUEUE_CAPACITY) {
    pthread_mutex_unlock(&queue_mutex);
    free(task);
    return -1;
  }
  task_queue[task_count++] = task;
  pthread_cond_signal(&queue_cond);
  pthread_mutex_unlock(&queue_mutex);
  return 0;
}

const char *get_mime_type(const char *path) {
  const char *ext = strrchr(path, '.');
  if (!ext)
    return "application/octet-stream";
  if (strcmp(ext, ".html") == 0 || strcmp(ext, ".htm") == 0)
    return "text/html";
  if (strcmp(ext, ".css") == 0)
    return "text/css";
  if (strcmp(ext, ".js") == 0)
    return "application/javascript";
  if (strcmp(ext, ".png") == 0)
    return "image/png";
  if (strcmp(ext, ".jpg") == 0 || strcmp(ext, ".jpeg") == 0)
    return "image/jpeg";
  return "text/plain";
}

int set_nonblocking(int fd) {
  int flags = fcntl(fd, F_GETFL, 0);
  if (flags == -1) {
    return -1;
  }
  return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

void serve_file(int client_fd, const char *path) {
  char file_path[512] = {0};

  if (strcmp(path, "/") == 0) {
    snprintf(file_path, sizeof(file_path), "public/index.html");
  } else {
    if (strstr(path, "..")) {
      const char *forbidden = "HTTP/1.1 403 Forbidden\r\n"
                              "Content-Length: 9\r\n"
                              "Connection: close\r\n"
                              "\r\n"
                              "Forbidden";
      send(client_fd, forbidden, strlen(forbidden), 0);
      return;
    }
    snprintf(file_path, sizeof(file_path), "public%s", path);
  }

  FILE *file = fopen(file_path, "rb");
  if (!file) {
    const char *not_found = "HTTP/1.1 404 Not Found\r\n"
                            "Content-Length: 13\r\n"
                            "Connection: close\r\n"
                            "\r\n"
                            "404 Not Found";
    send(client_fd, not_found, strlen(not_found), 0);
    return;
  }

  fseek(file, 0, SEEK_END);
  long file_size = ftell(file);
  fseek(file, 0, SEEK_SET);

  char header[256];
  snprintf(header, sizeof(header),
           "HTTP/1.1 200 OK\r\n"
           "Content-Type: %s\r\n"
           "Content-Length: %ld\r\n"
           "Connection: close\r\n"
           "\r\n",
           get_mime_type(file_path), file_size);
  send(client_fd, header, strlen(header), 0);

  char buffer[1024];
  size_t bytes_read;
  while ((bytes_read = fread(buffer, 1, sizeof(buffer), file)) > 0) {
    send(client_fd, buffer, bytes_read, 0);
  }

  fclose(file);
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

  if (strcmp(method, "GET") == 0) {
    serve_file(client_fd, path);
  } else if (strcmp(method, "POST") == 0) {
    const char *body = strstr(buffer, "\r\n\r\n");
    if (body) {
      body += 4;
      printf("Received POST body: %s\n", body);
    }
    const char *response = "HTTP/1.1 200 OK\r\n"
                           "Content-Type: text/plain\r\n"
                           "Content-Length: 17\r\n"
                           "Connection: close\r\n"
                           "\r\n"
                           "POST received OK\n";
    send(client_fd, response, strlen(response), 0);
  } else {
    const char *not_impl = "HTTP/1.1 501 Not Implemented\r\n"
                           "Connection: close\r\n\r\n";
    send(client_fd, not_impl, strlen(not_impl), 0);
  }
}

static int create_server_socket(void) {
  int server_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (server_fd < 0) {
    perror("Socket creation failed");
    return -1;
  }

  int opt = 1;
  setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

  struct sockaddr_in address;
  address.sin_family = AF_INET;
  address.sin_port = htons(PORT);
  address.sin_addr.s_addr = INADDR_ANY;

  if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
    perror("Bind failed");
    close(server_fd);
    return -1;
  }

  if (listen(server_fd, 10) < 0) {
    perror("Listen failed");
    close(server_fd);
    return -1;
  }

  if (set_nonblocking(server_fd) < 0) {
    perror("Failed to set non-blocking mode");
    close(server_fd);
    return -1;
  }

  return server_fd;
}

static void accept_new_connection(int epoll_fd, int server_fd) {
  int client_fd = accept(server_fd, NULL, NULL);
  if (client_fd < 0) {
    perror("Accept failed");
    return;
  }

  if (set_nonblocking(client_fd) < 0) {
    perror("Failed to set client socket non-blocking");
    close(client_fd);
    return;
  }

  struct epoll_event client_ev;
  client_ev.events = EPOLLIN;
  client_ev.data.fd = client_fd;

  if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &client_ev) < 0) {
    perror("Failed to add client socket to epoll");
    close(client_fd);
    return;
  }

  printf("New client connected: %d\n", client_fd);
}

static void handle_client_event(int epoll_fd, int client_fd) {
  char buffer[REQUEST_BUF_SIZE] = {0};
  ssize_t bytes_read = recv(client_fd, buffer, sizeof(buffer) - 1, 0);

  if (bytes_read <= 0) {
    printf("Client disconnected: %d\n", client_fd);
    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_fd, NULL);
    close(client_fd);
    return;
  }

  epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_fd, NULL);

  int flags = fcntl(client_fd, F_GETFL, 0);
  if (flags != -1) {
    fcntl(client_fd, F_SETFL, flags & ~O_NONBLOCK);
  }

  if (enqueue_task(client_fd, buffer) < 0) {
    const char *unavailable = "HTTP/1.1 503 Service Unavailable\r\n"
                              "Content-Length: 24\r\n"
                              "Connection: close\r\n"
                              "\r\n"
                              "503 Service Unavailable";
    send(client_fd, unavailable, strlen(unavailable), 0);
    close(client_fd);
  }
}

void *worker_thread(void *arg) {
  (void)arg;
  while (1) {
    pthread_mutex_lock(&queue_mutex);

    while (task_count == 0) {
      pthread_cond_wait(&queue_cond, &queue_mutex);
    }

    task_t *task = task_queue[0];
    for (int i = 1; i < task_count; i++) {
      task_queue[i - 1] = task_queue[i];
    }
    task_count--;

    pthread_mutex_unlock(&queue_mutex);

    handle_client(task->client_fd, task->request);
    close(task->client_fd);
    free(task);
  }
  return NULL;
}

int main(void) {
  int server_fd = create_server_socket();
  if (server_fd < 0) {
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

  if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &ev) == -1) {
    perror("Epoll ctl failed");
    close(server_fd);
    close(epoll_fd);
    return EXIT_FAILURE;
  }

  pthread_t workers[NUM_WORKERS];
  for (int i = 0; i < NUM_WORKERS; i++) {
    if (pthread_create(&workers[i], NULL, worker_thread, NULL) != 0) {
      perror("Failed to create worker thread");
      close(server_fd);
      close(epoll_fd);
      return EXIT_FAILURE;
    }
  }

  struct epoll_event events[MAX_EVENTS];

  while (1) {
    int nfds = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
    if (nfds < 0) {
      perror("Epoll_wait failed");
      break;
    }

    for (int i = 0; i < nfds; i++) {
      if (events[i].data.fd == server_fd) {
        accept_new_connection(epoll_fd, server_fd);
      } else {
        handle_client_event(epoll_fd, events[i].data.fd);
      }
    }
  }

  close(server_fd);
  return EXIT_SUCCESS;
}
