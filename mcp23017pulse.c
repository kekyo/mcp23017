#include <wiringPi.h>
#include <wiringPiI2C.h>

#define IODIRA   0x00     // MCP23017 address of I/O direction
#define IODIRB   0x01     // MCP23017 address of I/O direction
#define GPIOA    0x12     // MCP23017 address of GP Value
#define GPIOB    0x13     // MCP23017 address of GP Value
#define ADDRESS  0x20     // MCP23017 I2C address

int main()
{
	int fd = wiringPiI2CSetup(ADDRESS);
	if (fd == -1) return 1;

	wiringPiI2CWriteReg16(fd, IODIRA, 0x00);
	wiringPiI2CWriteReg16(fd, IODIRB, 0x00);
	wiringPiI2CWriteReg16(fd, GPIOA, 0x00);
	wiringPiI2CWriteReg16(fd, GPIOB, 0x00);

	while (1)
	{
		wiringPiI2CWriteReg8(fd, GPIOA, 0x01);
		delay(200);
		wiringPiI2CWriteReg8(fd, GPIOA, 0x00);
		delay(200);
	}
}

