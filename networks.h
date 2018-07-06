
// 	Writen - HMS April 2017
//  Supports TCP and UDP - both client and server


#ifndef __NETWORKS_H__
#define __NETWORKS_H__

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <pthread.h>
#include <errno.h>

#define BACKLOG 10
#define MAX_BUF 1500
#define MAX_DATA_BUF 1400
#define MAX_THREADS 1000

#define TEN_SECONDS 10

#define SEND_CONNECTION 0
#define SEND_FILENAME 1
#define WAIT_ON_FILENAME_RESPONSE 2
#define GET_DATA 3
#define DONE 4
#define WAIT_ON_CONNECTION 5
#define GET_CONNECTION 6
#define GET_FILENAME_RESPONSE 7

#define PROCESS_SETUP 8
#define SEND_SETUP_RESPONSE 9
#define WAIT_ON_FILENAME 10
#define GET_FILENAME 11
#define SEND_DATA 12

#define WAIT_FOR_PACKET 13
#define GET_PACKET 14
#define SEND_PACKET 15
#define SEND_FILENAME_RESPONSE 16
#define PROCESS_DATA 17
#define WAIT_ON_DATA 18
#define RESEND_RR 19

#define PREPARE_DATA 20
#define WAIT_FOR_ACK 21
#define CHECK_FOR_ACK 22
#define PROCESS_ACK 23

#define DATA_READY 0
#define DATA_NOT_READY 1
#define TRIES_FINISHED 2

#define FLAG_1_SETUP 1
#define FLAG_2_SETUP 2
#define FLAG_3_DATA 3
#define FLAG_5_RR 5
#define FLAG_6_SREJ 6
#define FLAG_7_FILENAME 7
#define FLAG_8_BAD_FILENAME 8
#define FLAG_9_END_CONNECTION 9
#define FLAG_10_FINAL_DATA 10

#define TRUE 1
#define FALSE 0

typedef struct __attribute__ ((__packed__)) header {
   uint32_t sequence;
   uint16_t checksum;
   uint8_t flag;
   uint16_t length;
} Header;

typedef struct __attribute__((__packed__)) connection {
   int32_t socketNum;
   struct sockaddr_in6 remote;
   uint32_t len;
} Connection;

typedef struct __attribute__((__packed__)) packet {
   uint8_t buf[MAX_BUF];
   uint32_t sequence;
   uint8_t isSREJ;
   Header header;
} Packet;

int safeRecv(int socketNum, void * buf, int len, int flags);
int safeSend(int socketNum, void * buf, int len, int flags);
int safeRecvfrom(int socketNum, void * buf, int len, int flags, struct sockaddr *srcAddr, int * addrLen);
int safeSendto(int socketNum, void * buf, int len, int flags, struct sockaddr *srcAddr, int addrLen);

int safeSelect(int socketNum, int seconds, int *triesLeft);

int32_t receivePacket(int socketNum, uint8_t *buf, struct sockaddr *srcAddr, int length);
Header createHeader(uint32_t sequence, uint8_t flag, uint16_t length);
ssize_t sendHeader(int socketNum, uint32_t sequence, uint8_t flag, struct sockaddr *srcAddr, int addrLen);

ssize_t sendPacket(int socketNum, uint32_t sequence, uint8_t flag, struct sockaddr *srcAddr, uint8_t *buf, uint16_t length);

// for the server side
int tcpServerSetup(int portNumber);
int tcpAccept(int server_socket, int debugFlag);
int udpServerSetup(int portNumber);

// for the client side
int tcpClientSetup(char * serverName, char * port, int debugFlag);
int setupUdpClientToServer(struct sockaddr_in6 *server, char * hostName, int portNumber);


#endif
