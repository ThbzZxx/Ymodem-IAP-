#include "bootloader.h"

iapfun jump2app; 

/*    boot跳转配置（为app提供干净的运行环境）
    1.关闭全局中断
    2.复位RCC 和 开启的外设
    3.关闭滴答定时器
    4.设置跳转PC SP 和CONTROL寄存器
    5.打开全局中断
    6.注意RTOS和裸机跳转异同
*/

uint8_t iap_load_app(uint32_t appxaddr)
{
	uint8_t i;
	
    uint32_t jump_addr;
    if (((*(__IO uint32_t*)appxaddr) & 0x2FFF0000 ) == 0x20000000) 
	{  
        jump_addr = *(__IO uint32_t*) (appxaddr + 4);  
        jump2app = (iapfun)jump_addr;
		
		/*  关闭全局中断    */
		__disable_irq();

		/*  复位RCC  */
		RCC_DeInit();
		
		/*  关闭滴答定时器  */
		SysTick->CTRL = 0;
		SysTick->LOAD = 0;
		SysTick->VAL  = 0;

		/*  关闭所有中断，清除所有中断挂起标志  */  
		for (i = 0; i < 8; i++)
		{
			NVIC->ICER[i]=0xFFFFFFFF;
			NVIC->ICPR[i]=0xFFFFFFFF;
		}
		/*  使能全局中断    */
		__enable_irq();

		/*  设置主堆栈指针  */	
        __set_MSP(*(__IO uint32_t*)appxaddr);
	
		/*  RTOS中很重要，设置为特权模式，使用MSP指针*/
    	__set_CONTROL(0);

        jump2app();
        return 1;
    }
    return 0;
}

uint8_t mcu_flash_erase(uint32_t addr, uint16_t sector_num) 
{
	uint16_t i;
	FLASH_Unlock();
	for (i = 0; i < sector_num; ++i) 
	{
		if (FLASH_ErasePage(addr + i * 1024) != FLASH_COMPLETE) 
		{
			return 0;
		}
	}
	FLASH_Lock();
	return 1;
}

uint8_t mcu_flash_write(uint32_t addr, uint8_t *buffer, uint32_t length) 
{
	FLASH_Status result;
	uint16_t i, data = 0;
	FLASH_Unlock();
	for (i = 0; i < length; i += 2) 
	{
		data = (*(buffer + i + 1) << 8) + (*(buffer + i));
		result = FLASH_ProgramHalfWord((uint32_t)(addr + i), data);
	}
	FLASH_Lock();

	if(result != FLASH_COMPLETE)
	{
		return 0;
	}
	else
	{
		return 1;
	}
}

void mcu_flash_read(uint32_t addr, uint8_t *buffer, uint32_t length) 
{
	memcpy(buffer, (void *)addr, length);
}
