
#include "nvitem_common.h"
#include "nvitem_packet.h"
#include "nvitem_channel.h"

//----------------------------------------------------------------------
//				packet define
//----------------------------------------------------------------------
#define _MAGICNUM 0x524d4e56		// "RMNV"
typedef struct
{
	uint32	MAGICNUM;
	uint32	ecc;

	uint32	idxGrp;
	uint32	idxSub;
	uint32	sizeOfPacket;
}PACKET_HEAD;

#define PACKETSIZE	1024
static uint8 packeBuf[sizeof(PACKET_HEAD)+PACKETSIZE];

static PACKET_HEAD*	_packetHead;
static uint8*			_packetBody;

static uint32 _idxGrp	= 0;
static uint32 _idxSub	= 0;

//---------------------------------------------------------
#define PACKETRSP_SIZE	16
static uint8 packetRsp[sizeof(PACKET_HEAD)+PACKETRSP_SIZE];
static PACKET_HEAD*	_packetRspHead;
static uint8*			_packetRspBody;

static uint32 __makeEcc(uint8* buf, uint32 len)
{
	return 0;
}

void __sendPacketRsp(PACKET_HEAD* header, uint8 rsp)
{
	uint32 hasWrite;
//---for test
#if 0
	static uint32 cnt = 0;
	uint32 _delay;
	_delay = rand()%2;
	if(0 == _delay) cnt++;
	else if(3 >= cnt){_delay = 0;}
	else{cnt = 0;}
	sleep(_delay);	// for test
#endif
//---
	memset(packetRsp,0,sizeof(PACKET_HEAD)+PACKETRSP_SIZE);
	memcpy(_packetRspHead,header,sizeof(PACKET_HEAD));
	_packetRspHead->sizeOfPacket	= 1;
	_packetRspHead->ecc			= __makeEcc(0,0);
	_packetRspBody[0] = rsp;
	channel_write(packetRsp, sizeof(PACKET_HEAD)+PACKETRSP_SIZE, &hasWrite);
}
//---------------------------------------------------------

static BOOLEAN __chkEcc(uint8* buf, uint32 len, uint32 ecc)
{
	return 1;
}

/*PASS*/
void _initPacket(void)
{
	_packetHead = ((PACKET_HEAD*)packeBuf);
	_packetBody = &packeBuf[sizeof(PACKET_HEAD)];

	_idxGrp	= (uint32)(-1);
	_idxSub	= 0;

	_packetRspHead = ((PACKET_HEAD*)packetRsp);
	_packetRspBody = &packetRsp[sizeof(PACKET_HEAD)];
}

/*PASS*/
uint32 _getPacket(uint8** buf, uint32* size)
{
	uint32 hasRead;
	//printf("NVITEM:_getPacket\n");
//------------------------------------------------------------
	if(!channel_read(packeBuf, sizeof(PACKET_HEAD)+PACKETSIZE, &hasRead))
//------------------------------------------------------------
	{
		//printf("NVITEM:fail channel_read\n");
		return _PACKET_FAIL;
	}
	if(hasRead < sizeof(PACKET_HEAD))
	{
		printf("NVITEM:fail PACKET_HEAD\n");
		return _PACKET_SKIP;
	}
	if(_MAGICNUM != _packetHead->MAGICNUM){
		printf("NVITEM:fail _MAGICNUM\n");
		return _PACKET_SKIP;
	}
	if(!__chkEcc(0,0,0)){
		printf("NVITEM:fail ECC\n");
		return _PACKET_SKIP;
	}
	if(hasRead < (sizeof(PACKET_HEAD)+_packetHead->sizeOfPacket))
	{
		printf("NVITEM:fail SIZE\n");
		return _PACKET_SKIP;
	}
//----------------------------------------------------------
if(0)
{
	uint32 i;
	printf("----------");
	for(i = 0; i < (sizeof(PACKET_HEAD)+_packetHead->sizeOfPacket); i++)
	{
		if(!(i%8))
		{
			if(!(i%32))
			{
				printf(" \n ");
			}
			printf(" | ");
		}
		printf("%2x ",packeBuf[i]);
	}
	printf("\n----------\n");
}
//----------------------------------------------------------
	if(_idxGrp == _packetHead->idxGrp)
	{
		if(_idxSub+1 == _packetHead->idxSub)
		{
			_idxSub++;
			*buf	= _packetBody;
			*size	= _packetHead->sizeOfPacket;
//			__sendPacketRsp(_packetHead, 1);
			printf("NVITEM _getPacket continue read %d size %d grp %d sub %d\n",hasRead,_packetHead->sizeOfPacket,_packetHead->idxGrp,_packetHead->idxSub);
			return _PACKET_CONTINUE;
		}
		else
		{
			printf("NVITEM:fail GROUP %d %d %d %d\n",_idxGrp,_idxSub,_packetHead->idxGrp,_packetHead->idxSub);
			return _PACKET_SKIP;
		}
	}
	else
	{
		if(0 == _packetHead->idxSub)
		{
			_idxGrp	= _packetHead->idxGrp;
			_idxSub	= 0;
			*buf	= _packetBody;
			*size	= _packetHead->sizeOfPacket;
//			__sendPacketRsp(_packetHead, 1);
			printf("NVITEM _getPacket start read %d size %d grp %d sub %d\n",hasRead,_packetHead->sizeOfPacket,_packetHead->idxGrp,_packetHead->idxSub);
			return _PACKET_START;
		}
		else
		{
			printf("NVITEM:fail SUB %d %d %d %d\n",_idxGrp,_idxSub,_packetHead->idxGrp,_packetHead->idxSub);
			return _PACKET_SKIP;
		}
	}
}

void _sendPacktRsp(uint8 rsp)
{
	__sendPacketRsp( _packetHead, rsp);
}