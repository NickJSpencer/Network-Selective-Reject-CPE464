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

#include "cpe464.h"
#include "networks.h"

/* #include "libcpe464/networks/checksum.h"
 */
#define MAXBUF 80
#define xstr(a) str(a)
#define str(a) #a

void processServer(int socketNum, struct sockaddr_in6 server);

int waitOnData(int socketNum, struct sockaddr_in6 server, int *tries);

int sendSetupPacket(int socketNum, struct sockaddr_in6 server);

int waitOnConnection(int socketNum, struct sockaddr_in6 server, int *tries);
int waitOnFilenameResponse(int socketNum, struct sockaddr_in6 server, int *tries);

int getData(int socketNum, uint8_t *buf, struct sockaddr_in6 server);
int processData(int socketNum, uint8_t *buf, struct sockaddr_in6 server, int32_t *expectedSequence, int *srejSent, Packet *packets);
int processExpectedPacket(int socketNum, struct sockaddr_in6 server, uint8_t *buf, Packet *packets, Header header, int windowSize, int *expectedSequence);
int processOverPacket(int socketNum, struct sockaddr_in6 server, uint8_t *buf, Packet *packets, Header header, int windowSize, int *expectedSequence, int *srejSent);
int processUnderPacket(int socketNum, struct sockaddr_in6 server, int *expectedSequence, int *srejSent, int windowSize, Packet *packets);

int processSetupPacket(int socketNum, struct sockaddr_in6 *server, uint8_t *buf, int *tries);
int processFilenameResponse(int socketNum, struct sockaddr_in6 server, uint8_t *buf, int *tries);
int resendRR(int socketNum, struct sockaddr_in6 server, int *expectedSequence, uint8_t *buf);

int checkArgs(int argc, char * argv[]);

char remoteFile[MAX_BUF];
char localFile[MAX_BUF];
int windowSize;
int bufferSize;
float errorPercent;
char remoteMachine[MAX_BUF];

int outFile;
int srej = 0;
uint32_t sequenceNum = 0;

int main (int argc, char *argv[])
 {
	int socketNum = 0;				
	struct sockaddr_in6 server;		// Supports 4 and 6 but requires IPv6 struct
	int portNumber = 0;

	portNumber = checkArgs(argc, argv);
   
   if ((outFile = open(localFile, O_CREAT | O_TRUNC | O_WRONLY, 0600)) < 0)
   {
      perror("open");
      exit(-1);
   }
      
   sendtoErr_init(errorPercent, DROP_ON, FLIP_ON, DEBUG_OFF, RSEED_ON);
   
	socketNum = setupUdpClientToServer(&server, remoteMachine, portNumber);
	
	processServer(socketNum, server);
	
	close(socketNum);
}

void processServer(int socketNum, struct sockaddr_in6 server)
{
   int state = SEND_CONNECTION;
   uint8_t *buffer;
	if ((buffer = malloc(MAX_BUF)) < 0)
   {
      perror("malloc");
      exit(-1);
   }
   Packet *packets;
   if ((packets = calloc(windowSize, sizeof(Packet))) < 0)
   {
      perror("calloc");
      exit(-1);
   }
   uint32_t expectedSequence = 0;
   int srejSent = FALSE;
   int tries = 10;   
   struct sockaddr_in6 parentServer;
   memcpy(&parentServer, &server, sizeof(struct sockaddr_in6));
   
   while(state != DONE)
   {
      switch(state)
      {
         case SEND_CONNECTION: /* Send connection packet */
         {
            state = sendSetupPacket(socketNum, parentServer);
            break;
         }
         case WAIT_ON_CONNECTION: /* Wait for packet to come */
         {
            state = waitOnConnection(socketNum, server, &tries);
            break;
         }
         case GET_CONNECTION: /* Receive connection response packet */
         {
            state = processSetupPacket(socketNum, &server, buffer, &tries);
            break;
         }
         case SEND_FILENAME: /* Send filename packet */
         {
            sendPacket(socketNum, 0, FLAG_7_FILENAME, (struct sockaddr *) &server, (uint8_t *) remoteFile, strlen(remoteFile) + 1);
            state = WAIT_ON_FILENAME_RESPONSE;
            break;
         }
         case WAIT_ON_FILENAME_RESPONSE: /* Wait on the filename response packet */
         {
            state = waitOnFilenameResponse(socketNum, server, &tries);
            break;
         }
         case GET_FILENAME_RESPONSE: /* Receive filename response packet */
         {
            state = processFilenameResponse(socketNum, server, buffer, &tries);
            break;
         }
         case WAIT_ON_DATA: /* Wait for more data packets to arrive */
         {
            state = waitOnData(socketNum, server, &tries);
            break;
         }
         case GET_DATA: /* Get an incoming data packet */
         {
            state = getData(socketNum, buffer, server);
            break;
         }
         case PROCESS_DATA: /* Process a received data packet */
         {
            state = processData(socketNum, buffer, server, &expectedSequence, &srejSent, packets);
            break;
         }
         case RESEND_RR: /* Resend the most recent RR */
         {
            state = resendRR(socketNum, server, &expectedSequence, buffer);
            break;
         }
         default:
         {
            fprintf(stderr, "Bad state: %d, Exiting...\n", state);
            exit(-1);
         }
      }
   }
   free(packets);
   free(buffer);
}

/* Resend the most recent RR */
int resendRR(int socketNum, struct sockaddr_in6 server, int *expectedSequence, uint8_t *buf)
{
   uint32_t seq = htonl(*expectedSequence);
   memcpy(buf, &seq, sizeof(*expectedSequence));
   sendPacket(socketNum, sequenceNum, FLAG_5_RR, (struct sockaddr *) &server, (uint8_t *) buf, sizeof(*expectedSequence));
   sequenceNum++;
   return WAIT_ON_DATA;
}

/* Wait for more data packets to arrive for 10 seconds */
int waitOnData(int socketNum, struct sockaddr_in6 server, int *tries)
{
   *tries = 1;
   int dataState = DATA_NOT_READY;
   dataState = safeSelect(socketNum, 10, tries); 

   switch(dataState)
   {
      case DATA_NOT_READY: /* If no data ever comes in 10 seconds, end the process */
      {
         return DONE;
      }
      case DATA_READY: /* Process the incoming data */
      {
         return GET_DATA;
      }
      case TRIES_FINISHED: /* Not needed, but that is okay */
      {
         fprintf(stderr, "Tries finished! Exiting... \n");
         exit(-1);
      }
      default:
      {
         fprintf(stderr, "WAIT ON CONNECTION: SWITCH DEFAULT\n");
         exit(-1);
      }
   }
}

/* Get an incoming data packet */
int getData(int socketNum, uint8_t *buf, struct sockaddr_in6 server)
{
   /* Grab the data packet */
   Header header;
   int len = receivePacket(socketNum, buf, (struct sockaddr *) &server, sizeof(Header) + bufferSize);
   memcpy(&header, buf, sizeof(Header));
   /* If the data packet is bad, wait for more */
   if (len == 0)
   {
      return WAIT_ON_DATA;
   }
   /* Otherwise, process the data */
   else if (header.flag = FLAG_3_DATA)
   {
      return PROCESS_DATA;
   }
   /* If it is not a data packet, wait for more packets */
   return WAIT_ON_DATA;
}

/* Process a received data packet */
int processData(int socketNum, uint8_t *buf, struct sockaddr_in6 server, int32_t *expectedSequence, int *srejSent, Packet *packets)
{
   Header header;
   uint8_t *bufPtr = buf;
   /* Grab the header of the packet */
   memcpy(&header, buf, sizeof(Header));
   /* If the packet is the expected packet, save it and write its contents to the file */
   if (header.sequence == *expectedSequence)
   {
      //*srejSent = FALSE;
      return processExpectedPacket(socketNum, server, buf, packets, header, windowSize, expectedSequence);
   }
   /* If the packet's sequence is greater than expected, send SREJ's for the missing packets */
   else if (header.sequence > *expectedSequence)
   {
      return processOverPacket(socketNum, server, buf, packets, header, windowSize, expectedSequence, srejSent);
   }
   /* If the packet's sequence is less than expected, send an SREJ if that was recently sent and/or an RR */
   else 
   {
      return processUnderPacket(socketNum, server, expectedSequence, srejSent, windowSize, packets);
   }
}

/* Process a packet that was expected */
int processExpectedPacket(int socketNum, struct sockaddr_in6 server, uint8_t *buf, Packet *packets, Header header, int windowSize, int *expectedSequence)
{
   uint8_t *sendBuf;
   if ((sendBuf = malloc(MAX_BUF)) < 0)
   {
      perror("malloc");
      exit(-1);
   }
   /* Create and save this packet */
   Packet packet;
   memcpy(packet.buf, buf, header.length);
   packet.sequence = header.sequence;
   packet.header = header;
   packet.isSREJ = FALSE;
   memcpy(&(packets[header.sequence % windowSize]), &packet, sizeof(Packet));
   /* Write this packet and any other consecutive packets that already arrived with a higher sequence to the file */
   while(packet.sequence == *expectedSequence)
   {
      uint8_t *bufPtr = packet.buf;
      memcpy(&header, bufPtr, sizeof(Header));
      bufPtr += sizeof(Header);
      
      write(outFile, bufPtr, header.length - sizeof(Header));
      
      (*expectedSequence)++;
      
      uint32_t seq;
      memcpy(&seq, expectedSequence, sizeof(uint32_t));
      seq = htonl(seq);
      memcpy(sendBuf, &seq, sizeof(uint32_t));
      
      sendPacket(socketNum, sequenceNum, FLAG_5_RR, (struct sockaddr *) &server, sendBuf, sizeof(*expectedSequence));  
      sequenceNum++;
      
      memcpy(&packet, &(packets[(*expectedSequence) % windowSize]), sizeof(Packet));
   }
   free(sendBuf);
   /* If it is the last packet, make sure to close the file and exit */
   if (header.flag == FLAG_10_FINAL_DATA)
   {
      close(outFile);
      return DONE;
   }

   return WAIT_ON_DATA;
}

/* Process a packet that has a higher sequence number than expected */
int processOverPacket(int socketNum, struct sockaddr_in6 server, uint8_t *buf, Packet *packets, Header header, int windowSize, int *expectedSequence, int *srejSent)
{
   uint8_t *sendBuf;
   if ((sendBuf = malloc(MAX_BUF)) < 0)
   {
      perror("malloc");
      exit(-1);
   }
   /* If SREJ's have not yet been sent since the last expected packet arrived, send all the appropriate SREJ's */
   uint32_t i;
   Packet *prevPacket = &(packets[header.sequence % windowSize]);
   for (i = *expectedSequence; i < header.sequence; i++)
   {
      Packet *packet = &(packets[i % windowSize]);
      if ((packet->sequence != i) && ((packet->isSREJ == FALSE) ||  (prevPacket->isSREJ)))
      {
         uint32_t seq = htonl(i);
         memcpy(sendBuf, &seq, sizeof(*expectedSequence));
         sendPacket(socketNum, sequenceNum, FLAG_6_SREJ, (struct sockaddr *) &server, sendBuf, sizeof(*expectedSequence));
         packet->isSREJ = TRUE;
         sequenceNum++;
      }
   }
   /* Save the packet in the packet array */
   Packet packet;
   memcpy(packet.buf, buf, header.length);
   packet.sequence = header.sequence;
   packet.isSREJ = FALSE;
   memcpy(&(packets[header.sequence % windowSize]), &packet, sizeof(Packet));
   
   free(sendBuf);
   /* Wait for more data to come */
   return WAIT_ON_DATA;
}

/* Process a data packet that has a sequence number less than expected */
int processUnderPacket(int socketNum, struct sockaddr_in6 server, int *expectedSequence, int *srejSent, int windowSize, Packet *packets)
{
   uint8_t *sendBuf;
   if ((sendBuf = malloc(MAX_BUF)) < 0)
   {
      perror("malloc");
      exit(-1);
   }
   
   Packet packet = packets[*expectedSequence % windowSize];
   /* If an SREJ(s) has been sent since the last expected packet, send it again */
   if (packet.isSREJ)
   {      
      uint32_t seq = htonl(*expectedSequence);
      memcpy(sendBuf, &seq, sizeof(*expectedSequence));
      sendPacket(socketNum, sequenceNum, FLAG_6_SREJ, (struct sockaddr *) &server, sendBuf, sizeof(*expectedSequence));
      sequenceNum++;
   }
   free(sendBuf);
   /* Just to try moving the window up, resend the most recent RR */
   return RESEND_RR;
}

/* Send the first packet to the server */
int sendSetupPacket(int socketNum, struct sockaddr_in6 server)
{
   uint8_t buf[MAX_BUF];
   uint8_t *bufPtr = buf;
   
   memcpy(bufPtr, &windowSize, sizeof(windowSize));
   bufPtr += sizeof(windowSize);
   memcpy(bufPtr, &bufferSize, sizeof(bufferSize));
   bufPtr += sizeof(bufferSize);
   
   uint16_t length = bufPtr - buf;
   sendPacket(socketNum, 0, FLAG_1_SETUP, (struct sockaddr *) &server, buf, length);
   
   bufPtr = buf;
   int win = 0;
   int buff = 0;
   memcpy(&win, bufPtr, sizeof(windowSize));
   bufPtr += sizeof(windowSize);
   
   memcpy(&buff, bufPtr, sizeof(bufferSize));
   
   return WAIT_ON_CONNECTION;
}

/* Wait for the connection response packet */
int waitOnConnection(int socketNum, struct sockaddr_in6 server, int *tries)
{   
   int dataState = DATA_NOT_READY;
   dataState = safeSelect(socketNum, 1, tries); 

   switch(dataState)
   {
      case DATA_NOT_READY: /* If no response, send the connection packet again */
      {
         return SEND_CONNECTION;
      }
      case DATA_READY: /* If data is ready, grab it and make sure the connection is solid */
      {
         return GET_CONNECTION;
      }
      case TRIES_FINISHED: /* If there is never a response from the server, end the connection */
      {
         fprintf(stderr, "Tries finished! Exiting... \n");
         exit(-1);
      }
      default:
      {
         fprintf(stderr, "WAIT ON CONNECTION: SWITCH DEFAULT\n");
         exit(-1);
      }
   }
}

/* Wait for a response about the filename */
int waitOnFilenameResponse(int socketNum, struct sockaddr_in6 server, int *tries)
{   
   int dataState = DATA_NOT_READY;
   dataState = safeSelect(socketNum, 1, tries); 

   switch(dataState)
   {
      case DATA_NOT_READY: /* If it is not ready, resend the filename */
      {
         return SEND_FILENAME;
      }
      case DATA_READY: /* If it is ready, get the response */
      {
         return GET_FILENAME_RESPONSE;
      }
      case TRIES_FINISHED: /* If the response never comes, end connection */
      {
         fprintf(stderr, "Tries finished! Exiting... \n");
         exit(-1);
      }
      default:
      {
         fprintf(stderr, "WAIT ON CONNECTION: SWITCH DEFAULT\n");
         exit(-1);
      }
   }
}

/* Process the incoming setup packet */
int processSetupPacket(int socketNum, struct sockaddr_in6 *server, uint8_t *buf, int *tries)
{
   int len = receivePacket(socketNum, buf, (struct sockaddr *) server, sizeof(Header));
   if (len == 0)
   {
      return SEND_CONNECTION;
   }
   *tries = 10;
   Header header;
   uint8_t *bufPtr = buf;
   
   memcpy(&header, bufPtr, sizeof(Header));
   bufPtr += sizeof(Header);
   /* If the packet is not a setup packet, something went wrong, so terminate */
   if (header.flag != FLAG_2_SETUP)
   {
      fprintf(stderr, "Invalid first packet! Exiting... \n");
      exit(-1);
   }
   
   return SEND_FILENAME;
}

/* Process the response to the filename */
int processFilenameResponse(int socketNum, struct sockaddr_in6 server, uint8_t *buf, int *tries)
{
   Header header;
   *tries = 10;
   int len = 0;
   int addrLen = sizeof(struct sockaddr_in6);
   if ((len = safeRecvfrom(socketNum, buf, sizeof(Header), MSG_PEEK, (struct sockaddr *) &server, &addrLen)) == 0)
   {
      fprintf(stderr, "No message! Exiting... \n");
      exit(-1);
   }
   memcpy(&header, buf, sizeof(Header));
   if (header.flag == FLAG_8_BAD_FILENAME)
   {
      len = receivePacket(socketNum, buf, (struct sockaddr *) &server, MAX_BUF);
      if (len == 0)
      {
         return SEND_FILENAME;
      }
      /* If the filename is bad, print the errno message from the server */
      else if (header.flag == FLAG_8_BAD_FILENAME)
      {
         uint8_t *bufPtr = buf + sizeof(Header);
         memcpy(&errno, bufPtr, sizeof(errno));
         sendHeader(socketNum, 0, FLAG_9_END_CONNECTION, (struct sockaddr *) &server, sizeof(struct sockaddr_in6));
         perror("from server");      
         exit(-1);
      }
   }
   else if(header.flag == FLAG_3_DATA || header.flag == FLAG_10_FINAL_DATA)
   {
      len = receivePacket(socketNum, buf, (struct sockaddr *) &server, sizeof(Header) + bufferSize);
      if (len == 0)
      {
         return SEND_FILENAME;
      }
      return PROCESS_DATA;
   }
   /* Otherwise, just resend the filename */
   else
   {
      return SEND_FILENAME;
   }
}

/* Checks args and returns port number */
int checkArgs(int argc, char* argv[])
{	
	int portNumber = 0;
   int error = 0;
   /* There must be 8 args */
	if (argc != 8)
	{
		fprintf(stderr, "Usage %s: [local-file] [remote-file] [window-size] [buffer-size] [error-percent] [remote-machine] [remote-port]\n", argv[0]);
		exit(-1);
	}
   /* First arg is local filename */
   memcpy(localFile, argv[1], strlen(argv[1]) + 1);
   /* Then filename from server */
   memcpy(remoteFile, argv[2], strlen(argv[2]) + 1);
   /* Grab windowsize */
   if ((windowSize = atoi(argv[3])) == 0)
   {
		fprintf(stderr, "Usage %s: [local-file] [remote-file] [window-size] [buffer-size] [error-percent] [remote-machine] [remote-port]\n", argv[0]);
		exit(-1);
   }
   /* Then grab buffersize */
   if ((bufferSize = atoi(argv[4])) == 0)
   {
		fprintf(stderr, "Usage %s: [local-file] [remote-file] [window-size] [buffer-size] [error-percent] [remote-machine] [remote-port]\n", argv[0]);
		exit(-1);
   }
   if (bufferSize > 1400)
   {
      fprintf(stderr, "Usage %s: [local-file] [remote-file] [window-size] [buffer-size] [error-percent] [remote-machine] [remote-port]\n", argv[0]);
		exit(-1);
   }
   /* Then grab errorpercent */
   errorPercent = atof(argv[5]);
   if (errorPercent <= 0 || errorPercent >= 1)
   {
		fprintf(stderr, "Usage %s: [local-file] [remote-file] [window-size] [buffer-size] [error-percent] [remote-machine] [remote-port]\n", argv[0]);
		exit(-1);
   }
   /* Grab the remote machine */
   memcpy(remoteMachine, argv[6], strlen(argv[6]) + 1);
   /* Finally, the port number */
   if ((portNumber = atoi(argv[7])) == 0)
   {
		fprintf(stderr, "Usage %s: [local-file] [remote-file] [window-size] [buffer-size] [error-percent] [remote-machine] [remote-port]\n", argv[0]);
		exit(-1);
   }

	return portNumber;
}