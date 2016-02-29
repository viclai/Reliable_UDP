#include "server.h"
#include "noise.h"

#include <string.h>     // memset
#include <stdlib.h>     // atoi, exit, malloc, free, atol
#include <signal.h>     // signalaction
#include <unistd.h>     // alarm
#include <netinet/in.h> // sockaddr_in, htons, htonl, INADDR_ANY
#include <sys/types.h>  
#include <errno.h>      // errno
#include <sys/select.h> // select
#include <sys/socket.h> // socket, bind, sockaddr, AF_INET, SOCK_DGRAM
#include <sys/time.h>   // FD_SET, FD_ISSET, FD_ZERO, gettimeofday, timeval
#include <stdio.h>      // fprintf, fopen, ftell, fclose, fseek, rewind,
                        // SEEK_END

#include <string>       // string, to_string, c_str
#include <cstring>      // strcpy, strchr, strstr
#include <vector>       // vector
#include <list>         // list
#include <utility>      // pair, make_pair
using namespace std;

int main(int argc, char* argv[])
{
  int sockfd, port, n, activity, windowSize;
  string clientMsg, clientName;
  struct sockaddr_in serverAddr, clientAddr;
  socklen_t addrLen;
  struct sigaction action;
  fd_set readFds;
  const char* serverMsg;
  pair<MessageType, string> typeValue;
  MessageType msgType;

  if (argc < 3) // TODO: Add probabilities for corruption and packet loss
  {
    fprintf(stderr, "Usage: %s PORT WINDOW-SIZE\n", argv[0]);
    exit(1);
  }

  port = atoi(argv[1]);

  windowSize = atoi(argv[2]);
  if (windowSize > MAX_SEQUENCE / 2)
  {
    fprintf(stderr, "Window size is too large\n");
    exit(1);
  }
  if (windowSize < MAX_PACKET_SIZE)
  {
    fprintf(stderr, "Window size must be at least %dB\n", MAX_PACKET_SIZE);
    exit(1);
  }

  sockfd = socket(AF_INET, SOCK_DGRAM, 0);
  if (sockfd < 0)
  {
    perror("ERROR opening socket");
    exit(1);
  }

  memset((char*)&serverAddr, 0, sizeof(serverAddr));
  serverAddr.sin_family = AF_INET;
  serverAddr.sin_addr.s_addr = htonl(INADDR_ANY);
  serverAddr.sin_port = htons(port); // Converts host byte order to network
                                     // byte order

  if (bind(sockfd, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0)
  {
    perror("ERROR binding");
    exit(1);
  }

  action.sa_handler = catchAlarm;
  action.sa_flags = SA_RESTART;
  sigaction(SIGALRM, &action, NULL);
  alarm(ALARM_TIME);

  addrLen = sizeof(clientAddr);

  clientReq = new SRInfo;
  while (true)
  {
    FD_ZERO(&readFds);
    FD_SET(sockfd, &readFds);

    // TODO: Timeout not set in select()
    activity = select(sockfd + 1, &readFds, NULL, NULL, NULL);
    if (activity < 0)
    {
      if (errno != EINTR)
        fprintf(stderr, "* Select error(#%d)\n", errno);
    }
    else if (activity == 0) // Timeout
    {
      delete clientReq;
      clientReq = new SRInfo;
      continue;
    }
    else
    {
      if (FD_ISSET(sockfd, &readFds))
      {
        fprintf(stdout, "*\n");
        fprintf(stdout, "* Receiving a packet...\n");

        if (recvMsg(sockfd, clientMsg, 0, (struct sockaddr*)&clientAddr,
                         &addrLen) == 0)
        {
          // TODO
          close(sockfd);
          break;
        }
        else
        {
          // TODO: Call noise function to determine if packet is corrupted or
          // lost

          fprintf(stdout, "* Incoming message: %s\n", clientMsg.c_str());

          // Process client message
          typeValue = parseMsg(clientMsg);
          msgType = typeValue.first;
          if (msgType == REQUEST)
          {
            clientReq->clientInfo.address = (struct sockaddr*)&clientAddr;
            clientReq->clientInfo.length = addrLen;
            clientReq->clientInfo.sockfd = sockfd;

            if (fileExists(&clientReq->filemeta))
            {
              fprintf(stdout, "* Requested file exists\n");
            }
            else
            {
              fprintf(stdout, "* Requested file does not exist\n");
            }
            fprintf(stdout, "* Preparing packet(s) for '%s'...\n",
              clientReq->filemeta.name.c_str());
            createSegments();
          }
          else if (msgType == ACK)
          {
            fprintf(stdout, "* Processing ACK(s)...\n");
            processAcks(&clientReq->sequenceSpace, typeValue.second);
          }
          else // Message with unknown format
          {
            serverMsg = "Unable to process your message\n";
            n = sendMsg(sockfd, serverMsg, strlen(serverMsg), 0,
                        (struct sockaddr*)&clientAddr, addrLen);
            if (n < 0)
            {
              perror("* ERROR sending to client address");
              exit(1);
            }
            continue;
          }

          // Send packets if window is not full
          sendPackets(&clientReq->sequenceSpace, windowSize, sockfd,
                      (struct sockaddr*)&clientAddr, addrLen);
        }
      }
    }
  }

  return 0;
}

FileData::FileData()
{
  length = 0;
}

Ack::Ack()
{
  isAcked = false;
}

AckSpace::AckSpace()
{
  windowSize = base = nextSeq = 0;
}

int recvMsg(int sockfd, string& msg, int flags, struct sockaddr* clientAddr,
            socklen_t* addrLen)
{
  char buffer[BUFFER_SIZE];
  int nRead;

  msg = "";
  memset(buffer, 0, BUFFER_SIZE);

  if ((nRead =
           recvfrom(sockfd, buffer, BUFFER_SIZE, 0, clientAddr, addrLen)) < 0)
    return -1;
  msg += buffer;
  return nRead;
}

int sendMsg(int sockfd, const void* buffer, size_t length, int flags,
            struct sockaddr* destAddr, socklen_t destLen)
{
  ssize_t n;
  const char* p = (const char*)buffer;
  while (length > 0)
  {
    n = sendto(sockfd, p, length, flags, destAddr, destLen);
    if (n <= 0)
      break;
    p += n;
    fprintf(stdout, "* %d bytes sent to client\n", (int)n);
    length -= n;
  }
  return (n > 0) ? 0 : -1;
}

pair<MessageType, string> parseMsg(string message)
{
  int len, startIndex, endIndex;
  char* value; // Either file name or ACK number(s)
  char* msg;
  char* space;
  pair<MessageType, string> typeValue;

  msg = new char[message.size() + 1];
  strcpy(msg, message.c_str());
  if (strstr(msg, "File") == msg)
    typeValue.first = REQUEST;
  else if (strstr(msg, "ACK") == msg)
    typeValue.first = ACK;
  else
  {
    typeValue.first = UNKNOWN;
    return typeValue;
  }

  if (typeValue.first == ACK)
  {
    while (strstr(msg, "ACK") != NULL)
    {
      space = strchr(msg, ' ');
      startIndex = space - msg + 1;
      if (strstr(space, "ACK") != NULL)
      {
        endIndex = strstr(space, "ACK") - msg;
        len = endIndex - startIndex;
        value = new char[len + 1];
        memcpy(value, &msg[startIndex], len);
        value[len] = '\0';
        typeValue.second += value;
        typeValue.second += " ";
        msg = strstr(space, "ACK");
      }
      else
      {
        endIndex = strchr(msg, '\0') - msg;
        len = endIndex - startIndex;
        value = new char[len + 1];
        memcpy(value, &msg[startIndex], len);
        value[len] = '\0';
        typeValue.second += value;
        break;
      }
    }
  }
  else // REQUEST
  {
    space = strchr(msg, ' ');
    startIndex = space - msg + 1;
    len = message.size() - startIndex;

    value = new char[len + 1];
    memcpy(value, &msg[startIndex], len);
    value[len] = '\0';
    clientReq->filemeta.name = value;
    typeValue.second = value;
  }
  
  return typeValue;
}

bool fileExists(FileData* file)
{
  FILE* pFile;
  char* fileData;

  pFile = fopen(file->name.c_str(), "rb");
  if (pFile == NULL)
    return false;

  fseek(pFile, 0, SEEK_END);
  file->length = ftell(pFile);
  rewind(pFile);
  fileData = (char*)malloc((file->length + 1) * sizeof(char));
  fread(fileData, 1, file->length, pFile);
  file->content = fileData;

  free(fileData);
  fclose(pFile);
  return true;
}

void createSegments()
{
  int curSequence, curFilePos, dataLen, fileSize;
  string header, segmentData;
  Ack segment;

  curSequence = curFilePos = 0;
  fileSize = clientReq->filemeta.length;
  
  if (fileSize == 0)
  {
    header = "SEQ: " + to_string(curSequence) + "\n" +
             "File Size: " + to_string(fileSize) + "B\n\n";
    segmentData = header;

    segment.sequence = curSequence;
    segment.data = segmentData;
    clientReq->sequenceSpace.seqNums.push_back(segment);
    return;
  }

  while (curFilePos < fileSize)
  {
    header = "SEQ: " + to_string(curSequence) + "\n" +
             "File Size: " + to_string(fileSize) + "B\n\n";
    dataLen = MAX_PACKET_SIZE - header.size();
    segmentData = header +
                  clientReq->filemeta.content.substr(curFilePos, dataLen);

    segment.sequence = curSequence;
    segment.data = segmentData;
    clientReq->sequenceSpace.seqNums.push_back(segment);
    
    curSequence = (curSequence + segmentData.size()) % (MAX_SEQUENCE + 1);
    curFilePos += dataLen;
  }

  return;
}

void processAcks(AckSpace* sequenceSpace, string acks)
{
  int i, j, k, windowSize;
  vector<int> ackNums;
  string temp;
  list<pair<int, struct timeval> >::iterator it;
  
  windowSize = sequenceSpace->windowSize;

  // Extract ACK number(s)
  for (i = 0; i < (int)acks.size(); i++)
  {
    if (acks[i] != ' ')
      temp += acks[i];
    else
    {
      ackNums.push_back(atoi(temp.c_str()));
      temp = "";
    }
  }
  ackNums.push_back(atoi(temp.c_str())); // Extract last ACK number

  // i: counter for window size
  // j: index of packet
  // k: index of ACK number
  for (i = 0, j = sequenceSpace->base, k = 0; i < windowSize; )
  {
    if (sequenceSpace->seqNums[j].sequence == ackNums[k])
    { 
      // Remove sequence from sentUnacked list
      it = sequenceSpace->sentUnacked.begin();
      while (it != sequenceSpace->sentUnacked.end())
      {
        if (sequenceSpace->seqNums[it->first].sequence == ackNums[k])
        {
          sequenceSpace->sentUnacked.erase(it);
          break;
        }
        it++;
      }
      sequenceSpace->seqNums[j].isAcked = true;
      fprintf(stdout, "* ACK %d processed\n", ackNums[k]);

      if (j == sequenceSpace->base)
        sequenceSpace->base++;
      if (windowSize != 0)
      {
        sequenceSpace->windowSize -= sequenceSpace->seqNums[j].data.size();
        windowSize = sequenceSpace->windowSize;
      }
      k++;
      if (k == (int)ackNums.size())
        break;

      // Reset
      j = sequenceSpace->base;
      i = 0;
      continue;
    }
    i += sequenceSpace->seqNums[j].data.size();
    j++;
  }

  return;
}

void sendPackets(AckSpace* sequenceSpace, int windowSize, int sockfd,
                 struct sockaddr* destAddr, socklen_t destLen)
{
  int i, n, diff;
  struct timeval now;
  string curPacket;
  pair<int, struct timeval> indexTime;

  while (sequenceSpace->windowSize < windowSize &&
         sequenceSpace->nextSeq < (int)sequenceSpace->seqNums.size())
  {
    i = sequenceSpace->nextSeq;
    curPacket = sequenceSpace->seqNums[i].data;
    diff = windowSize - sequenceSpace->windowSize;

    if ((int)curPacket.size() > diff)
      break;
    n = sendMsg(sockfd, curPacket.c_str(), curPacket.size(), 0, destAddr,
                destLen);
    if (n < 0)
    {
      perror("* ERROR sending to client address");
      exit(1);
    }
    gettimeofday(&now, NULL);
    indexTime = make_pair(i, now);
    sequenceSpace->sentUnacked.push_back(indexTime);
    fprintf(stdout, "* Sent SEQ %d\n", sequenceSpace->seqNums[i].sequence);

    sequenceSpace->windowSize += curPacket.size();
    sequenceSpace->nextSeq++;
  }
  return; // TODO: Might want to return the number of packets sent
}

void catchAlarm(int signal)
{
  fprintf(stdout, "*\n");
  fprintf(stdout, "* Checking for timeouts...\n");
  checkTimeout();
  alarm(ALARM_TIME);
  fprintf(stdout, "*\n");
}

void checkTimeout()
{
  suseconds_t diff;
  int i, n, index, sockfd, nTimedOut;
  struct timeval now;
  struct sockaddr* destAddr;
  socklen_t destLen;
  string packetData;
  vector<pair<int, struct timeval> > resent;
  list<pair<int, struct timeval> >::iterator it;

  it = clientReq->sequenceSpace.sentUnacked.begin();
  sockfd = clientReq->clientInfo.sockfd;
  destAddr = clientReq->clientInfo.address;
  destLen = clientReq->clientInfo.length;
  nTimedOut = 0;

  while (it != clientReq->sequenceSpace.sentUnacked.end())
  {
    index = it->first;
    packetData = clientReq->sequenceSpace.seqNums[index].data;

    gettimeofday(&now, NULL);
    diff = 
      (1E6 * (now.tv_sec - it->second.tv_sec)) +
      (now.tv_usec - it->second.tv_usec);

    if (diff >= ACK_TIMEOUT) // Resend packet
    {
      fprintf(stdout, "* Timeout for SEQ %d!\n",
        clientReq->sequenceSpace.seqNums[index].sequence);
      nTimedOut++;
      n = sendMsg(sockfd, packetData.c_str(), packetData.size(), 0, destAddr,
                  destLen);
      if (n < 0)
      {
        perror("ERROR sending to client address");
        exit(1);
      }
      
      gettimeofday(&now, NULL);
      it = clientReq->sequenceSpace.sentUnacked.erase(it);
      resent.push_back(make_pair(index, now));
      continue;
    }

    it++;
  }

  for (i = 0; i < (int)resent.size(); i++)
    clientReq->sequenceSpace.sentUnacked.push_back(resent[i]);

  return;
}
