#ifndef _IPXSERVER_H_
#define _IPXSERVER_H_

#include "config.h"
#include "SDL_net.h"

struct packetBuffer {
	Bit8u buffer[1024];
	Bit16s packetSize;  // Packet size remaining in read
	Bit16s packetRead;  // Bytes read of total packet
	bool inPacket;      // In packet reception flag
	bool connected;		// Connected flag
	bool waitsize;
};

#define SOCKETTABLESIZE 256
#define CONVIP(hostvar) hostvar & 0xff, (hostvar >> 8) & 0xff, (hostvar >> 16) & 0xff, (hostvar >> 24) & 0xff
#define CONVIPX(hostvar) hostvar[0], hostvar[1], hostvar[2], hostvar[3], hostvar[4], hostvar[5]


void IPX_StopServer();
bool IPX_StartServer(Bit16u portnum);
bool IPX_isConnectedToServer(Bits tableNum, IPaddress ** ptrAddr);

Bit8u packetCRC(Bit8u *buffer, Bit16u bufSize);

#endif
