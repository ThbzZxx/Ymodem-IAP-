#include "stm32f10x.h"
#include "bsp_led.h"
#include "sysTick.h"

/*	APP程序
	1.APP程序入口是 Reset_Handler
	2.注意设置app的中断向量地址
	3.BOOT占用的RAM空间可以全部被APP使用
	4.APP程序版本号和程序完整性问题(CRC校验/MD5校验实现)
	5.固件加密（AES....）
	6.APP跳回BOOT->使用NVIC_StstemReset软件复位
*/

int main(void)
{
	NVIC_SetVectorTable(NVIC_VectTab_FLASH, 0x3000);
	LED_GPIO_Config();
	
	while(1)
	{
		LED1_ON();
		delay_ms(500);
		LED1_OFF();
		delay_ms(500);
	}
}

