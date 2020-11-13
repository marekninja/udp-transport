#define _CRT_SECURE_NO_WARNINGS
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <stdio.h>
#include <WinSock2.h>
#include <time.h>
#include <Windows.h>
#include <strsafe.h>
#include <tchar.h>
#include <string.h>
#define HEADER_SIZE 10
#define ID_FILE 2
#define ID_TEXT 1
#define ID_CON 0
#define T_CON 0
#define T_INIT 1
#define T_DATA 2
#define T_ACK 3
#pragma comment(lib, "Ws2_32.lib")



typedef struct timer {
	time_t start;
	time_t end;
	time_t count;
} TIMER;

typedef struct proto {
	//CON = 0, INIT = 1, DATA = 2, ACK = 3
	BYTE type;
	//CON = 0, INIT = 1/2 text/file, DATA = rand, ACK = equal
	BYTE id;
	DWORD32 fragNum;
	WORD checksum;
	WORD dataLen;
	char *data;
} OWN_PROTO;

TIMER	*clientTimer;
HANDLE	serverThread, clientThread;
int		remotePort, listenPort, fragmentSize;
char	dataToSend[1024];
BOOL	sendFile;
char	remoteIpAddr[16];
long	tvUsecACKtimeout = 800000; //in MICROseconds , is 0.8sec
int		mistakeChance = 0;
int		mistakesCount = 0;
int		mistakesMax = 5;

float getTime(TIMER *timer);
float startTimer();
DWORD WINAPI runTimer(LPVOID *args);
int recvfromTimeOutUDP(SOCKET socket, long sec, long usec);
DWORD WINAPI runServer(LPVOID args);
DWORD WINAPI runClient(LPVOID args);
OWN_PROTO *createCON();
OWN_PROTO *createACK(BYTE id, DWORD fragNum);
OWN_PROTO *createINIT(BYTE id, DWORD fragNum, FILE *f, char *fileName);
OWN_PROTO *createDATA(BYTE id, DWORD fragNum, char *data, WORD dataLen);
WORD calcChecksum(char *preparedOwnProto, int ownProtoLen);
char *createFrame(OWN_PROTO *ownProtocolFilled);
OWN_PROTO *readFrame(char *receivedFrame);
void repairFragmentSize();
void setClient();
void setServer();
char getMode();
void uberFlush();
DWORD getProtLen(char *receivedBuf);
int *getMissingFromAck(OWN_PROTO *ackRecv);
char getOneChar();
char *makeMistake(char *filledFrame);

int _tmain() {
	WSADATA wsaData;

	if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
		printf("WSAStartup failed with error: %d\n", WSAGetLastError());
		return 1;
	}
	else {
		printf("Current status is: %s.\n", wsaData.szSystemStatus);
	}

	//check if the ws version is 2.2
	if (LOBYTE(wsaData.wVersion) != 2 || HIBYTE(wsaData.wVersion) != 2) {
		printf("Winsock version %u.%u not supported...\n", LOBYTE(wsaData.wVersion), HIBYTE(wsaData.wVersion));
		WSACleanup();
		return 1;
	}

	srand(time(0));

	//server session should be always running
	//from the task, there will always be only one sided sending
	//that means, there will be no need to send via client, while also receiving as server
	//so every time I call send, I can create new client with exact same parameters


	//runServer();	 //should be running always
	//so it listens by default ??

	//setServer();

	//serverThread = CreateThread(NULL, 0, runServer, 0, 0, 0);
	//if (serverThread == NULL) {
	//	printf("Error %d, while creating Server Thread!\n", GetLastError());
	//	WSACleanup();
	//	return 1;
	//};

	//WaitForSingleObject(serverThread, 40);
			//float time = 5;
	//HANDLE timerThread = CreateThread(NULL, 0, runTimer, 0, 0, 0);

	char devMode = ' ';
	
	while (devMode != 'Q') {

		devMode = getMode();

		//at any time needs to have 2 sessions open - server, client
		//this works just as switch, 
		if (devMode == 'R') {
			//new receive mode
			//listening socket == server
			//zatvorit client thread :/
			setServer();
			TerminateThread(clientThread,0);
			TerminateThread(serverThread, 0);
			serverThread = CreateThread(NULL, 0, runServer, 0, 0, 0);
			if (serverThread == NULL) {
				printf("Error %d, while creating Server Thread!\n", GetLastError());
				WSACleanup();
				return 1;
			}
			//HANDLE timerThread = CreateThread(NULL, 0, runTimer, 0, 0, 0);
			WaitForSingleObject(serverThread, INFINITE);
			TerminateThread(serverThread, 0);

		}
		if (devMode == 'S') {
			//new send mode 
			//sending socket == client
		
			setClient();
			TerminateThread(clientThread, 0);
			TerminateThread(serverThread, 0);

			clientThread = CreateThread(NULL, 0, runClient, 0, 0, 0);
			if ( clientThread == NULL) {
				printf("Error %d, while creating Server Thread!\n", GetLastError());
				WSACleanup();
				system("pause");
				return 1;
			}
			WaitForSingleObject(clientThread, INFINITE);
			TerminateThread(clientThread, 0);
			//runClient();
		}
	}
	
	//printf("server %d client %d", serverThread, clientThread);

	TerminateThread(clientThread,0);
	TerminateThread(serverThread,0);

	if (WSACleanup() == SOCKET_ERROR) {
		printf("WSACleanup failed with error: %d\n", WSAGetLastError());
		return 1;
	}
	system("pause");
	return 0;
}



float getTime(TIMER *timer) {
	timer->count = (timer->end - timer->start) / CLOCKS_PER_SEC;
	return 4;
}

float startTimer() {
	return clock();
}

DWORD WINAPI runTimer(LPVOID *args) {
	clientTimer = (TIMER*)malloc(sizeof(TIMER));
	clientTimer->start = startTimer();
	while (clientTimer->count < 60000) {
		getTime(clientTimer);
		printf("time %f", clientTimer->count);
	}
	return 4;
}

//returns 0 if timeout, -1 if error, >0 data received, can read
int recvfromTimeOutUDP(SOCKET socket, long sec, long usec) {
	TIMEVAL timeout;
	timeout.tv_sec = sec;
	timeout.tv_usec = usec;
	fd_set fds;
	FD_ZERO(&fds);
	FD_SET(socket, &fds);

	return select(0, &fds, 0, 0, &timeout);
}

//always listens
DWORD WINAPI runServer(LPVOID args) {
	printf("Created server...\n");
	WSADATA            wsaData;
	SOCKET             ReceivingSocket;
	SOCKADDR_IN        ReceiverAddr;

	int                Port = listenPort;
	char          ReceiveBuf[1472];
	int                BufLength = 1472;
	SOCKADDR_IN        SenderAddr;
	int                SenderAddrSize = sizeof(SenderAddr);
	int                ByteReceived = 5;

	// Initialize Winsock version 2.2
	if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
	{
		printf("Server: WSAStartup failed with error %ld\n", WSAGetLastError());
		return -1;
	}
	else
		printf("Server: The Winsock DLL status is %s.\n", wsaData.szSystemStatus);

	// Create a new socket to receive datagrams on.
	ReceivingSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

	if (ReceivingSocket == INVALID_SOCKET)
	{
		printf("Server: Error at socket(): %ld\n", WSAGetLastError());
		// Clean up
		WSACleanup();
		// Exit with error
		return -1;
	}
	else
		printf("Server: socket() is OK!\n");

	// Set up a SOCKADDR_IN structure that will tell bind that we
	// want to receive datagrams from all interfaces using port 5150.
	// The IPv4 family

	ReceiverAddr.sin_family = AF_INET;

	// Port no. 5150
	ReceiverAddr.sin_port = htons(Port);

	// From all interface (0.0.0.0)
	ReceiverAddr.sin_addr.s_addr = htonl(INADDR_ANY);

	// Associate the address information with the socket using bind.
	// At this point you can receive datagrams on your bound socket.
	if (bind(ReceivingSocket, (SOCKADDR *)&ReceiverAddr, sizeof(ReceiverAddr)) == SOCKET_ERROR)
	{
		printf("Server: bind() failed! Error: %ld.\n", WSAGetLastError());
		// Close the socket
		closesocket(ReceivingSocket);
		// Do the clean up
		WSACleanup();
		// and exit with error
		return -1;
	}
	else
		printf("Server: bind() is OK!\n");

	//// Some info on the receiver side...
	//getsockname(ReceivingSocket, (SOCKADDR *)&ReceiverAddr, (int *)sizeof(ReceiverAddr));

	////printf("Server: Receiving IP(s) used: %s\n", inet_ntop(AF_INET,ReceiverAddr.sin_addr,));
	//printf("Server: Receiving port used: %d\n", htons(ReceiverAddr.sin_port));

	printf("Server: I\'m ready to receive a datagram...\n");

	// At this point you can receive datagrams on your bound socket.
	for (int i = 0; i < 5; i++ ) {
		ByteReceived += recvfrom(ReceivingSocket, ReceiveBuf, BufLength, 0, (SOCKADDR *)&SenderAddr, &SenderAddrSize);
		OWN_PROTO *actProto = readFrame(ReceiveBuf);
		
		if (actProto->checksum == calcChecksum(ReceiveBuf,getProtLen(ReceiveBuf))) {
			//if CON
			if (actProto->type == T_CON) {
				printf("Server: Received connection request\n");

				getpeername(ReceivingSocket, (SOCKADDR *)&SenderAddr, &SenderAddrSize);


				OWN_PROTO *sendAck = createACK(actProto->id, actProto->fragNum);
				char *send = createFrame(sendAck);
				sendAck->checksum = calcChecksum(send, getProtLen(send));
				send = createFrame(sendAck);

				//char SendBuf[1024] = "thank you for the music";
				//int SendBufLength = 1024;
				sendto(ReceivingSocket, send, getProtLen(send), 0,
					(SOCKADDR *)&SenderAddr, sizeof(SenderAddr));
				break;
				
			}
		}
		else {
			printf("Server: Checksum not equal!\n");
			if (i == 4) {
				printf("Server: Closing connection\n");
				// Close the socket
				closesocket(ReceivingSocket);
				// Do the clean up
				WSACleanup();
				// and exit with error
				return -1;
			}
		}

	}
	
	OWN_PROTO *initProto = NULL;
	for (int i = 0; i < 5; i++) {
		//wait for INIT
		int ret;
		ret = recvfrom(ReceivingSocket, ReceiveBuf, BufLength, 0, (SOCKADDR *)&SenderAddr, &SenderAddrSize);
		
		if (ret < 0) {
			printf("Server: Error %d, while receiving message\n", WSAGetLastError());
		}
		else {
			initProto = readFrame(ReceiveBuf);

			printf("Server: got message, is it INIT?\n");

			if (initProto->checksum == calcChecksum(ReceiveBuf, getProtLen(ReceiveBuf))) {
				if (initProto->type == T_INIT) {
					printf("Server: Received INIT request\n");

					getpeername(ReceivingSocket, (SOCKADDR *)&SenderAddr, &SenderAddrSize);


					OWN_PROTO *sendAck = createACK(initProto->id, initProto->fragNum);
					char *send = createFrame(sendAck);
					sendAck->checksum = calcChecksum(send, getProtLen(send));
					send = createFrame(sendAck);

					sendto(ReceivingSocket, send, getProtLen(send), 0, (SOCKADDR *)&SenderAddr, sizeof(SenderAddr));
					break;
				}
			}
			else {
				printf("Server: Checksum not equal!\n");
				if (i == 4) {
					closesocket(ReceivingSocket);
					WSACleanup();
					return -1;
				}
			}
		}
	}

	//calculate how many fragments to receive, and groups
	int numOfFragments;
	int numOfSets;
	char *filename = NULL;
	if (initProto->id == ID_FILE) {
		//HANDLE FILE
		numOfFragments = initProto->fragNum;
		numOfSets = numOfFragments / 200;
		if (numOfSets * 200 != numOfFragments) numOfSets++;
		filename = initProto->data;
		filename[initProto->dataLen] = '\0';
	}
	if (initProto->id == ID_TEXT) {
		//HANDLE TEXT
		numOfFragments = initProto->fragNum;
		numOfSets = numOfFragments / 200;
		if (numOfSets * 200 != numOfFragments) numOfSets++;

	}
	printf("Server: Ready to receive %d fragments.\n", numOfFragments);

	OWN_PROTO **checkedProts = (OWN_PROTO **)malloc(numOfFragments * sizeof(OWN_PROTO*));
	OWN_PROTO **setInProg = (OWN_PROTO **)malloc(200 * sizeof(OWN_PROTO*));
	//to be able to check for NULL
	for (int i = 0; i < 200; i++)setInProg[i] = NULL;
	//memset(setInProg, 0, sizeof(OWN_PROTO*));

	int checkedCount = 0;
	int setCount = 0;
	WORD countMissing = 0;
	while (1) {
		//handle DATA somehow
		//receive first
		BYTE ackID = 4;
		//int ret;
		int count = 0;
		char **fragments = (char**)malloc(200*sizeof(char*));
		int inThisSet; //= numOfFragments - (setCount * 200);
		if (countMissing == 0) {
			if (setCount == numOfSets - 1) {
				inThisSet = numOfFragments % 200;
			}
			else {
				inThisSet = 200;
			}
		}
		else {
			inThisSet = countMissing;
		}
		
		while (1) {
			int ret;
			//ret = recvfromTimeOutUDP(ReceivingSocket, 1, tvUsecACKtimeout);
			ret = recvfromTimeOutUDP(ReceivingSocket, 2, 0);
			//ret = 1;
			if (ret == 0) {
				printf("Server: Client timeout\n");
				//that means lost fragments, gotta ask for them
				//or end of receiving
				break;
			}
			if (ret == -1) {
				printf("Server: Error %d, while receiving DATA\n", WSAGetLastError());
			}
			if (ret > 0) {
				int check;
				//printf("Server: Receiving DATA\n");
				memset(ReceiveBuf, 0, BufLength);
				check = recvfrom(ReceivingSocket, ReceiveBuf, BufLength, 0, (SOCKADDR *)&SenderAddr, &SenderAddrSize);
				//check = recv(ReceivingSocket, ReceiveBuf, BufLength, 0);
				if (check < 0) {
					printf("Server: Error %d receiving DATA\n", WSAGetLastError());
				}
				else {

					OWN_PROTO *actProto =readFrame(ReceiveBuf);
					if (actProto->checksum == calcChecksum(ReceiveBuf, getProtLen(ReceiveBuf))) {
						if (actProto->type == T_DATA) {
							setInProg[actProto->fragNum % 200] = actProto;
							ackID = actProto->id;
							count++;
						}
					}
					else {
						printf("Server: Checksum not equal!\n");
						count++;
					}
					
				}
				if (count == inThisSet) {
					printf("Server: Got set %d...\n",setCount);
					break;
				}
			}

		}
		
		//control then
		//kontrola vsetkych prijatych fragmentov

		countMissing = 0;
		int check = 0;
		int missing[200];
		printf("Server: Got %d fragments\n", count);

		//if (setCount == 0) inThisSet = numOfFragments % 200;
		 
		//tu by som mal zistit chybajuce
		for (int i = 0; i < inThisSet; i++) {
			if (setInProg[i] == NULL  || setInProg[i]->data == NULL) {
				missing[countMissing++] = setCount * numOfFragments + i;
				//printf("missing is %d\n", missing[countMissing - 1]);
			}
		}

		//a poslat ACK
		getpeername(ReceivingSocket, (SOCKADDR *)&SenderAddr, &SenderAddrSize);


		OWN_PROTO *sendAck = createACK(ackID, countMissing);
		sendAck->dataLen = countMissing * 4;
		sendAck->data = (char *)malloc(countMissing * sizeof(DWORD));
		for (DWORD i = 0; i < countMissing; i++) {
			*((DWORD*)sendAck->data+i) = missing[i];
		}
		char *send = createFrame(sendAck);
		sendAck->checksum = calcChecksum(send, getProtLen(send));
		send = createFrame(sendAck);

		sendto(ReceivingSocket, send, getProtLen(send), 0, (SOCKADDR *)&SenderAddr, sizeof(SenderAddr));
		
		//ak mam osetrene a bezchybnych tychto 200 tak, idem na dalsi set
		if (countMissing == 0) {
			setCount++;
			printf("Server: SUCCES! Set complete\n");
			//MEMSET NOT WORKING
			for (int i = 0; i < 200; i++) {
				if (setInProg[i] == NULL) break;
				checkedProts[checkedCount++] = setInProg[i];
			}
			for (int j = 0; j < 200; j++)setInProg[j] = NULL;
			//memset(setInProg, 0, sizeof(OWN_PROTO*));
			if (setCount == numOfSets) break;
		}
		
	}

	int fragSize = 0;

	printf("Server: finished receiving\n");
	if (initProto->id == ID_FILE) {
		//here should be path to file created;
		FILE *fileRecv = fopen(filename, "wb+");
		
		for (int i = 0; i < numOfFragments; i++) {
			for (int j = 0; j < checkedProts[i]->dataLen; j++) {
				if (checkedProts[i]->dataLen > fragSize) fragSize = checkedProts[i]->dataLen;
				//printf("fragNum %d data %.2x\n", checkedProts[i]->fragNum, checkedProts[i]->data[j]);

				BYTE c = checkedProts[i]->data[j];
				//printf("%.2x", c);
				fwrite(&c, sizeof(BYTE), 1, fileRecv);
			}
			/*putchar(' ');
			if (i % 12 == 0) putchar('\n');*/
			//printf("%d data is: %s\n", checkedProts[i]->fragNum, checkedProts[i]->data);
		}
		fclose(fileRecv);
		//FILE *fileRecv2 = fopen("pokus.png", "wb+");
		//for (int i = 0; i < numOfFragments;i++) {
		//	if (checkedProts[i] == NULL) {
		//		printf("Checked prots %d is null = MISSING", i);
		//		system("pause");
		//	}
		//	fwrite(&(checkedProts[i]->data), checkedProts[i]->dataLen, 1, fileRecv2);
		//}
		//fclose(fileRecv2);
		//fclose(fileRecv);
		//fclose(sendFile);
		//putchar('\n');
	}
	if (initProto->id == ID_TEXT) {
		printf("Received message is: ");
		for (int i = 0; i < numOfFragments; i++) {
			for (int j = 0; j < checkedProts[i]->dataLen; j++) {
				if (checkedProts[i]->dataLen > fragSize) fragSize = checkedProts[i]->dataLen;
				//printf("fragNum %d data %.2x\n",checkedProts[i]->fragNum ,checkedProts[i]->data[j])
				putchar(checkedProts[i]->data[j]);
			}
			//printf("%d data is: %s\n", checkedProts[i]->fragNum, checkedProts[i]->data);
		}
		putchar('\n');
	}
	
	printf("Server: Size of fragment was: %d\n", fragSize);

	//// Some info on the sender side

	// When your application is finished receiving datagrams close the socket.
	printf("Server: Finished receiving. Closing the listening socket...\n");
	if (closesocket(ReceivingSocket) != 0)
		printf("Server: closesocket() failed! Error code: %ld\n", WSAGetLastError());
	else
		printf("Server: closesocket() is OK\n");

	// When your application is finished call WSACleanup.
	printf("Server: Cleaning up...\n");
	if (WSACleanup() != 0)
		printf("Server: WSACleanup() failed! Error code: %ld\n", WSAGetLastError());
	else
		printf("Server: WSACleanup() is OK\n");

	// Back to the system
	//return 0;
	return 0;
}

//creates new on user's call
DWORD WINAPI runClient(LPVOID args) {
	printf("Created client session...\n");
	WSADATA		wsaData;
	SOCKET		SendingSocket;
	SOCKADDR_IN	ReceiverAddr, SrcInfo;
	int			Port = remotePort; //= 5150;
	//char		*SendBuf = dataToSend;
	//int			BufLength = 1462;	 				
	int len;
	int TotalByteSent;

	// Initialize Winsock version 2.2
	if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
	{
		printf("Client: WSAStartup failed with error %ld\n", WSAGetLastError());
		// Clean up
		WSACleanup();
		// Exit with error
		return -1;
	}
	else
		printf("Client: The Winsock DLL status is %s.\n", wsaData.szSystemStatus);


	// Create a new socket to send datagrams to.
	SendingSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (SendingSocket == INVALID_SOCKET)
	{
		printf("Client: Error at socket(): %ld\n", WSAGetLastError());
		// Clean up
		WSACleanup();
		// Exit with error
		return -1;
	}
	else
		printf("Client: socket() is OK!\n");


	ReceiverAddr.sin_family = AF_INET;
	ReceiverAddr.sin_port = htons(Port);
	ReceiverAddr.sin_addr.s_addr = inet_addr(remoteIpAddr);


	//if (connect(SendingSocket, (SOCKADDR *)&ReceiverAddr, sizeof(ReceiverAddr)) == INVALID_SOCKET)
	//{
	//	printf("Client: bind() failed! Error: %ld.\n", WSAGetLastError());
	//	// Close the socket
	//	closesocket(SendingSocket);
	//	// Do the clean up
	//	WSACleanup();
	//	// and exit with error
	//	return -1;
	//}
	//else
	//	printf("Client: bind() is OK!\n");


	// Send a datagram to the receiver.
	//printf("Client: Data to be sent: \"%s\"\n", SendBuf);
	printf("Client: Sending datagrams...\n");

	//here goes sending loop
	//was mistake in design, changed states 1 and 2 between Server/Client
	//1. send request for connection CON, rand ID
	//2. wait for ACK, equal ID
	//3. send INIT frame with right ID 1/2 mess/file
	//4. wait for ACK, equal ID
	//5. send fragments, in groups of 200
	//6. wait for ACK - handle missing frags
	//				  - back to 5., continue sending, till all

	TotalByteSent = 0;

	char RecvBuf[1462];
	int RecvBufLength = 1462;

	//buÔ for cyklus, alebo na kaûd˙ poûiadavku Ëakaù t˝ch vyhraden˝ch 60s
	//teda m·m zaruËenÈ, ûe aj pri dlhom v˝padku spojenia, to poölem

	//STAVY CLIENTA A SERVERA VIEM JASNE URCIT A MOZEM NIMI CHODIT VO WHILE CYKLE S PODMIENKAMI
	//TO JE VHODNA NAHRADA, ABY SOM NEPOUZIVAL GOTO

	OWN_PROTO *ownCon = createCON();
	char *readyCon = createFrame(ownCon);
	printf("Client, estab. con: ");
	for (int i = 0; i < 50; i++) {
		printf("%u", readyCon[i]);
	}
	putchar('\n');

	ownCon->checksum = calcChecksum(readyCon,HEADER_SIZE);
	readyCon = createFrame(ownCon);
	
	//printf("Client, estab. con: %s\n", readyCon);

	printf("Client: sending CON\n");
	for (int i = 0; i < 5; i++) {
		//send CON
		//MIND THE TIMEOUT

		
		TotalByteSent += sendto(SendingSocket, makeMistake(readyCon), HEADER_SIZE, 0, (SOCKADDR *)&ReceiverAddr, sizeof(ReceiverAddr));

		int time = recvfromTimeOutUDP(SendingSocket, 0, tvUsecACKtimeout);
		if (time == 0) {
			//timeout
			printf("Client: Server response timed out num: %d, has max 5 tries...\n", i + 1);
			if (i == 4) {
				//system("pause");
				printf("Client: Server not online, closing connection...\n");
				return -1;
			}
			
		}
		if (time == -1) {
			//error ocured
			printf("Error ocured, while receiving response... %d\n", WSAGetLastError());
			return -1;
		}
		if (time > 0) {
			//can read
			//getsockname(SendingSocket, (SOCKADDR *)&SrcInfo, &len);
			//getpeername(SendingSocket, (SOCKADDR *)&ReceiverAddr, (int *)sizeof(ReceiverAddr));

			SOCKADDR_IN checkSender;
			if (recv(SendingSocket, RecvBuf, RecvBufLength, 0) < 0) {
				printf("Error receiving response %d\n", WSAGetLastError());
			}
			else {
				//get ACK, control ACK
			//check checksum here
				OWN_PROTO *actProto = readFrame(RecvBuf);

				if (actProto->checksum == calcChecksum(RecvBuf, getProtLen(RecvBuf))) {
					if (actProto->type == T_ACK && actProto->id == ownCon->id) {
						printf("Client: Confirmed connection request\n");
						//printf("Client, got response: %s\n", actProto);
						break;
					}
				}
				else {
					printf("Client: Response checksum not right!\n");
				}
			}

		}
	}

	int numOfFrags = 0;
	BYTE initID;
	FILE *file = NULL;
	char *filename = NULL;
	int fileSize = 0;
	if (sendFile == TRUE) {
		//handle file fragments
		//file = fopen("C:/Users/petro/Desktop/field-beauty-mountain-nature-flower-fields-natures-hd-background-1280x768.jpg", "rb");
		file = fopen(dataToSend, "rb");
		if (file == NULL) {
			printf("Client: Error opening file!\n");
			return -1;
		}

		char c = '/';
		filename = strrchr(dataToSend, c)+1;

		fseek(file, 0, SEEK_END);
		fileSize = ftell(file);
		rewind(file);

		numOfFrags = (fileSize / fragmentSize);
		if (fragmentSize * numOfFrags != fileSize) numOfFrags++;

		printf("Client: File opened succesfully! Has sizeL %d\n",fileSize);
		//char 

		initID = ID_FILE;
	};
	if (sendFile == FALSE) {
		//handle text message
		printf("Message is \n%s\n", dataToSend);
		numOfFrags = (strlen(dataToSend)/fragmentSize);
		if (fragmentSize * numOfFrags != strlen(dataToSend)) numOfFrags ++;
		printf("Client: Length of message %s is %d\n", dataToSend, strlen(dataToSend));
		initID = ID_TEXT;
	};

	OWN_PROTO *init = createINIT(initID, numOfFrags, file, filename);
	char *sendInit = createFrame(init);
	//POZOR CHECKSUM V OPACNOM PORADI MA BAJTY
	init->checksum = calcChecksum(sendInit, getProtLen(sendInit));
	sendInit = createFrame(init);

	printf("Client: sending INIT\n");
	//send INIT
	for (int i = 0; i < 5; i++) {

		TotalByteSent += sendto(SendingSocket, makeMistake(sendInit), getProtLen(sendInit), 0, (SOCKADDR *)&ReceiverAddr, sizeof(ReceiverAddr));

		int time = recvfromTimeOutUDP(SendingSocket, 0, tvUsecACKtimeout);
		if (time == 0) {
			//timeout
			printf("Server response timed out num: %d, has max 5 tries...\n", i + 1);
			if (i == 4) break;
		}
		if (time == -1) {
			//error ocured
			printf("Error ocured, while receiving response... %d", WSAGetLastError());
			return -1;
		}
		if (time > 0) {
			//can read
			//get ACK, control ACK 
			SOCKADDR_IN checkSender;
			if (recv(SendingSocket, RecvBuf, RecvBufLength, 0) < 0) {
				printf("Error receiving response %d\n", WSAGetLastError());
			}
			else {
				//get ACK, control ACK
				//check checksum here
				OWN_PROTO *actProto = readFrame(RecvBuf);

				if (actProto->checksum == calcChecksum(RecvBuf, getProtLen(RecvBuf))) {
					if (actProto->type == T_ACK && actProto->id == init->id) {
						printf("Client: Confirmed INIT\n");
						//printf("Client, got response: %s\n", actProto);
						break;
					}
				}
				else {
					printf("Client: Response checksum not right!\n");
				}
			}
		}
	}

	//TU UZ POSIELAM DATA
	int succesfulSet = 0;
	//int fragCount; //= count num of fragment sets here...

	//OWN_PROTO **fragments = (OWN_PROTO **)malloc(numOfFrags * sizeof(OWN_PROTO*));
	char **fragments = (char **)malloc(numOfFrags * sizeof(char *));
	BYTE idData = rand() & 256;

	if (sendFile == TRUE) {
		//count num of fragments based on file size
		//prepare file fragments here
		FILE *fcheck;
		fcheck = fopen(filename,"wb+");
		for (int i = 0; i < numOfFrags; i++) {
			WORD dataLen = 0;
			char *fragData = (char*)malloc(fragmentSize * sizeof(char));
			for (int j = 0; j < fragmentSize;j++) {
				int check = fread(&fragData[j], 1, 1, file);
				if (check != 0) dataLen++;
				if (check == 0) break;
			}
			
			fwrite(&fragData, fragmentSize, 1, fcheck);

			//printf("now i have %s\n", fragData);
			OWN_PROTO *actFrag = createDATA(idData, i, fragData,dataLen);
			char *actFrame = createFrame(actFrag);
			actFrag->checksum = calcChecksum(actFrame, getProtLen(actFrame));
			actFrame = createFrame(actFrag);

			fragments[i] = actFrame;
			//fragments[i] = actFrag;
		}
		fclose(fcheck);
		rewind(file);

	}
	if (sendFile == FALSE) {
		//prepare text message fragments here
		int dataLen = strlen(dataToSend);

		for (int i = 0; i < numOfFrags; i++) {
			char *fragData = (char*)malloc(fragmentSize * sizeof(char));
			
			WORD fragLen = 0;
			for (int j = i*fragmentSize; j < i * fragmentSize+fragmentSize; j++) {
				if (j >= dataLen) break;
				fragData[j-i*fragmentSize] = dataToSend[j];
				fragLen++;
			}


			printf("now i have %s\n", fragData);
			OWN_PROTO *actFrag = createDATA(idData, i, fragData, fragLen);
			char *actFrame = createFrame(actFrag);
			actFrag->checksum = calcChecksum(actFrame,getProtLen(actFrame));
			actFrame = createFrame(actFrag);

			fragments[i] = actFrame;
			//fragments[i] = actFrag;
		}
	}
	int numOfSets = numOfFrags / 200;
	if (numOfSets * 200 != numOfFrags) numOfSets++;

	int missing[200];
	for (int i = 0; i < 200; i++) missing[i] = 1;
	//memset(&missing, 1, sizeof(int));

	printf("Client: Sending data\n");

	while (1) {
		//send DATA
		for (int j = 0; j < numOfSets; j++) {
			while (1) {
				
				int inSet; //= numOfFrags - j * 200;
				if (j == numOfSets - 1) {
					inSet = numOfFrags % 200;
				}
				else {
					inSet = 200;
				}

				for (int i = 0; i < inSet; i++) {
					//if (j * 200 + i >= numOfFrags) break;
					if (missing[i] == 1) {
						int check;
						char *toSend = fragments[i + j * 200];
						toSend = makeMistake(toSend);
						//for (int k = 0; k < 1000000; k++);
						check = sendto(SendingSocket, toSend, getProtLen(toSend), 0, (SOCKADDR *)&ReceiverAddr, sizeof(ReceiverAddr));

						if (check == SOCKET_ERROR) {
							printf("Client: Error %d sending fragments", WSAGetLastError());
						}
					}
					

					//printf("Client: sent frag %d\n", i);
				}
				//tu cakam a spracuvam ack
				int time = recvfromTimeOutUDP(SendingSocket, 2, 0);
				if (time == 0) {
					//timeout
					printf("Server response timed out, has max 5 tries...\n");
					//if (i == 4) break;
				}
				if (time == -1) {
					//error ocured
					printf("Error ocured, while receiving response... %d", WSAGetLastError());
					//return -1;
				}
				if (time > 0) {
					//can read
					//get ACK, control ACK 
					SOCKADDR_IN checkSender;
					if (recv(SendingSocket, RecvBuf, RecvBufLength, 0) < 0) {
						printf("Error receiving response %d\n", WSAGetLastError());
					}
					else {
						//get ACK, control ACK
						//check checksum here
						OWN_PROTO *actProto = readFrame(RecvBuf);

						if (actProto->checksum == calcChecksum(RecvBuf, getProtLen(RecvBuf))) {
							if (actProto->type == T_ACK && actProto->id == idData) {
								//printf("Client: Got ACK\n");
								
								if (actProto->fragNum == 0) {
									break;
								}
								if (actProto->fragNum != 0) {
									int countMissing = 0;
									//memset(&missing, 0, sizeof(int));
									for (int i = 0; i < 200; i++) missing[i] = 0;

									for (DWORD i = 0; i < actProto->dataLen / sizeof(DWORD); i++) {
										//missing[countMissing++] = *(DWORD*)actProto->data[i];
										int ind = *((DWORD*)actProto->data+i) % 200;
										missing[ind] = 1;
									}
								}
							}
						}
						else {
							printf("Client: Response checksum not right!\n");
						}
					}
				}
			}
			

		}
		printf("Client: All sent.\n");
		break;
	}


	//for (int i = 0; i < numOfFrags; i++) {
	//	for (int j = 0; j < fileSize; j++) {
	//		char c[1];
	//		fread(&c, 1, 1, file);
	//		printf("%.2x", c);
	//		//fwrite(&c, 1, 1, fileRecv);
	//	}
	//	putchar(' ');
	//	if (i % 12 == 0) putchar('\n');
	//	//printf("%d data is: %s\n", checkedProts[i]->fragNum, checkedProts[i]->data);
	//}


	printf("Client: sendto() looks OK!\n");

	// Some info on the receiver side...
	// Allocate the required resources
	memset(&SrcInfo, 0, sizeof(SrcInfo));
	len = sizeof(SrcInfo);

	getsockname(SendingSocket, (SOCKADDR *)&SrcInfo, &len);
	//printf("Client: Sending IP(s) used: %s\n", inet_ntop(SrcInfo.sin_addr));
	printf("Client: Sending port used: %d\n", htons(SrcInfo.sin_port));


	// Some info on the sender side
	getpeername(SendingSocket, (SOCKADDR *)&ReceiverAddr, (int *)sizeof(ReceiverAddr));
	//	printf("Client: Receiving IP used: %s\n", inet_ntop(ReceiverAddr.sin_addr));
	printf("Client: Receiving port used: %d\n", htons(ReceiverAddr.sin_port));
	printf("Client: Total byte sent: %d\n", TotalByteSent);

	// When your application is finished receiving datagrams close the socket.
	printf("Client: Finished sending. Closing the sending socket...\n");
	if (closesocket(SendingSocket) != 0)
		printf("Client: closesocket() failed! Error code: %ld\n", WSAGetLastError());
	else
		printf("Client: closesocket() is OK\n");



	// When your application is finished call WSACleanup.
	printf("Client: Cleaning up...\n");
	if (WSACleanup() != 0)
		printf("Client: WSACleanup() failed! Error code: %ld\n", WSAGetLastError());
	else
		printf("Client: WSACleanup() is OK\n");

	// Back to the system
	//return 0;
	//ResumeThread(serverThread);
	return 0;
}
//not complete, needs checksum
OWN_PROTO *createCON() {
	OWN_PROTO *reqCon = (OWN_PROTO*)malloc(sizeof(OWN_PROTO));
	reqCon->type = T_CON;
	reqCon->id = ID_CON;
	reqCon->fragNum = 0;
	reqCon->dataLen = 0;
	reqCon->data = 0;
	reqCon->checksum = 0;
	return reqCon;
}

//not complete, needs checksum/data
OWN_PROTO *createACK(BYTE id, DWORD fragNum) {
	OWN_PROTO *sendACK = (OWN_PROTO*)malloc(sizeof(OWN_PROTO));
	sendACK->type = T_ACK;
	sendACK->id = id;
	sendACK->fragNum = fragNum;
	sendACK->dataLen = 0;
	sendACK->checksum = 0;
	return sendACK;
}

//not complete, needs checksum/data
//ID = 2 if TEXT MESSAGE, ID = 1 if FILE
OWN_PROTO *createINIT(BYTE id, DWORD fragNum, FILE *f, char *fileName) {
	OWN_PROTO *sendINIT = (OWN_PROTO*)malloc(sizeof(OWN_PROTO));
	sendINIT->type = T_INIT;
	sendINIT->id = id;
	sendINIT->fragNum = fragNum;
	//handle if file, then data = filename.format
	if (id == ID_FILE) {
		sendINIT->dataLen = strlen(fileName);
		//sendINIT->data = fileName;
		sendINIT->data = (char *)malloc(sendINIT->dataLen * sizeof(char));
		for (int i = 0; i < sendINIT->dataLen; i++) {
			sendINIT->data[i] = *(fileName + i);
		}
	}
	if (id == ID_TEXT) {
		sendINIT->data = 0;
		sendINIT->dataLen = 0;
	}

	sendINIT->checksum = 0;
	return sendINIT;
}

//not complete, needs checksum/data
OWN_PROTO *createDATA(BYTE id, DWORD fragNum, char *data, WORD dataLen ) {
	OWN_PROTO *sendDATA = (OWN_PROTO*)malloc(sizeof(OWN_PROTO));
	sendDATA->type = T_DATA;
	sendDATA->id = id;
	sendDATA->fragNum = fragNum;
	sendDATA->dataLen = dataLen;
	//sendDATA->checksum = getChecksum(sendDATA);
	sendDATA->data = data;
	return sendDATA;
}

WORD calcChecksum(char *preparedOwnProto, int ownProtoLen) {
	int len = ownProtoLen / 2;
	//printf("len %d", len);
	WORD *calcFrom = (WORD*)preparedOwnProto;
	WORD checksum = 0;
	for (int i = 0; i < len; i++) {
		if (i == 0) checksum = calcFrom[i];
		if (i != 3 && i != 0) {
			checksum += calcFrom[i];
		}
	}
	//printf("%u", checksum);
	return checksum;
}

char *createFrame(OWN_PROTO *ownProtocolFilled) {
	//based on OWN_PROTO
	//fragment Size from user + 10B Proto Head
	int ownProtoLen = ownProtocolFilled->dataLen + 10;
	WORD dataLen = (WORD)fragmentSize;
	char *filledFrame = (char*)malloc(ownProtoLen * sizeof(char));
	filledFrame[0] = ownProtocolFilled->type;
	filledFrame[1] = ownProtocolFilled->id;
	*(DWORD*)(filledFrame + 2) = ownProtocolFilled->fragNum;
	*(WORD*)(filledFrame + 6) = ownProtocolFilled->checksum;
	*(WORD*)(filledFrame + 8) = ownProtocolFilled->dataLen;

	for (int i = 10; i < 10 + ownProtocolFilled->dataLen; i++) {
		*(filledFrame + i) = ownProtocolFilled->data[i - 10];
	}
	
	return filledFrame;
}

OWN_PROTO *readFrame(char *receivedFrame) {
	OWN_PROTO *ownProtocolFilled = (OWN_PROTO*)malloc(sizeof(OWN_PROTO));
	ownProtocolFilled->type = receivedFrame[0];
	ownProtocolFilled->id = receivedFrame[1];
	ownProtocolFilled->fragNum = *(DWORD*)(receivedFrame + 2);
	ownProtocolFilled->checksum = *(WORD*)(receivedFrame + 6);
	ownProtocolFilled->dataLen = *(WORD*)(receivedFrame + 8);
	ownProtocolFilled->data = (char *)malloc(ownProtocolFilled->dataLen * sizeof(char));
	for (int i = 10; i < 10 + ownProtocolFilled->dataLen; i++) {
		ownProtocolFilled->data[i - 10] = *(receivedFrame + i);
	}

	return ownProtocolFilled;
}

DWORD getProtLen(char *receivedBuf) {
	return *(WORD*)(receivedBuf + 8) + HEADER_SIZE;
}

void repairFragmentSize() {
	if (fragmentSize > 1462) {
		printf("Fragment size too big, must be max 1462 B...\n");
		while (fragmentSize > 1462) {
			printf("Type new fragment size (just number): ");
			scanf("%d", &fragmentSize);
			fflush(stdin);
		}
	}
}

void setClient() {
	printf("Set Server port: ");
	scanf("%d", &remotePort);
	printf("Entered: %d\n", remotePort);
	fflush(stdin);

	//PCSTR ipAddr[16];
	fflush(stdin);
	printf("Set server IPv4 adress: ");
	scanf("%s", &remoteIpAddr);
	//fgets(ipAddr, 15, stdin);
	printf("Entered: %s\n", remoteIpAddr);
	getchar();
	fflush(stdin);
	//remoteIpAddr = ipAddr;

	printf("Want to send file? y/n:");
	char fileAns;
	//fileAns = 'n';
	fileAns = getOneChar();
	fileAns = toupper(fileAns);
	if (fileAns == 'Y') {
		printf("Zadajte cestu k suboru:\n");
		sendFile = TRUE;
	}
	else {
		printf("Zadajte spravu: \n");
		sendFile = FALSE;
	}

	//uberFlush();

	char message[1024];
	fgets(message, 1024, stdin);
	fflush(stdin);
	message[strlen(message)-1] = '\0';


	strcpy(dataToSend, message);
	printf("You entered: %s\n", dataToSend);

	printf("Zadaj sancu chyby (cislo od 0 do 100): ");
	scanf("%d", &mistakeChance);
	printf("Entered: %d\n", mistakeChance);
	fflush(stdin);

	printf("Zadaj max pocet chyb: ");
	scanf("%d", &mistakesMax);
	printf("Entered: %d\n", mistakesMax);
	fflush(stdin);


	printf("Set max fragment size: ");
	scanf("%d", &fragmentSize);
	printf("Entered: %d\n", fragmentSize);
	fflush(stdin);

	repairFragmentSize();
}

void setServer() {
	printf("Set own listenig port: ");
	scanf("%d", &listenPort);
	fflush(stdin);
}

char getOneChar() {
	char last, act, out;
	while (1) {
		act = getchar();
		if ((act >= 'a' && act <= 'z') || (act >= 'A' && act <= 'Z')) {
			last = act;
			break;
		}
	}

	out = last;

	while (1) {
		act = getchar();
		if (act == '\n') break;
	}
	return out;
}

char getMode() {
	char mode;
	printf("Mode: Receive- r/ Send- s/ Quit- q:\n");

	mode = getOneChar();
	
	mode = toupper(mode);

	return mode;
}

void uberFlush(){
	char act;
	while (act = getchar() == '\n');
}

int *getMissingFromAck(OWN_PROTO *ackRecv) {
	int numOfMissing = ackRecv->dataLen / sizeof(DWORD);
	int *missing = (int *)malloc(numOfMissing * sizeof(int));
	for (int i = 0; i < numOfMissing; i++) {
		missing[i] = *((DWORD*)ackRecv->data + i);
		printf("missing %d\n",missing[i]);
	}
	return missing;
}


char *makeMistake(char *filledFrame) {


	if ( mistakesCount < mistakesMax && mistakeChance != 0 && rand() % 100 < mistakeChance) {
		printf("Client: Made mistake!\n");

		int len = getProtLen(filledFrame);
		char *newFrame = (char *)malloc(len * sizeof(char));

		for (int i = 0; i < len; i++) {
			newFrame[i] = filledFrame[i];
		}
		
		newFrame[3] = 4;
		newFrame[4] = 4;
		newFrame[5] = 4;
		mistakesCount++;
		return newFrame;
	}
	return filledFrame;
}