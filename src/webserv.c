//==============================================================================
//==============================================================================
// Michael Gerakis
//
// A web server written in C. Creates sockets bound to local IP and listens for
// requests from clients. Requests are parsed and then, depending on the request
// executables are executed/scripts are sent.
//
//==============================================================================
//==============================================================================


//----------------------------------------------
//------- Includes -----------------------------
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <stdio.h>       // fprintf, printf
#include <string.h>      // memset
#include <unistd.h>      // close
#include <signal.h>
#include <errno.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>     // open
#include <sys/stat.h>
#include <sys/uio.h>
#include <dirent.h>
//----------------------------------------------

//----------------------------------------------
//-------- Defines -----------------------------
#define PATH_MAX 4096
#define REQ_SIZE 516
#define PARAM_BUFSIZE 256
//----------------------------------------------

//----------------------------------------------
//------- Prototypes ---------------------------
void  sigChildHandler(int);
void* get_in_addr(struct sockaddr *);
int   openSocket(struct addrinfo *);
void  parseRequest(char*, int);
int   read_filename(const char*, char*, char*);
char* read_parameter(const char*, char*, int*);
void  send_status(int, char*, int);
void  dir_list(int, char*);
//----------------------------------------------


//----------------------------------------------
//------- Main ---------------------------------
int
main(int argc, char *argv[]) {
  int sockfd, client_fd;
  int status;
  struct addrinfo hints;
  struct addrinfo *servinfo;
  struct sigaction sa;
  char *req;

  req = (char *) malloc(REQ_SIZE * sizeof (char));

  if (argc != 2) {
    fprintf(stderr, "Usage: server port number\n");
    return 1;
  }

  // Setup address information
  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_UNSPEC;       // both IPv4 and IPv6 work
  hints.ai_socktype = SOCK_STREAM;   // TCP stream sockets
  hints.ai_flags = AI_PASSIVE;       // fill in my IP for me
  if ((status = getaddrinfo(NULL, argv[1], &hints, &servinfo)) != 0) {
    fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(status));
    return 1;
  }

  sockfd = openSocket(servinfo);
  freeaddrinfo(servinfo);

  if (sockfd == -1) {
    fprintf(stderr, "Server: Failed to bind.\n");
    return 1;
  }

  if (listen(sockfd, 5) == -1) {
    perror("Server: listen()");
    close(sockfd);
    return 1;
  }

  // reap all dead processes
  sa.sa_handler = sigChildHandler;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = SA_RESTART;
  if (sigaction(SIGCHLD, &sa, NULL) == -1) {
    perror("Server: sigaction()");
    close(sockfd);
    return 1;
  }

  printf("Server: Waiting for connections...\n");

  while(1) {
    client_fd = accept(sockfd, NULL, NULL);
    if (client_fd == -1) {
      perror("Server: accept()");
      continue;
    }

    if (!fork()) {
      close(sockfd);
      if (recv(client_fd, req, REQ_SIZE, 0) == -1)
        perror("Server: recv");
      else {
        printf("\n");
        parseRequest(req, client_fd);
        printf("\n");
      }
      close(client_fd);
      exit(0);
    }
    close(client_fd);
  }

  return 0;
}


void
parseRequest(char *req, int clientfd) {
  struct stat info;
  char filename[PATH_MAX];
  char contentType[6];
  char *parameter;
  int has_parameters = 0;
  int len = 0;  // send whole file
  char *parameters[7];
  int i;

  for( i = 0; i < 7; i++)
    parameters[i] = NULL;

  if (read_filename(req, filename, contentType) == -1) {
    send_status(501, contentType, clientfd);
    return;
  }

  if (req[(3+strlen(filename))] == '?') {
    parameters[0] = filename;
    int offset = (4 + strlen(filename));
    i = 1;

    while(i < 7) {
      parameter = read_parameter(req, filename, &offset);
      if (strlen(parameter) == 0) {  // No more parameters left to process
        break;
      }
      parameters[i] = parameter;
      offset += (strlen(parameter));
      i++;
    }

    has_parameters = 1;
  }

  printf("Filename: %s\n", filename);
  printf("Content Type: %s\n", contentType);
  for (i = 0; parameters[i] != NULL; i++) {
    printf("Parameter %d is %s\n", i, parameters[i]);
  }

  if (stat(filename, &info) == 0) {
    if (S_ISREG(info.st_mode)) {
      if (info.st_mode & S_IXUSR) {
        /* executable */
        printf("Found an executable file.\n");
        send_status(200, contentType, clientfd);
        dup2(clientfd, 1);
        if (has_parameters == 1) {
          execv(filename+2, parameters);
        } else {
          execl(filename+2, filename, NULL);
        }
        perror("Server: execv");
      } else {
        /* non-executable */
        printf("Found a non-executable file.\n");
        int fd;
        fd = open(filename, O_RDONLY);
        send_status(200, contentType, clientfd);
        if (sendfile(fd, clientfd, 0, &len, NULL, 0) == -1)
          perror("Server: sendfile");
        close(fd);
        return;
      }
    }
    if (S_ISDIR(info.st_mode)) {
      send_status(200, "html", clientfd);
      dir_list(clientfd, filename);
    }
  } else {
    printf("File %s not found.\n", filename);
    send_status(404, contentType, clientfd);
    return;
  }
}


int
openSocket(struct addrinfo *servinfo) {
  int sockfd;
  struct addrinfo *p;
  // To make sure sockets are reusable without having to wait several minutes
  int check = 1;

  // Search addresses for valid entries
  for (p = servinfo; p != NULL; p = p->ai_next) {
    if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
      perror("Server: socket");
      continue;
    }


    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &check, sizeof(int)) == -1) {
      perror("Server: setsockopt");
      return -1;
    }

    if((bind(sockfd, p->ai_addr, p->ai_addrlen) == -1)) {
      close(sockfd);
      perror("Server: bind");
      continue;
    }

  return sockfd;
  }

  printf("No addresses with valid entries.\n");
  return -1;
}


void
send_status(int status, char* contentType, int clientfd) {
  char response[512];
  char *status_message = (char *)malloc(sizeof(char) * 30);

  if (status == 200) {
    char *content_type   = (char *)malloc(sizeof(char) * 30);
    status_message = "OK\0";

    if (strcmp("jpg", contentType) == 0 || strcmp("jpeg", contentType) == 0)
      content_type = "image/jpeg\n\0";
    else if (strcmp("gif", contentType) == 0)
      content_type = "image/gif\n\0";
    else if (strcmp("html", contentType) == 0)
      content_type = "text/html\n\0";
    else if (strcmp("css", contentType) == 0)
      content_type = "text/css\n\0";
    else
      content_type = "text/plain\n\0";

    sprintf(response, "HTTP/1.1 %d %s\nContent-type: %s\n", status,
                                                            status_message,
                                                            content_type);
  } else {
    char *responseBody = (char *)malloc(sizeof(char)*100);

    if (status == 404) {
      status_message = "Not Found\0";
      responseBody = "<html><head><title>Error</title></head><body><h1>404 Error: Not Found</body></html>\0";
    }
    if (status == 501) {
      status_message = "Not Implemented\0";
      responseBody = "<html><head><title>Error</title></head><body><h1>501 Error: Not Implemented</body></html>\0";
    }

    sprintf(response, "HTTP/1.1 %d %s\n\n%s", status, status_message,
                                              responseBody);
  }

  send(clientfd, response, strlen(response), 0);
  return;
}


void dir_list(int clientfd, char* path) {
  struct dirent *dirent;
  DIR *dir = opendir(path);

  if (dir == 0) {
    perror("Server: dir");
    return;
  }

  char head[] = "<html><head><title>DirectoryListing</title></head><body><ul>";
  send(clientfd, head, strlen(head), 0);

  while((dirent = readdir(dir)) != 0) {
    char list[] = "<li>";
    send(clientfd, list, strlen(list), 0);
    send(clientfd, dirent->d_name, strlen(dirent->d_name), 0);
    char listEnd[] = "</li>";
    send(clientfd, listEnd, strlen(listEnd), 0);
  }

  char headEnd[] = "</ul></body></html>";
  send(clientfd, headEnd, strlen(headEnd), 0);
}


int
read_filename(const char *req, char *filename, char *contentType) {
  if (req[0] != 'G' || req[1] != 'E' || req[2] != 'T' || req[3] != ' ')
    return -1;

  int i;
  int fposition = 1;
  int cposition = 0;  // content position
  int typeCheck = 0;  // checks if . has been processed
  filename[0] = '.';

  for (i = 4; req[i] != ' '; i++) {
    if (req[i] == '?') {   // rest of the req contains parameters
      break;
    }

    filename[fposition] = req[i];
    fposition++;

    if (typeCheck) {
      contentType[cposition] = req[i];
      cposition++;
    }

    if (req[i] == '.')
      typeCheck = 1;
  }

  filename[fposition] = '\0';
  contentType[cposition] = '\0';

  return 0;
}

char *
read_parameter(const char* req, char* filename, int* offset) {
  int bufsize = PARAM_BUFSIZE;
  int position = 0;
  char *buffer = (char *) malloc(sizeof(char) * bufsize);

  int i;
  int vCheck = 0;   // True if we are on a value, false if on key

  for (i = *offset; req[i] != ' '; i++) {
    if (req[i] == '&') {  // Only return one parameter
      *offset = *offset + 2;
      break;
    } else if (vCheck) {
      buffer[position] = req[i];
      position++;

      if (position >= bufsize) {
        bufsize += PARAM_BUFSIZE;
        buffer = (char *) realloc(buffer, bufsize);
        if (!buffer) {
          fprintf(stderr, "Server: allocation error\n");
          exit(EXIT_FAILURE);
        }
      }
    } else if (req[i] == '=') {
      vCheck = 1;
    } else
      *offset = *offset + 1;
  }

  buffer[position] = '\0';
  return buffer;
}


void *get_in_addr(struct sockaddr *sa) {
  if (sa->sa_family == AF_INET) {
    return &(((struct sockaddr_in*)sa)->sin_addr);
  }

  return &(((struct sockaddr_in6*)sa)->sin6_addr);
}


void
sigChildHandler(int s) {
  int saved_errno = errno;

  while(waitpid(-1, NULL, WNOHANG) > 0);
  errno = saved_errno;
}
