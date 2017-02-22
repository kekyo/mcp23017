#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <malloc.h>
#include <memory.h>

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

static void set_databusdirection(int fd, int isout)
{
	if (isout) wiringPiI2CWriteReg8(fd, IODIRA, 0x00);
	else wiringPiI2CWriteReg8(fd, IODIRA, 0xff);
}

static void write_databus(int fd, unsigned char data)
{
	wiringPiI2CWriteReg8(fd, GPIOA, data);
	delay(1);
}

static void write_controlbus(int fd, unsigned char controls)
{
	wiringPiI2CWriteReg8(fd, GPIOB, controls ^ WR ^ RD ^ RST);
	delay(1);
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

static unsigned char read_ym2151(int fd, unsigned char address)
{
	unsigned char result;

	set_databusdirection(fd, 1);
	write_databus(fd, address);
	write_controlbus(fd, WR);
	write_controlbus(fd, 0);
	set_databusdirection(fd, 0);
	write_controlbus(fd, A0);
	write_controlbus(fd, A0 | RD);
	result = wiringPiI2CReadReg8(fd, GPIOA);
	write_controlbus(fd, 0);
	return result;
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
	int fd;
	struct stat s;
	unsigned char* pBuffer;
	S98Header* pHeader;

	fd = open("acid_shota.s98", O_RDONLY);
	fstat(fd, &s);
	pBuffer = (unsigned char*)malloc((int)s.st_size + 1);
	memset(pBuffer, 0, (int)s.st_size + 1);
	read(fd, pBuffer, s.st_size);
	close(fd);

	pHeader = (S98Header*)pBuffer;

	printf("TagIndex = %08x\r\n", pHeader->tagIndex);
	printf("DumpDataIndex = %08x\r\n", pHeader->dumpDataIndex);
	printf("LoopPointIndex = %08x\r\n", pHeader->loopPointIndex);

	fd = wiringPiI2CSetup(ADDRESS);
	if (fd == -1) return 1;

	wiringPiI2CWriteReg16(fd, IODIRA, 0xff);
	wiringPiI2CWriteReg16(fd, IODIRB, 0x00);

	write_controlbus(fd, RST);
	write_controlbus(fd, 0);

	while (1)
	{
		wiringPiI2CWriteReg8(fd, GPIOA, 0x01);
		delay(200);
		wiringPiI2CWriteReg8(fd, GPIOA, 0x00);
		delay(200);
	}
}

