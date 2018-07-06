
// Hugh Smith April 2017
// Network code to support TCP/UDP client and server connections

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <sys/time.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

#include "cpe464.h"
#include "networks.h"
#include "gethostbyname.h"

int safeRecvfrom(int socketNum, void * buf, int len, int flags, struct sockaddr *srcAddr, int * addrLen)
{
	int returnValue = 0;
	if ((returnValue = recvfrom(socketNum, buf, (size_t) len, flags, srcAddr, (socklen_t *) addrLen)) < 0)
	{
		perror("recvfrom: ");
		exit(-1);
	}
	
	return returnValue;
}

int safeSendto(int socketNum, void * buf, int len, int flags, struct sockaddr *srcAddr, int addrLen)
{
	int returnValue = 0;
	if ((returnValue = sendto(socketNum, buf, (size_t) len, flags, srcAddr, (socklen_t) addrLen)) < 0)
	{
		perror("sendto: ");
		exit(-1);
	}

	return returnValue;
}

int safeRecv(int socketNum, void * buf, int len, int flags)
{
	int returnValue = 0;
	if ((returnValue = recv(socketNum, buf, (size_t) len, flags)) < 0)
	{
		perror("recv: ");
		exit(-1);
	}
	
	return returnValue;
}

int safeSend(int socketNum, void * buf, int len, int flags)
{
	int returnValue = 0;
	if ((returnValue = send(socketNum, buf, (size_t) len, flags)) < 0)
	{
		perror("send: ");
		exit(-1);
	}
	
	return returnValue;
}

/* Select function used for looking for packets that are not setup packets */
int safeSelect(int socketNum, int seconds, int *triesLeft)
{
   fd_set sockets;
   struct timeval timeout;
   struct timeval *timeoutPtr = NULL;
   /* There must be tries left, otherwise the process will end */
   if (*triesLeft <= 0)
   {
      return TRIES_FINISHED;
   }
   (*triesLeft)--;
   /* Set the amount of time to wait for a packet */
   if (seconds >= 0)
   {
      timeout.tv_sec = seconds;
      timeout.tv_usec = 0;
      timeoutPtr = &timeout;
   }
   /* Reset all the sockets */
   FD_ZERO(&sockets);
   FD_SET(socketNum, &sockets);
   /* Wait for packets to arrive */
   if (select(socketNum + 1, &sockets, NULL, NULL, timeoutPtr) < 0)
   {
      perror("select");
      exit(-1);
   }
   /* Make sure the proper socket is being used */
   if (FD_ISSET(socketNum, &sockets))
   {
      return DATA_READY;
   }
   else
   {
      return DATA_NOT_READY;
   }
}

/* Receives a packet and makes sure it is valid */
int32_t receivePacket(int socketNum, uint8_t *buf, struct sockaddr *srcAddr, int length)
{
   Header header;
   int messageLen = 0;
   int addrLen = sizeof(struct sockaddr_in6);
   /* Grab the whole packet based on the length specified in the header */
   if ((messageLen = safeRecvfrom(socketNum, buf, length, 0, srcAddr, &addrLen)) == 0)
   {
      fprintf(stderr, "No message! Exiting... \n");
      exit(-1);
   }
   /* Verify checksum, otherwise return 0, like no packet was received */
   if (in_cksum((unsigned short *) buf, messageLen) != 0)
   {
      return 0;
   }
   /* Convert the header back to host order and paste it back into the packet */
   memcpy(&header, buf, sizeof(Header));
   header.sequence = ntohl(header.sequence);
   memcpy(buf, &header, sizeof(Header));
   
   return messageLen;
}

/* Create a header with the given flag and length of packet */
Header createHeader(uint32_t sequence, uint8_t flag, uint16_t length)
{
   Header header;
   header.sequence = htonl(sequence);
   header.checksum = 0;
   header.length = length;
   header.flag = flag;
   return header;
}

/* Send a packet with only a header with the given flag */
ssize_t sendHeader(int socketNum, uint32_t sequence, uint8_t flag, struct sockaddr *srcAddr, int addrLen)
{
   char sendBuf[MAX_BUF];
   
   Header header = createHeader(sequence, flag, sizeof(Header));
   memcpy(sendBuf, &header, sizeof(Header));
   
   uint16_t checksum = in_cksum((unsigned short *) sendBuf, sizeof(Header));
   header.checksum = checksum;
   memcpy(sendBuf, &header, sizeof(Header));
      
   /* Send the packet */
   return safeSendto(socketNum, sendBuf, sizeof(Header), 0, srcAddr, addrLen);
}

/* Send a packet that includes a data buffer after the header */
ssize_t sendPacket(int socketNum, uint32_t sequence, uint8_t flag, struct sockaddr *srcAddr, uint8_t *buf, uint16_t length)
{
   char sendBuf[MAX_BUF];
   uint8_t *bufPtr = sendBuf;
   /* Prepare the header */
   Header header = createHeader(sequence, flag, sizeof(Header) + length);
   memcpy(bufPtr, &header, sizeof(Header));
   bufPtr += sizeof(Header);
   /* Prepare the data buffer */
   memcpy(bufPtr, buf, length);
   /* Calculate the checksum */
   int len = sizeof(Header) + length;
   uint16_t checksum = in_cksum((unsigned short *) sendBuf, len);
   header.checksum = checksum;
   memcpy(sendBuf, &header, sizeof(Header));
   /* Send the packet */
   return safeSendto(socketNum, sendBuf, len, 0, srcAddr, sizeof(struct sockaddr_in6));
}

// This function sets the server socket. The function returns the server
// socket number and prints the port number to the screen.  
int tcpServerSetup(int portNumber)
{
	int server_socket= 0;
	struct sockaddr_in6 server;     
	socklen_t len= sizeof(server);  

	server_socket= socket(AF_INET6, SOCK_STREAM, 0);
	if(server_socket < 0)
	{
		perror("socket call");
		exit(1);
	}

	server.sin6_family= AF_INET6;         		
	server.sin6_addr = in6addr_any;   
	server.sin6_port= htons(portNumber);         

	// bind the name (address) to a port 
	if (bind(server_socket, (struct sockaddr *) &server, sizeof(server)) < 0)
	{
		perror("bind call");
		exit(-1);
	}
	
	// get the port name and print it out
	if (getsockname(server_socket, (struct sockaddr*)&server, &len) < 0)
	{
		perror("getsockname call");
		exit(-1);
	}

	if (listen(server_socket, BACKLOG) < 0)
	{
		perror("listen call");
		exit(-1);
	}
	
	printf("Server Port Number %d \n", ntohs(server.sin6_port));
	
	return server_socket;
}

// This function waits for a client to ask for services.  It returns
// the client socket number.   

int tcpAccept(int server_socket, int debugFlag)
{
	struct sockaddr_in6 clientInfo;   
	int clientInfoSize = sizeof(clientInfo);
	int client_socket= 0;

	if ((client_socket = accept(server_socket, (struct sockaddr*) &clientInfo, (socklen_t *) &clientInfoSize)) < 0)
	{
		perror("accept call");
		exit(-1);
	}
	  
	if (debugFlag)
	{
		printf("Client accepted.  Client IP: %s Client Port Number: %d\n",  
				getIPAddressString6(clientInfo.sin6_addr.s6_addr), ntohs(clientInfo.sin6_port));
	}
	

	return(client_socket);
}

int tcpClientSetup(char * serverName, char * port, int debugFlag)
{
	// This is used by the client to connect to a server using TCP
	
	int socket_num;
	uint8_t * ipAddress = NULL;
	struct sockaddr_in6 server;      
	
	// create the socket
	if ((socket_num = socket(AF_INET6, SOCK_STREAM, 0)) < 0)
	{
		perror("socket call");
		exit(-1);
	}

	// setup the server structure
	server.sin6_family = AF_INET6;
	server.sin6_port = htons(atoi(port));
	
	// get the address of the server 
	if ((ipAddress = gethostbyname6(serverName, &server)) == NULL)
	{
		exit(-1);
	}

	if(connect(socket_num, (struct sockaddr*)&server, sizeof(server)) < 0)
	{
		perror("connect call");
		exit(-1);
	}

	if (debugFlag)
	{
		printf("Connected to %s IP: %s Port Number: %d\n", serverName, getIPAddressString6(ipAddress), atoi(port));
	}
	
	return socket_num;
}

int udpServerSetup(int portNumber)
{
	struct sockaddr_in6 server;
	int socketNum = 0;
	int serverAddrLen = 0;	
	
	// create the socket
	if ((socketNum = socket(AF_INET6,SOCK_DGRAM,0)) < 0)
	{
		perror("socket() call error");
		exit(-1);
	}
	
	// set up the socket
	server.sin6_family = AF_INET6;    		// internet (IPv6 or IPv4) family
	server.sin6_addr = in6addr_any ;  		// use any local IP address
	server.sin6_port = htons(portNumber);   // if 0 = os picks 

	// bind the name (address) to a port
	if (bind(socketNum,(struct sockaddr *) &server, sizeof(server)) < 0)
	{
		perror("bind() call error");
		exit(-1);
	}

	/* Get the port number */
	serverAddrLen = sizeof(server);
	getsockname(socketNum,(struct sockaddr *) &server,  &serverAddrLen);
	printf("Server using Port #: %d\n", ntohs(server.sin6_port));

	return socketNum;	
	
}

int setupUdpClientToServer(struct sockaddr_in6 *server, char * hostName, int portNumber)
{
	// currently only setup for IPv4 
	int socketNum = 0;
	char ipString[INET6_ADDRSTRLEN];
	uint8_t * ipAddress = NULL;
	
	// create the socket
	if ((socketNum = socket(AF_INET6, SOCK_DGRAM, 0)) < 0)
	{
		perror("socket() call error");
		exit(-1);
	}
  	 	
	if ((ipAddress = gethostbyname6(hostName, server)) == NULL)
	{
		exit(-1);
	}
	
	server->sin6_port = ntohs(portNumber);
	server->sin6_family = AF_INET6;	
	
	inet_ntop(AF_INET6, ipAddress, ipString, sizeof(ipString));
	printf("Server info - IP: %s Port: %d \n", ipString, portNumber);
		
	return socketNum;
}
