/*
# This is a simple linux command line utility to execute rcon commands
# Just change the YOUR_PASSWORD_HERE to your rcon password (unless
# you want to enter it every time) and possibly change the default
# IP address from 127.0.0.1 (localhost)
#
# once downloaded on your linux system, compile it with:
#
#   gcc -o rcon rcon.c
#
# note, it should work on non-linux too, but may require changing the 
# socket stuff (i.e. windows will definitely need to add the winsock
# initialization line)
#
# written by [ASY]Zyrain
#
*/

#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define DEBUG 0

#define SERVERDATA_EXECCOMMAND 2
#define SERVERDATA_AUTH 3
#define SERVERDATA_RESPONSE_VALUE 0
#define SERVERDATA_AUTH_RESPONSE 2

int send_rcon(int sock, int id, int command, char *string1, char *string2) {
  int size, ret;
  size = 10+strlen(string1)+strlen(string2);
 
  ret = send(sock,&size,sizeof(int),0);
  if(ret == -1) {
    perror("send() failed:");
    return -1;
  }
  ret = send(sock,&id,sizeof(int),0);
  if(ret == -1) {
    perror("send() failed:");
    return -1;
  }
  ret = send(sock,&command,sizeof(int),0);
  if(ret == -1) {
    perror("send() failed:");
    return -1;
  }
  ret = send(sock,string1,strlen(string1)+1,0);
  if(ret == -1) {
    perror("send() failed:");
    return -1;
  }
  ret = send(sock,string2,strlen(string2)+1,0);
  if(ret == -1) {
    perror("send() failed:");
    return -1;
  }
  if(DEBUG) printf("Sent %d bytes\n",size+4);
  return 0;
}
 
int recv_rcon(int sock, int timeout, int *id, int *command, char *string1,
       char *string2) {
  struct timeval tv;
  fd_set readfds;
  int size;
  char *ptr;
  int ret;
  char buf[8192];
  int total_received = 0;
  int received_large_packet = 0; // 新增变量，用于标记是否收到过大于1400字节的包

  size=0xDEADBEEF;
  *id=0xDEADBEEF;
  *command=0xDEADBEEF;
  string1[0]=0;
  string2[0]=0;

  tv.tv_sec = timeout;
  tv.tv_usec = 0;

  FD_ZERO(&readfds);
  FD_SET(sock, &readfds);

  /* don't care about writefds and exceptfds: */
  select(sock+1, &readfds, NULL, NULL, &tv);

  if (!FD_ISSET(sock, &readfds)) {
    if(DEBUG) { 
      printf("recv timeout\n");
    }
    return -1; // timeout
  }
  if(DEBUG) printf("Got a response\n");
  ret = recv(sock, &size, sizeof(int), 0);
  if(ret == -1) {
    perror("recv() failed:");
    return -1;
  }
  if((size<10) || (size>8192)) {
    printf("Illegal size %d\n",size);
    exit(-1);
  }
  ret = recv(sock, id, sizeof(int),0);
  if(ret == -1) {
    perror("recv() failed:");
    return -1;
  }
  size-=ret;
  ret = recv(sock, command, sizeof(int),0);
  if(ret == -1) {
    perror("recv() failed:");
    return -1;
  }
  size-=ret;

  ptr = buf;
  while(size) {
    ret = recv(sock, ptr, size, 0);
    if(ret == -1) {
      perror("recv() failed:");
      return -1;
    }
    size -= ret; 
    ptr += ret;
    total_received += ret;

    // Check if we need to continue receiving data
    if (total_received >= 1400) {
      received_large_packet = 1; // 标记已收到大于1400字节的包
      // Reset the timeout for the next select call
      tv.tv_sec = 0;
      tv.tv_usec = 20000; // 0.05 seconds

      FD_ZERO(&readfds);
      FD_SET(sock, &readfds);

      select(sock+1, &readfds, NULL, NULL, &tv);

      if (!FD_ISSET(sock, &readfds)) {
        break; // No more data to receive
      }
    } else if (received_large_packet) {
      // 如果已经收到过大于1400字节的包，再收到小于1400字节的包则终止收包并退出程序
      // printf("Received packet size less than 1400 bytes after a large packet, terminating...\n");
      exit(-1);
    }
  }
  buf[8190] = 0;
  buf[8191] = 0;

  strncpy(string1, buf, 4095);
  string1[4095] = 0;
  strncpy(string2, buf+strlen(string1)+1, 4095);
  
  return 0;
}

/* This is set to 1 when we've been authorized */
int auth = 0;
char string1[4096];
char string2[4096];
 
int process_response(int sock) {
  int ret;
  int id;
  int command;
 
  ret=recv_rcon(sock, 1, &id, &command, string1, string2);
  if(DEBUG) printf("Received = %d : id=%d, command=%d, s1=%s, s2=%s\n",
    ret, id, command, string1, string2);
  if(ret==-1) {
    return -1;
  }
  
  switch(command) {
  case SERVERDATA_AUTH_RESPONSE:
    switch(id) {
    case 20: 
      auth = 1;
      break;
    case -1:
      printf("Password Refused\n");
      return -1;
    default:
      printf("Bad Auth Response ID = %d\n",id);
      exit(-1);
    };
    break;
  case SERVERDATA_RESPONSE_VALUE:
    printf("%s",string1);
    break;
  default:
    if (!auth) {
      printf("Unexpected command: %d\n", command);
    }
    break;
  };
}

int connect_with_retry(const char *address, short port, int max_retries) {
  struct sockaddr_in a;
  int sock;
  int ret;
  struct timespec ts;

  for (int attempt = 0; attempt < max_retries; attempt++) {
    sock = socket(AF_INET, SOCK_STREAM, 0); // TCP socket
    if (sock == -1) {
      perror("socket() failed");
      continue;
    }

    a.sin_family = AF_INET;
    a.sin_addr.s_addr = inet_addr(address);
    a.sin_port = htons(port);

    ret = connect(sock, (struct sockaddr *)&a, sizeof(a));
    if (ret == 0) {
      return sock; // Connection successful
    }

    close(sock);
    clock_gettime(CLOCK_REALTIME, &ts);
    printf("Connection failed at %ld.%03ld, retrying (%d/%d)...\n", ts.tv_sec, ts.tv_nsec / 1000000, attempt + 1, max_retries);
    sleep(1); // Wait before retrying
  }

  return -1; // All retries failed
}

int main(int argc, char **argv)
{
  int sock;
  int ret, i;
  char password[512]="YOUR_PASSWORD_HERE";
  short port = 21114;
  char address[512] = "127.0.0.1";

  int arg;

  auth = 0;

  if(argc<2)
    {
      printf("Syntax: rcon [-P\"rcon_password\"] [-a127.0.0.1] [-p21114] command\n");
      printf("be based on https://github.com/zyrain/rcon\n");
      printf("Bugle Call To Charge Community [BCTC]Teddyou Second Optimization and Modification\n");
      printf("This program is mainly aimed at Squad Rcon communication\n");
      return 0;
    }
  
  for(arg = 1;arg<argc;arg++) {
    if(argv[arg][0] != '-')
      break; /* done with args */
    switch(argv[arg][1]) {
    case 'a':
      strncpy(address, argv[arg]+2, 512);
      break;
    case 'p':
      port = atoi(argv[arg]+2);
      break;
    case 'P':
      strncpy(password, argv[arg]+2, 512);
      break;
    default:
      fprintf(stderr, "Unknown option -%c\n",argv[arg][1]);
      return 0;
    }
  }
  
  sock = connect_with_retry(address, port, 5);
  if (sock == -1) {
    printf("Failed to connect after 5 retries\n");
    return -1;
  }

  if(DEBUG) printf("Connected to Server\n");
  
  if(DEBUG) printf("Sending RCON Password\n");
  ret=send_rcon(sock, 20, SERVERDATA_AUTH, password, "");
 
  if(ret == -1) {
    perror("Sending password");
    return -1;
  };
 
  while(auth==0) {
    if(process_response(sock)==-1) {
      printf("Couldn't Authenticate\n");
      close(sock);
      sock = connect_with_retry(address, port, 5);
      if (sock == -1) {
        printf("Failed to reconnect after 5 retries\n");
        return -1;
      }
      if(DEBUG) printf("Reconnected to Server\n");
      if(DEBUG) printf("Sending RCON Password\n");
      ret=send_rcon(sock, 20, SERVERDATA_AUTH, password, "");
      if(ret == -1) {
        perror("Sending password");
        return -1;
      };
    }
  }
 
  if(DEBUG) printf("Password Accepted\n");
  /* Now we're authorized, send command */
 
  /* built command */
  ret = 0;
  while(arg < argc) {
    if(strlen(argv[arg]) + ret < 4096) {
      strcpy(string1+ret, argv[arg]);
      ret += strlen(argv[arg]);
      string1[ret] = ' ';
      ret++;
      arg++;
    } else {
      fprintf(stderr, "cmd too long to send\n");
      return -1;
    }
  }
  //  string1[ret] = '\n';
  //ret++;
  ret--;
  string1[ret]=0;
 
  if(DEBUG) printf("Sending Command: \"%s\"\n", string1);
 
  ret=send_rcon(sock, 20, SERVERDATA_EXECCOMMAND, string1, "");
 
  if(ret == -1) {
    perror("cmd send");
    return -1;
  }
 
  // process responses until a timeout
  while(process_response(sock) != -1);
  
  close(sock);
  return 0;
}
