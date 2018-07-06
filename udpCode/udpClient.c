// Client side - UDP Code				    
// By Hugh Smith	4/1/2017		

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

#include "networks.h"

#define MAXBUF 80
#define xstr(a) str(a)
#define str(a) #a

void talkToServer(int socketNum, struct sockaddr_in6 server);
int getData(char * buffer);
int checkArgs(int argc, char * argv[]);


int main (int argc, char *argv[])
 {
	int socketNum = 0;				
	struct sockaddr_in6 server;		// Supports 4 and 6 but requires IPv6 struct
	int portNumber = 0;
	
	portNumber = checkArgs(argc, argv);
	
	socketNum = setupUdpClientToServer(&server, argv[1], portNumber);
	
	talkToServer(socketNum, server);
	
	close(socketNum);
}

void talkToServer(int socketNum, struct sockaddr_in6 server)
{
	int serverAddrLen = sizeof(server);
	int dataLen = 0; 
	char buffer[MAXBUF+1];
	char c = 0;
	
	buffer[0] = '\0';
	while (buffer[0] != '.')
	{
		dataLen = getData(buffer);

		printf("Sending: %s with len: %d\n", buffer,dataLen);
	
		safeSendto(socketNum, buffer, dataLen, 0, (struct sockaddr *) &server, sizeof(server));
		
		safeRecvfrom(socketNum, buffer, MAXBUF, 0, (struct sockaddr *) &server, &serverAddrLen);
		
		printf("Returned from server %s\n", buffer);
	      
	}
}

int getData(char * buffer)
{
	// Read in the data
	buffer[0] = '\0';
	printf("Enter the data to send: ");
	scanf("%" xstr(MAXBUF) "[^\n]%*[^\n]", buffer);
	getc(stdin);  // eat the \n
		
	return (strlen(buffer)+ 1);
}

int checkArgs(int argc, char * argv[])
{
	/* check command line arguments  */
	if (argc != 3)
	{
		printf("usage: %s host-name port-number \n", argv[0]);
		exit(1);
	}
	
	// Checks args and returns port number
	int portNumber = 0;

	if (argc != 3)
	{
		fprintf(stderr, "Usage %s [optional port number]\n", argv[0]);
		exit(-1);
	}
	
	portNumber = atoi(argv[2]);
		
	return portNumber;
}





