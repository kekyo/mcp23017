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

#define ADDRESS  0x20     // AVR address

static unsigned int pcount = 0;

static void write_data(int fd, unsigned char address, unsigned char data)
{
	wiringPiI2CWriteReg8(fd, address, data);
	pcount++;
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

static void Sync(struct timeval* pt, double syncDelayMSec, int multiply)
{
	struct timeval now;
	gettimeofday(&now, NULL);

	double differMSec = (now.tv_sec - pt->tv_sec) * 1000.0 + (now.tv_usec - pt->tv_usec) * 0.001;
	int delayTime = (int)((syncDelayMSec - differMSec) + syncDelayMSec * (multiply - 1));

	//printf("%f : %f : %d : %d\n", syncDelayMSec, differMSec, multiply, delayTime);

	if (delayTime >= 1)
	{
		delay(delayTime);
	}

	*pt = now;
}

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

	double syncDelayMSec = ((double)pHeader->timerInfoNumerator) * 1000.0 / pHeader->timerInfoDenominator;
	printf("Sync = %d / %d = %fmsec\r\n", pHeader->timerInfoNumerator, pHeader->timerInfoDenominator, syncDelayMSec);

	int ifd = wiringPiI2CSetup(ADDRESS);

	const unsigned char* pData = pBuffer + pHeader->dumpDataIndex;

	struct timeval sv, ev, sync;
	gettimeofday(&sv, NULL);
	sync = sv;

	int count = 0;
	while (1)
	{
		switch (*pData)
		{
			case 0xff:
				Sync(&sync, syncDelayMSec, 1);
				pData++;
				break;
			case 0xfe:
				{
					int syncCount = getvv(&pData);
					if (syncCount >= 1)
					{
						Sync(&sync, syncDelayMSec, syncCount);
					}
				}
				break;
			case 0xfd:
				printf("EOF\n");
				return 0;
			case 0x00:
				{
					pData++;
					unsigned char address = *pData;
					pData++;
					unsigned char data = *pData;
					pData++;
					write_data(ifd, address, data);
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

