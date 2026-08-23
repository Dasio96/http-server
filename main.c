#include <fcntl.h>
#include <netinet/in.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/ssl.h>
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
} task_t;

task_t *task_queue[QUEUE_CAPACITY] = {0};
int task_count = 0;
pthread_mutex_t queue_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t queue_cond = PTHREAD_COND_INITIALIZER;

SSL_CTX *ssl_ctx = NULL;

int enqueue_task(int client_fd) {
  task_t *task = malloc(sizeof(task_t));
  if (!task) {
    return -1;
  }
  task->client_fd = client_fd;

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

void serve_file(SSL *ssl, const char *path) {
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
      SSL_write(ssl, forbidden, strlen(forbidden));
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
    SSL_write(ssl, not_found, strlen(not_found));
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
  SSL_write(ssl, header, strlen(header));

  char buffer[1024];
  size_t bytes_read;
  while ((bytes_read = fread(buffer, 1, sizeof(buffer), file)) > 0) {
    SSL_write(ssl, buffer, bytes_read);
  }

  fclose(file);
}

void handle_client(SSL *ssl, const char *buffer) {
  char method[16] = {0};
  char path[256] = {0};
  char protocol[16] = {0};

  if (sscanf(buffer, "%15s %255s %15s", method, path, protocol) < 3) {
    const char *bad_request =
        "HTTP/1.1 400 Bad Request\r\nConnection: close\r\n\r\n";
    SSL_write(ssl, bad_request, strlen(bad_request));
    return;
  }

  printf("Method: %s, Path: %s, Protocol: %s\n", method, path, protocol);

  if (strcmp(method, "GET") == 0) {
    serve_file(ssl, path);
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
    SSL_write(ssl, response, strlen(response));
  } else {
    const char *not_impl = "HTTP/1.1 501 Not Implemented\r\n"
                           "Connection: close\r\n\r\n";
    SSL_write(ssl, not_impl, strlen(not_impl));
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
  epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_fd, NULL);

  if (enqueue_task(client_fd) < 0) {
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

    int client_fd = task->client_fd;
    free(task);

    int flags = fcntl(client_fd, F_GETFL, 0);
    if (flags != -1) {
      fcntl(client_fd, F_SETFL, flags & ~O_NONBLOCK);
    }

    SSL *ssl = SSL_new(ssl_ctx);
    SSL_set_fd(ssl, client_fd);

    if (SSL_accept(ssl) > 0) {
      char buffer[REQUEST_BUF_SIZE] = {0};
      int bytes_read = SSL_read(ssl, buffer, sizeof(buffer) - 1);
      if (bytes_read > 0) {
        handle_client(ssl, buffer);
      }
    } else {
      ERR_print_errors_fp(stderr);
    }

    SSL_shutdown(ssl);
    SSL_free(ssl);
    close(client_fd);
  }
  return NULL;
}

int main(void) {
  SSL_library_init();
  OpenSSL_add_all_algorithms();
  SSL_load_error_strings();

  ssl_ctx = SSL_CTX_new(TLS_server_method());
  if (!ssl_ctx) {
    ERR_print_errors_fp(stderr);
    return EXIT_FAILURE;
  }

  if (SSL_CTX_use_certificate_file(ssl_ctx, "server.crt", SSL_FILETYPE_PEM) <=
          0 ||
      SSL_CTX_use_PrivateKey_file(ssl_ctx, "server.key", SSL_FILETYPE_PEM) <=
          0) {
    ERR_print_errors_fp(stderr);
    return EXIT_FAILURE;
  }

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

  SSL_CTX_free(ssl_ctx);
  close(server_fd);
  return EXIT_SUCCESS;
}
