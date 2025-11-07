/*
 * @Author: random
 * @Date: 2025-11-07 15:50:01
 * @Last Modified by: random
 * @Last Modified time: Do not Edit
 */
#ifndef __SYSTICK_H
#define __SYSTICK_H

#include "stm32f10x.h"

#define delay_ms(x) SysTick_Delay_Ms(x)     //????

void SysTick_Init(void);
void delay_us(__IO u32 nTime);
void SysTick_Delay_Us(__IO uint32_t us);
void SysTick_Delay_Ms(__IO uint32_t ms);

#endif /* __SYSTICK_H */
