#include "stm32f10x.h"
#include "bsp_usart.h"
#include "bsp_led.h"
#include "sysTick.h"
#include "ymodem.h"
#include "bootloader.h"
#include "bsp_key.h"



static void iap_process(void)
{
	delay_ms(50);
	if ((((*(vu32*)(APP_SECTOR_ADDR+4))&0xFF000000)==0x08000000)&&(!iap_load_app(APP_SECTOR_ADDR))) 
	{
		// 启动失败
		while(1){
		}
	}
}


int main(void)
{
	LED_GPIO_Config();
	ymodem_init();
	Key_GPIO_Config();
	
	// 直接通过按键判断是否进入升级模式
	if(Key_Scan(KEY1_GPIO_PORT,KEY1_GPIO_PIN) == 1)
	{
		LED1_ON();
		ymodem_c();
		
		//中断处理YMODEM协议
		while(1){
		}
	}
	else
	{
		LED1_ON();
		delay_ms(1000);
		LED1_OFF();
		iap_process();
	}
}











