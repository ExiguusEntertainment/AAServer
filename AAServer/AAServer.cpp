// AAServer.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include "config.h"
#include <windows.h>
#include "ipxserver.h"
#include <stdlib.h>
#include <string.h>
#include "ipx.h"


IPaddress ipxServerIp;  // IPAddress for server's listening port
UDPsocket ipxServerSocket;  // Listening server socket

packetBuffer connBuffer[SOCKETTABLESIZE];

Bit8u inBuffer[IPXBUFFERSIZE];
IPaddress ipconn[SOCKETTABLESIZE];  // Active TCP/IP connection 
UDPsocket tcpconn[SOCKETTABLESIZE];  // Active TCP/IP connections
SDLNet_SocketSet serverSocketSet;
//TIMER_TickHandler* serverTimer;

void UnpackIP(PackedIP ipPack, IPaddress * ipAddr) {
	ipAddr->host = ipPack.host;
	ipAddr->port = ipPack.port;
}

void PackIP(IPaddress ipAddr, PackedIP *ipPack) {
	ipPack->host = ipAddr.host;
	ipPack->port = ipAddr.port;
}

Bit8u packetCRC(Bit8u *buffer, Bit16u bufSize) {
	Bit8u tmpCRC = 0;
	Bit16u i;
	for(i=0;i<bufSize;i++) {
		tmpCRC ^= *buffer;
		buffer++;
	}
	return tmpCRC;
}

/*
static void closeSocket(Bit16u sockidx) {
	Bit32u host;

	host = ipconn[sockidx].host;
	LOG_MSG("IPXSERVER: %d.%d.%d.%d disconnected\n", CONVIP(host));

	SDLNet_TCP_DelSocket(serverSocketSet,tcpconn[sockidx]);
	SDLNet_TCP_Close(tcpconn[sockidx]);
	connBuffer[sockidx].connected = false;
	connBuffer[sockidx].waitsize = false;
}
*/

static void sendIPXPacket(Bit8u *buffer, Bit16s bufSize) {
	Bit16u srcport, destport;
	Bit32u srchost, desthost;
	Bit16u i;
	Bits result;
	UDPpacket outPacket;
	outPacket.channel = -1;
	outPacket.data = buffer;
	outPacket.len = bufSize;
	outPacket.maxlen = bufSize;
	IPXHeader *tmpHeader;
	tmpHeader = (IPXHeader *)buffer;

	srchost = tmpHeader->src.addr.byIP.host;
	desthost = tmpHeader->dest.addr.byIP.host;

	srcport = tmpHeader->src.addr.byIP.port;
	destport = tmpHeader->dest.addr.byIP.port;


#if 1
	printf("IPX packet: (%d.%d.%d.%d:%d) [", CONVIP(srchost), srcport);
	for (i=0; i<bufSize; i++) {
		if (i==sizeof(IPXHeader))
			printf("| ");
		printf("%02X ", buffer[i]);
	}
	printf("]\n");
#endif

	if(desthost == 0xffffffff) {
		// Broadcast
		for(i=0;i<SOCKETTABLESIZE;i++) {
			if(connBuffer[i].connected && ((ipconn[i].host != srchost)||(ipconn[i].port!=srcport))) {
				outPacket.address = ipconn[i];
				result = SDLNet_UDP_Send(ipxServerSocket,-1,&outPacket);
				if(result == 0) {
					LOG_MSG("IPXSERVER: %s\n", SDLNet_GetError());
					continue;
				}
				//LOG_MSG("IPXSERVER: Packet of %d bytes sent from %d.%d.%d.%d to %d.%d.%d.%d (BROADCAST) (%x CRC)\n", bufSize, CONVIP(srchost), CONVIP(ipconn[i].host), packetCRC(&buffer[30], bufSize-30));
			}
		}
	} else {
		// Specific address
		for(i=0;i<SOCKETTABLESIZE;i++) {
			if((connBuffer[i].connected) && (ipconn[i].host == desthost) && (ipconn[i].port == destport)) {
				outPacket.address = ipconn[i];
				result = SDLNet_UDP_Send(ipxServerSocket,-1,&outPacket);
				if(result == 0) {
					LOG_MSG("IPXSERVER: %s\n", SDLNet_GetError());
					continue;
				}
				//LOG_MSG("IPXSERVER: Packet sent from %d.%d.%d.%d to %d.%d.%d.%d\n", CONVIP(srchost), CONVIP(desthost));
			}
		}
	}




}

bool IPX_isConnectedToServer(Bits tableNum, IPaddress ** ptrAddr) {
	if(tableNum >= SOCKETTABLESIZE) return false;
	*ptrAddr = &ipconn[tableNum];
	return connBuffer[tableNum].connected;
}

static void ackClient(IPaddress clientAddr) {
	IPXHeader regHeader;
	UDPpacket regPacket;
	Bits result;

	SDLNet_Write16(0xffff, regHeader.checkSum);
	SDLNet_Write16(sizeof(regHeader), regHeader.length);
	
	SDLNet_Write32(0, regHeader.dest.network);
	PackIP(clientAddr, &regHeader.dest.addr.byIP);
	SDLNet_Write16(0x2, regHeader.dest.socket);

	SDLNet_Write32(1, regHeader.src.network);
	PackIP(ipxServerIp, &regHeader.src.addr.byIP);
	SDLNet_Write16(0x2, regHeader.src.socket);
	regHeader.transControl = 0;

	regPacket.data = (Uint8 *)&regHeader;
	regPacket.len = sizeof(regHeader);
	regPacket.maxlen = sizeof(regHeader);
	regPacket.address = clientAddr;
	// Send registration string to client.  If client doesn't get this, client will not be registered
	result = SDLNet_UDP_Send(ipxServerSocket,-1,&regPacket);

}

static void IPX_ServerLoop() {
	UDPpacket inPacket;
	IPaddress tmpAddr;

	//char regString[] = "IPX Register\0";

	Bit16u i;
	Bit32u host;
	Bits result;

	inPacket.channel = -1;
	inPacket.data = &inBuffer[0];
	inPacket.maxlen = IPXBUFFERSIZE;


	result = SDLNet_UDP_Recv(ipxServerSocket, &inPacket);
	if (result != 0) {
		// Check to see if incoming packet is a registration packet
		// For this, I just spoofed the echo protocol packet designation 0x02
		IPXHeader *tmpHeader;
		tmpHeader = (IPXHeader *)&inBuffer[0];
	
		// Check to see if echo packet
		if(SDLNet_Read16(tmpHeader->dest.socket) == 0x2) {
			// Null destination node means its a server registration packet
			if(tmpHeader->dest.addr.byIP.host == 0x0) {
				UnpackIP(tmpHeader->src.addr.byIP, &tmpAddr);
				for(i=0;i<SOCKETTABLESIZE;i++) {
					if(!connBuffer[i].connected) {
						// Use prefered host IP rather than the reported source IP
						// It may be better to use the reported source
						ipconn[i] = inPacket.address;

						connBuffer[i].connected = true;
						host = ipconn[i].host;
						LOG_MSG("IPXSERVER: Connect from %d.%d.%d.%d:%d\n", CONVIP(host), ipconn[i].port);
						ackClient(inPacket.address);
						return;
					} else {
						if((ipconn[i].host == tmpAddr.host) && (ipconn[i].port == tmpAddr.port)) {

							LOG_MSG("IPXSERVER: Reconnect from %d.%d.%d.%d\n", CONVIP(tmpAddr.host));
							// Update anonymous port number if changed
							ipconn[i].port = inPacket.address.port;
							ackClient(inPacket.address);
							return;
						}
					}
					
				}
			}
		}

		// IPX packet is complete.  Now interpret IPX header and send to respective IP address
		sendIPXPacket((Bit8u *)inPacket.data, inPacket.len);
	}
}

void IPX_StopServer() {
	//TIMER_DelTickHandler(&IPX_ServerLoop);
	SDLNet_UDP_Close(ipxServerSocket);
}

bool IPX_StartServer(Bit16u portnum) {
	Bit16u i;

	if(!SDLNet_ResolveHost(&ipxServerIp, NULL, portnum)) {
		//serverSocketSet = SDLNet_AllocSocketSet(SOCKETTABLESIZE);
		ipxServerSocket = SDLNet_UDP_Open(portnum);
		if(!ipxServerSocket) {
            printf("Failed to create server socket: %s\n", SDLNet_GetError());
            return false;
        }

		for(i=0;i<SOCKETTABLESIZE;i++) {
            connBuffer[i].connected = false;
        }

        printf("Server started on port %d\n", portnum);
		//TIMER_AddTickHandler(&IPX_ServerLoop);
        while (1) {
            IPX_ServerLoop();
            Sleep(1);
        }

		return true;
	} else {
        printf("Server could not resolve host!\n");
    }
	return false;
}


int _tmain(int argc, _TCHAR* argv[])
{
    printf("Amulets & Armor IPX Server v1.00\n");
    printf("--------------------------------\n");

    if(SDL_Init(0)==-1) {
        printf("SDL_Init: %s\n", SDL_GetError());
        exit(1);
    }
    if(SDLNet_Init()==-1) {
        printf("SDLNet_Init: %s\n", SDLNet_GetError());
        exit(2);
    }

    IPX_StartServer(213);
	return 0;
}

