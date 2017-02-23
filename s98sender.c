#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <malloc.h>
#include <memory.h>
#include <time.h>
#include <sys/time.h>

#include <wiringPi.h>
#include <wiringPiI2C.h>

#define IODIRA   0x00     // MCP23017 address of I/O direction
#define IODIRB   0x01     // MCP23017 address of I/O direction
#define GPIOA    0x12     // MCP23017 address of GP Value
#define GPIOB    0x13     // MCP23017 address of GP Value
#define ADDRESS  0x20     // MCP23017 I2C address

#define A0 0x01
#define WR 0x02
#define RD 0x04
#define RST 0x08

#define YM2151BUSY 0x80

static unsigned int pcount = 0;

static void set_databusdirection(int fd, int isout)
{
	if (isout) wiringPiI2CWriteReg8(fd, IODIRA, 0x00);
	else wiringPiI2CWriteReg8(fd, IODIRA, 0xff);
	pcount++;
}

static void write_databus(int fd, unsigned char data)
{
	wiringPiI2CWriteReg8(fd, GPIOA, data);
	//delayMicroseconds(1);
	pcount++;
}

static void write_controlbus(int fd, unsigned char controls)
{
	wiringPiI2CWriteReg8(fd, GPIOB, controls ^ WR ^ RD ^ RST);
	//delayMicroseconds(1);
	pcount++;
}

static void write_ym2151(int fd, unsigned char address, unsigned char data)
{
	set_databusdirection(fd, 1);
	write_databus(fd, address);
	write_controlbus(fd, WR);
	write_controlbus(fd, 0);
	write_controlbus(fd, A0);
	write_databus(fd, data);
	write_controlbus(fd, A0 | WR);
	write_controlbus(fd, A0);
	set_databusdirection(fd, 0);
}

static unsigned char read_ym2151(int fd)
{
	unsigned char result;

	write_controlbus(fd, A0);
	write_controlbus(fd, A0 | RD);
	result = wiringPiI2CReadReg8(fd, GPIOA);
	pcount++;
	write_controlbus(fd, 0);
	return result;
}

static int getvv(const unsigned char** p)
{
	int s = 0, n = 0;
	(*p)--;
	do
	{
		n |= (*(++(*p)) & 0x7f) << s;
		s += 7;
	}
	while (*(*p) & 0x80);
	return n + 2;
}

typedef struct
{
	unsigned int deviceType;
	unsigned int clock;
	unsigned int pan;
	unsigned int reserved;
} DeviceInfo;

typedef struct
{
	char magic[3];
	char version;
	unsigned int timerInfoNumerator;
	unsigned int timerInfoDenominator;
	unsigned int compressing;
	unsigned int tagIndex;
	unsigned int dumpDataIndex;
	unsigned int loopPointIndex;
	unsigned int deviceCount;
	DeviceInfo deviceInfo[1];
} S98Header;

int main()
{
	int fd = open("acid_shota.s98", O_RDONLY);

	struct stat s;
	fstat(fd, &s);

	unsigned char* pBuffer = (unsigned char*)malloc((int)s.st_size + 1);
	memset(pBuffer, 0, (int)s.st_size + 1);
	read(fd, pBuffer, s.st_size);

	close(fd);

	const S98Header* pHeader = (const S98Header*)pBuffer;

	printf("TagIndex = %08x\n", pHeader->tagIndex);
	printf("DumpDataIndex = %08x\n", pHeader->dumpDataIndex);
	printf("LoopPointIndex = %08x\n", pHeader->loopPointIndex);
	printf("DeviceCount = %08x\n", pHeader->deviceCount);
	printf("DeviceType0 = %08x\n", pHeader->deviceInfo[0].deviceType);
	printf("DeviceClock0 = %uHz\n", pHeader->deviceInfo[0].clock);

	int syncDelay = (int)(((long long)pHeader->timerInfoNumerator) * 1000 / pHeader->timerInfoDenominator);
	printf("Sync = %d / %d = %dmsec\r\n", pHeader->timerInfoNumerator, pHeader->timerInfoDenominator, syncDelay);

	int wfd = wiringPiI2CSetup(ADDRESS);
	if (wfd == -1) return 1;

	wiringPiI2CWriteReg8(fd, IODIRA, 0xff);
	wiringPiI2CWriteReg8(fd, IODIRB, 0x00);

	write_controlbus(fd, RST);
	write_controlbus(fd, 0);

	const unsigned char* pData = pBuffer + pHeader->dumpDataIndex;

	struct timeval sv, ev;
	gettimeofday(&sv, NULL);

	int count = 0;
	while (1)
	{
		switch (*pData)
		{
			case 0xff:
				// TODO: Calculate by between start and current time.
				delay(syncDelay);
				pData++;
				//printf("Sync\n");
				break;
			case 0xfe:
				{
					// TODO: Calculate by between start and current time.
					int syncCount = getvv(&pData);
					//printf("Sync[%d]\n", syncCount);
					delay(syncDelay * syncCount);
				}
				break;
			case 0xfd:
				printf("EOF\n");
				break;
			case 0x00:
				{
					pData++;
					unsigned char address = *pData;
					pData++;
					unsigned char data = *pData;
					pData++;
					write_ym2151(fd, address, data);
					while (1)
					{
						data = read_ym2151(fd);
						if ((data & YM2151BUSY) == 0) break;
						printf("BUSY\n");
					}	
				}
				break;
			default:
				printf("Unknown opcode: %02x\n", *pData);
				pData += 3;
				break;
		}

		count++;
		if (count == 10000)
		{
			gettimeofday(&ev, NULL);
			double differSec = (ev.tv_sec - sv.tv_sec) + (ev.tv_usec - sv.tv_usec) * 1.0e-6;
			printf("%u OPS : %f OPS/sec\n", pcount, pcount / differSec);
			count = 0;
			pcount = 0;
			sv = ev;
		}
	}
}

