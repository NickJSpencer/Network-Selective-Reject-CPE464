/* Server side - UDP Code				    */
/* By Hugh Smith	4/1/2017	*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>

#include "cpe464.h"
#include "networks.h"

#define MAXBUF 80


void listenForClients(int socketNum);

void processClient(int serverSocketNum, uint8_t *buf, int32_t len, Connection client);
int processSetupPacket(uint8_t *buf, int32_t len, Connection *client, int *windowSize, int *bufferSize, Packet **packets);

int prepareData(int socketNum, struct sockaddr_in6 server, Packet *packets, uint32_t *currentPreparePacket, int32_t file, int bufferSize, int windowSize, int *currentRR, int *donePreparing);
int sendData(int socketNum, Connection *client, Packet *packets, uint32_t *currentPacket, int *currentRR, int *currentSREJ, int windowSize, uint32_t *currentPreparePacket, int *donePreparing);
int processAck(int socketNum, struct sockaddr_in6 server, int *currentRR, int *currentSREJ, int *donePreparing, int *tries);
int checkForAck(int socketNum, struct sockaddr_in6 server, int *tries, int seconds);
int waitForAck(int socketNum, struct sockaddr_in6 server, int *tries, int seconds, int *currentRR, int *currentPacket, Packet *packets, int windowSize);

int waitOnFilename(int socketNum, struct sockaddr_in6 server, int *tries);
int processFilename(int socketNum, uint8_t *buf, int *datafile, Connection *client, int *isErr, int *tries);

int sendFilenameResponse(int socketNum, Connection *client, int *isErr);
int waitOnFilenameResponse(int socketNum, Connection *client, int *tries);
int processFilenameResponse(uint8_t *buf, Connection *client);

int checkArgs(int argc, char *argv[]);

int lastPacket = -1;
float errorPercent = 0.0f;

int main (int argc, char *argv[])
{ 
	int socketNum = 0;				
	struct sockaddr_in6 client;	// Can be either IPv4 or 6
	int portNumber = 0;
   pthread_t thread[MAX_THREADS];
   
	portNumber = checkArgs(argc, argv);
	socketNum = udpServerSetup(portNumber);

   listenForClients(socketNum);
}

void listenForClients(int socketNum)
{  
   pid_t pid = 0;
   int status = 0;
   fd_set sockets;
   uint8_t buf[MAX_BUF];
   int32_t len;
   Connection client;
   
   /* Loop forever looking for new clients */
   while(1)
   {
      /* Reset all the sockets */
      FD_ZERO(&sockets);
      FD_SET(socketNum, &sockets);
            
      /* Wait for packets to arrive */
      if (select(socketNum + 1, &sockets, NULL, NULL, NULL) < 0)
      {
         perror("select");
         exit(-1);
      }
      /* A new client wants to connect! */
      if (FD_ISSET(socketNum, &sockets))
      {         
         len = receivePacket(socketNum, buf, (struct sockaddr *) &(client.remote), sizeof(Header) + 2 * sizeof(int));
         if (len > 0)
         {
            if ((pid = fork()) < 0)
            {
               perror("fork");
               exit(-1);
            }
            /* Child Process */
            if (pid == 0)
            {
               sendtoErr_init(errorPercent, DROP_ON, FLIP_ON, DEBUG_OFF, RSEED_ON);
               processClient(socketNum, buf, len, client);
            }
         }
         /* Parent Process */
         while(waitpid(-1, &status, WNOHANG) > 0);
      }
      else
      {
         fprintf(stderr, "Invalid Socket\n");
      }
   }
}

/* State machine for each individual thread (so each individual client) */
void processClient(int serverSocketNum, uint8_t *buf, int32_t len, Connection client)
{
   int file;
   int state = PROCESS_SETUP;
   int tries = 1;
   int checkTries = 1;
   int isErr = 0;
   int windowSize = 0;
   int bufferSize = 0;
   int currentRR = 0;
   int currentSREJ = -1;
   uint32_t currentPacket = 0;
   uint32_t currentPreparePacket = 0;
   int donePreparing = FALSE;
   Packet *packets = NULL;
   
   while(state != DONE)
   {
      switch(state)
      {
         case PROCESS_SETUP: /* Process the first packet received from parent process */
         {
            state = processSetupPacket(buf, len, &client, &windowSize, &bufferSize, &packets);
            break;
         }
         case SEND_SETUP_RESPONSE: /* Respond to client with a successful connection message */
         {
            sendHeader(client.socketNum, 0, FLAG_2_SETUP, (struct sockaddr *) &(client.remote), sizeof(struct sockaddr_in6));
            state = WAIT_ON_FILENAME;
            break;
         }
         case WAIT_ON_FILENAME: /* Wait for the filename packet from the client */
         {
            state = waitOnFilename(client.socketNum, client.remote, &tries);
            break;
         }
         case GET_FILENAME: /* Get and process the filename packet that arrived */
         {
            state = processFilename(client.socketNum, buf, &file, &client, &isErr, &tries);
            break;
         }
         case SEND_FILENAME_RESPONSE: /* Send the errno response to the client for a bad filename */
         {
            state = sendFilenameResponse(client.socketNum, &client, &isErr);
            break;
         }
         case WAIT_ON_FILENAME_RESPONSE: /* Wait for the client to disconnect, resend filename if this times out */
         {
            state = waitOnFilenameResponse(client.socketNum, &client, &tries);
            break;
         }
         case GET_FILENAME_RESPONSE: /* Get the response from the client about the filename (presumably to end connection) */
         {
            state = processFilenameResponse(buf, &client);
            break;
         }
         case PREPARE_DATA: /* Prepare a data packet within the window and save it within packets array */
         {
            state = prepareData(client.socketNum, client.remote, packets, &currentPreparePacket, file, bufferSize, windowSize, &currentRR, &donePreparing);
            break;
         }
         case SEND_DATA: /* Send either the next data packet or a repeat packet from a received SREJ */
         {
            state = sendData(client.socketNum, &client, packets, &currentPacket, &currentRR, &currentSREJ, windowSize, &currentPreparePacket, &donePreparing);
            break;
         }
         case WAIT_FOR_ACK: /* Wait 1 second for an RR or SREJ packet, otherwise resend lowest packet in window (10 tries) */
         {
            state = waitForAck(client.socketNum, client.remote, &tries, 1, &currentRR, &currentPacket, packets, windowSize);
            break;
         }
         case CHECK_FOR_ACK: /* Wait 0 seconds for an RR or SREJ packet, otherwise goto SEND_DATA state */
         {
            state = checkForAck(client.socketNum, client.remote, &checkTries, 0);
            break;
         }
         case PROCESS_ACK: /* Process and incoming RR or SREJ packet */
         {
            state = processAck(client.socketNum, client.remote, &currentRR, &currentSREJ, &donePreparing, &tries);
            break;
         }
         default: /* State machine should never reach the default state, so exit */
         {
            fprintf(stderr, "Bad state: %d, Exiting...\n", state);
            exit(-1);
         }
      }
   }
   if (packets != NULL)
   {
      free(packets);
   }
   exit(0);
}

/* Prepare a data packet within the window and save it within packets array */
int prepareData(int socketNum, struct sockaddr_in6 server, Packet *packets, uint32_t *currentPreparePacket, int32_t file, int bufferSize, int windowSize, int *currentRR, int *donePreparing)
{
   Packet packet;
   int length = 0;
   uint8_t data[MAX_BUF];
   Header header;
   
   /* If all of the data from the file has been copied, just send data */
   if (*donePreparing)
   {
      return SEND_DATA;
   }
   /* If the entire window has already been prepared, just send data */
   if ((*currentPreparePacket) - (*currentRR) >= windowSize)
   {
      return SEND_DATA;
   }
   /* Read the next buffer length of the data */
   if((length = read(file, data, bufferSize)) < 0)
   {
      perror("read");
      exit(-1);
   }
   /*Prepare the packet to store */
   memcpy(&(packet.buf), data, length);
   packet.sequence = *currentPreparePacket;
   header.length = length;
   /* If the length is the size of the bufferSize, there is still more data, so it is a normal packet */
   if (length == bufferSize) 
   {
      header.flag = FLAG_3_DATA;
   }
   else /* Otherwise, it is the last data packet */
   {
      header.flag = FLAG_10_FINAL_DATA;
      *donePreparing = TRUE;
      lastPacket = *currentPreparePacket;
   }
   /* Store the packet */
   packet.header = header;
   memcpy(&(packets[(*currentPreparePacket) % windowSize]), &packet, sizeof(Packet));
   packet = packets[(*currentPreparePacket) % windowSize];
   /* Increment the next packet to prepare */
   (*currentPreparePacket)++;
   
   return SEND_DATA;
}

/* Send either the next data packet or a repeat packet from a received SREJ */
int sendData(int socketNum, Connection *client, Packet *packets, uint32_t *currentPacket, int *currentRR, int *currentSREJ, int windowSize, uint32_t *currentPreparePacket, int *donePreparing)
{
   
   Packet packet;
   /* If the packet about to be sent is less than the current RR, update currentPacket */
   if (*currentPacket < *currentRR)
   {
      *currentPacket = *currentRR;
   }
   /* If there is an SREJ to process, grab that particular packet */
   if (*currentSREJ != -1)
   {
      packet = packets[(*currentSREJ) % windowSize];
      *currentSREJ = -1;
   }
   /* If the window is closed */
   else if ((*currentPacket) - (*currentRR) >= windowSize)
   {
      return WAIT_FOR_ACK;
   }
   /* If the packet to be sent has not been prepared yet, prepare it, or wait for ACK if the last packet has been stored */
   else if (*currentPacket >= *currentPreparePacket)
   {
      if (*donePreparing || ((*currentPreparePacket - *currentRR) >= windowSize))
      {
         return WAIT_FOR_ACK;
      }
      return PREPARE_DATA;
   }
   else /* Prepare the packet based on the currentPacket */
   {
      packet = packets[(*currentPacket) % windowSize];
      (*currentPacket)++;
   }
   /* Send the packet */
   sendPacket(socketNum, packet.sequence, packet.header.flag, (struct sockaddr *) &(client->remote), packet.buf, packet.header.length);
   /* If it is the last packet, wait 1 sec for ACK */
   if (packet.header.flag == FLAG_10_FINAL_DATA)
   {
      return WAIT_FOR_ACK;
   }
   /* otherwise, check for ACK */
   return CHECK_FOR_ACK;
}

/* Process and incoming RR or SREJ packet */
int processAck(int socketNum, struct sockaddr_in6 server, int *currentRR, int *currentSREJ, int *donePreparing, int *tries)
{
   /* Make sure the amount of tries for waiting for ACK's is reset to 10 */
   *tries = 10;
   uint8_t buf[MAX_BUF];
   uint8_t *bufPtr = buf;
   uint32_t seq;
   /* Receive the packet */
   int len = receivePacket(socketNum, bufPtr, (struct sockaddr *) &server, sizeof(Header) + sizeof(uint32_t));
   if (len == 0)
   {
      return PREPARE_DATA;
   }
   /* Grab contents from the packet */
   Header header;
   memcpy(&header, bufPtr, sizeof(Header));
   bufPtr += sizeof(Header);
   memcpy(&seq, bufPtr, sizeof(seq));  
   seq = ntohl(seq);
   /* If the packet is RR, make sure to update the current packet, if it is the last one, end the thread */
   if (header.flag == FLAG_5_RR)
   {
      if (*donePreparing && seq > lastPacket)
      {
         return DONE;
      }
      *currentRR = seq;
      return CHECK_FOR_ACK;
   }
   /* If the packet is SREJ, set SREJ variable so it can be sent again immediately */
   else if (header.flag == FLAG_6_SREJ)
   {
      *currentSREJ = seq;
      return SEND_DATA;
   }
   else /* Otherwise, the packet should be ignored */
   {
      return PREPARE_DATA;
   }
}

/* Wait 0 seconds for an RR or SREJ packet, otherwise goto SEND_DATA state */
int checkForAck(int socketNum, struct sockaddr_in6 server, int *tries, int seconds)
{
   int dataState = DATA_NOT_READY;
   dataState = safeSelect(socketNum, seconds, tries); 
   *tries = 1;
   
   switch(dataState)
   {
      case DATA_NOT_READY: /* Prepare and send another packet */
      {
         return PREPARE_DATA;
      }
      case DATA_READY: /* Process ACK and proceed based on the contents */
      {
         return PROCESS_ACK;
      }
      case TRIES_FINISHED: /* This should never happen in this case, but here just in case */
      {
         fprintf(stderr, "Tries finished! Exiting... \n");
         exit(-1);
      }
      default:
      {
         fprintf(stderr, "Something went wrong in checkForAck() \n");
         exit(-1);
      }
   }
}

/* Wait 1 second for an RR or SREJ packet, otherwise resend lowest packet in window (10 tries) */
int waitForAck(int socketNum, struct sockaddr_in6 server, int *tries, int seconds, int *currentRR, int *currentPacket, Packet *packets, int windowSize)
{
   int dataState = DATA_NOT_READY;
   dataState = safeSelect(socketNum, seconds, tries); 

   switch(dataState)
   {
      case DATA_NOT_READY: /* Resend the lowest packet in the window again and wait for a response */
      {
         Packet packet = packets[*currentRR % windowSize];
         uint32_t seq = packet.sequence;
         sendPacket(socketNum, seq, packet.header.flag, (struct sockaddr *) &server, packet.buf, packet.header.length);
         return WAIT_FOR_ACK;
      }
      case DATA_READY: /* Process the incoming ACK */
      {
         return PROCESS_ACK;
      }
      case TRIES_FINISHED: /* If trying to get data for 10 tries, exit */
      {
         fprintf(stderr, "Tries finished! Exiting... \n");
         exit(-1);
      }
      default:
      {
         fprintf(stderr, "Something went wrong in waitForAck()\n");
         exit(-1);
      }
   }
}

/* Wait for the filename packet from the client */
int waitOnFilename(int socketNum, struct sockaddr_in6 server, int *tries)
{   
   int dataState = DATA_NOT_READY;
   dataState = safeSelect(socketNum, 10, tries); 

   switch(dataState)
   {
      case DATA_NOT_READY: /* Resend the setup response */
      {
         return SEND_SETUP_RESPONSE;
      }
      case DATA_READY: /* Get and process the filename packet */
      {
         return GET_FILENAME;
      }
      case TRIES_FINISHED: /* If trying to get data for 10 tries, exit */
      {
         fprintf(stderr, "Tries finished! Exiting... \n");
         exit(-1);
      }
      default:
      {
         fprintf(stderr, "Something went wrong in waitOnFilename() \n");
         exit(-1);
      }
   }
}

/* Wait for the client to disconnect, resend filename if this times out */
int waitOnFilenameResponse(int socketNum, Connection *client, int *tries)
{   
   int dataState = DATA_NOT_READY;
   dataState = safeSelect(socketNum, 1, tries); 

   switch(dataState)
   {
      case DATA_NOT_READY: /* Resend the filename response */
      {
         return SEND_FILENAME_RESPONSE;
      }
      case DATA_READY: /* Get and process the filename response */
      {
         return GET_FILENAME_RESPONSE;
      }
      case TRIES_FINISHED: /* If trying to get data for 10 tries, exit */
      {
         fprintf(stderr, "Tries finished! Exiting... \n");
         exit(-1);
      }
      default:
      {
         fprintf(stderr, "Something went wrong in waitOnFilenameResponse() \n");
         exit(-1);
      }
   }
}

/* Process the first packet received from parent process */
int processSetupPacket(uint8_t *buf, int32_t len, Connection *client, int *windowSize, int *bufferSize, Packet **packets)
{
   Header header;
   uint8_t *bufPtr = buf;
   
   /* Get client socket */
   if ((client->socketNum = socket(AF_INET6, SOCK_DGRAM, 0)) < 0)
   {
      perror("socket");
      exit(-1);
   }
   /* Grab the header */
   memcpy(&header, bufPtr, sizeof(Header));
   bufPtr += sizeof(Header);
   /* First packet should have flag 1, otherwise something funky is going on */
   if (header.flag != FLAG_1_SETUP)
   {
      fprintf(stderr, "Invalid first packet! Exiting... \n");
      exit(-1);
   }
   /* Grab the window size and buffer size */
   memcpy(windowSize, bufPtr, sizeof(*bufferSize));
   bufPtr += sizeof(*windowSize);
   memcpy(bufferSize, bufPtr, sizeof(*bufferSize));
   /* Initialize the packets based on the window size */
   if((*packets = calloc(*windowSize, sizeof(Packet))) < 0)
   {
      perror("calloc");
      exit(-1);
   }
   
   return SEND_SETUP_RESPONSE;
}

/* Get and process the filename packet that arrived */
int processFilename(int socketNum, uint8_t *buf, int *datafile, Connection *client, int *isErr, int *tries)
{
   /* Reset tries to 10 because data arrived! */
   *tries = 10;
   int len = receivePacket(client->socketNum, buf, (struct sockaddr *) &(client->remote), MAX_BUF);
   if (len == 0) /* If there is no packe to process (most likely due to a bad checksum), continue to wait for filename */
   {
      return WAIT_ON_FILENAME;
   }
   /* Parse filename packet */
   Header header;
   uint8_t *bufPtr = buf;
   char filename[MAX_BUF];
   memcpy(&header, bufPtr, sizeof(Header));
   bufPtr += sizeof(Header);
   memcpy(filename, bufPtr, header.length - sizeof(Header));

   /* Try to open file; Return errno code if its bad or start sending data if it is good */
   int fd;
   if ((fd = open(filename, O_RDONLY)) < 0)
   {
      *isErr = 1;
      return SEND_FILENAME_RESPONSE;
   }
   else
   {
      *datafile = fd;
      return PREPARE_DATA;
   }
}

/* Send the errno response to the client for a bad filename */
int sendFilenameResponse(int socketNum, Connection *client, int *isErr)
{
   char errBuf[MAX_BUF];
   memcpy(errBuf, &errno, sizeof(errno));
   sendPacket(socketNum, 0, FLAG_8_BAD_FILENAME, (struct sockaddr *) &(client->remote), errBuf, sizeof(errno));
   return WAIT_ON_FILENAME_RESPONSE;
}

/* Get the response from the client about the filename (presumably to end connection) */
int processFilenameResponse(uint8_t *buf, Connection *client)
{
   /* Grab the packet, resend the filename response if it is a bad packet */
   int len = receivePacket(client->socketNum, buf, (struct sockaddr *) &(client->remote), MAX_BUF);
   if (len == 0)
   {
      return SEND_FILENAME_RESPONSE;
   }
   /* Parse filename response packet */
   Header header;
   uint8_t *bufPtr = buf;
   memcpy(&header, bufPtr, sizeof(Header));
   bufPtr += sizeof(Header);
   /* If client has acknowledged the bad filename, end connection */
   if (header.flag == FLAG_9_END_CONNECTION)
   {
      return DONE;
   }
   /* Otherwise, resend the filename response */
   return SEND_FILENAME_RESPONSE;
}

/* Checks args and returns port number */
int checkArgs(int argc, char *argv[])
{
	int portNumber = 0;

   /* There must be either 2 or 3 args */
	if (argc > 3 || argc < 2)
	{
		fprintf(stderr, "Usage %s [error percent] [optional port number]\n", argv[0]);
		exit(-1);
	}
	
   /* if 3 args, 3rd is the port number */
	if (argc == 3)
	{
		portNumber = atoi(argv[2]);
	}
   
   /* Grab the percent and try to convert it to a float */
   errorPercent = atof(argv[1]);
   if (errorPercent <= 0 || errorPercent >= 1)
   {
      fprintf(stderr, "Error percent must be between 0 and 1\n");
      exit(-1);
   }
	
	return portNumber;
}


