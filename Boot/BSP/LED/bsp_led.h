#ifndef __LED_H
#define	__LED_H


#include "stm32f10x.h"
#define LED1_ON()	GPIO_ResetBits(GPIOC,GPIO_Pin_13)
#define LED1_OFF()	GPIO_SetBits(GPIOC,GPIO_Pin_13)


#define LED1_GPIO_PORT    	GPIOC			            /* GPIO�˿� */
#define LED1_GPIO_CLK 	    RCC_APB2Periph_GPIOC		/* GPIO�˿�ʱ�� */
#define LED1_GPIO_PIN		GPIO_Pin_13			        /* ���ӵ�SCLʱ���ߵ�GPIO */


void LED_GPIO_Config(void);
void led_status_indicate(uint8_t code);
void led_fast_blink(uint8_t times, uint16_t interval_ms);




#endif

