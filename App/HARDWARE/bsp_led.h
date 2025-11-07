#ifndef __LED_H
#define	__LED_H


#include "stm32f10x.h"
#define LED1_ON()	GPIO_ResetBits(GPIOC,GPIO_Pin_13)
#define LED1_OFF()	GPIO_SetBits(GPIOC,GPIO_Pin_13)


#define LED1_GPIO_PORT    	GPIOC			            /* GPIO端口 */
#define LED1_GPIO_CLK 	    RCC_APB2Periph_GPIOC		/* GPIO端口时钟 */
#define LED1_GPIO_PIN		GPIO_Pin_13			        /* 连接到SCL时钟线的GPIO */


void LED_GPIO_Config(void);




#endif

