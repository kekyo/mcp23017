#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <malloc.h>
#include <memory.h>
#include <time.h>
#include <sys/time.h>
#include <sys/ioctl.h>

#include <vector>

#include <wiringPi.h>
#include <wiringPiI2C.h>

//#include <i2c/smbus.h>

/////////////////////////////////////////////

typedef struct
{
	uint8_t size;
	uint8_t data[32];
} i2c_smbus_block_data;

typedef struct
{
	char readWrite;
	uint8_t command;
	int size;
	i2c_smbus_block_data* pBlockData;
} i2c_smbus_ioctl_args;

static int i2c_write(int fd, uint8_t size, const uint8_t* pData)
{
	i2c_smbus_block_data blockData;
	blockData.size = size;
	memcpy(blockData.data, pData, blockData.size);

	i2c_smbus_ioctl_args args;
	args.readWrite = 0;		// SMBUS_WRITE
	args.command = 0;
	args.size = 5;			// BLOCK_DATA
	args.pBlockData = &blockData;
	
	return ioctl(fd, 0x0720, &args);
}

/////////////////////////////////////////////


#define ADDRESS  0x20     // AVR address

static unsigned int pcount = 0;

typedef struct
{
	uint8_t address;
	uint8_t data;
} ym2151packet;

static void write_data(int fd, const std::vector<ym2151packet>& packet)
{
	int size = packet.size();
	if (size >= 1)
	{
		i2c_write(fd, size * sizeof(ym2151packet), &packet[0].address);
		pcount += size;
	}
}

static int getvv(const uint8_t** p)
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

static void Sync(struct timeval* pt, double syncDelayUSec, int multiply)
{
	struct timeval now;
	gettimeofday(&now, NULL);

	double differUSec = (now.tv_sec - pt->tv_sec) * 1000000.0 + (now.tv_usec - pt->tv_usec);
	int delayUSec = (int)((syncDelayUSec - differUSec) + syncDelayUSec * (multiply - 0));

	//printf("%f : %f : %d : %d\n", syncDelayUSec, differUSec, multiply, delayUSec);

	if (delayUSec > 0)
	{
		delayMicroseconds(delayUSec);
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

	S98Header* pHeader = (S98Header*)pBuffer;
	if (pHeader->timerInfoNumerator == 0) pHeader->timerInfoNumerator = 10;
	if (pHeader->timerInfoDenominator == 0) pHeader->timerInfoDenominator = 1000;
	if (pHeader->deviceCount == 0) pHeader->deviceCount = 1;

	printf("TagIndex = %08x\n", pHeader->tagIndex);
	printf("DumpDataIndex = %08x\n", pHeader->dumpDataIndex);
	printf("LoopPointIndex = %08x\n", pHeader->loopPointIndex);
	printf("DeviceCount = %08x\n", pHeader->deviceCount);
	printf("DeviceType0 = %08x\n", pHeader->deviceInfo[0].deviceType);
	printf("DeviceClock0 = %uHz\n", pHeader->deviceInfo[0].clock);

	double syncDelayUSec = ((double)pHeader->timerInfoNumerator) * 1000000.0 / pHeader->timerInfoDenominator;
	printf("Sync = %d / %d = %fusec\r\n", pHeader->timerInfoNumerator, pHeader->timerInfoDenominator, syncDelayUSec);

	int ifd = wiringPiI2CSetup(ADDRESS);

	const uint8_t* pData = pBuffer + pHeader->dumpDataIndex;

	std::vector<ym2151packet> buffer;

	struct timeval sv, ev, sync;
	gettimeofday(&sv, NULL);
	sync = sv;

	int count = 0;
	while (1)
	{
		switch (*pData)
		{
			case 0xff:
				if (buffer.size() >= 1)
				{
					write_data(ifd, buffer);;
					buffer.clear();
				}

				Sync(&sync, syncDelayUSec, 1);
				pData++;

				break;
			case 0xfe:
				{
					if (buffer.size() >= 1)
					{
						write_data(ifd, buffer);;
						buffer.clear();
					}

					int syncCount = getvv(&pData) + 1;
					if (syncCount >= 1)
					{
						Sync(&sync, syncDelayUSec, syncCount);
					}
				}
				break;
			case 0xfd:
				{
					if (pHeader->loopPointIndex == 0)
					{
						printf("EOF\n");
						return 0;
					}

					pData = pBuffer + pHeader->loopPointIndex;

					printf("Loop.\n");
				}
				break;
			default:
				{
					if (buffer.size() >= 16)
					{
						write_data(ifd, buffer);;
						buffer.clear();
					}

					ym2151packet packet;
					pData++;
					packet.address = *pData;
					pData++;
					packet.data = *pData;
					pData++;
					buffer.push_back(packet);
				}
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

