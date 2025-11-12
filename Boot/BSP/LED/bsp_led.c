#include "bsp_led.h"
#include "sysTick.h"


void LED_GPIO_Config(void)
{
	GPIO_InitTypeDef GPIO_InitStructure;
	RCC_APB2PeriphClockCmd(LED1_GPIO_CLK, ENABLE);

	GPIO_InitStructure.GPIO_Pin = LED1_GPIO_PIN;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(LED1_GPIO_PORT, &GPIO_InitStructure);

	GPIO_SetBits(LED1_GPIO_PORT, LED1_GPIO_PIN);
}

/**
 * @brief  LED状态指示 - 通过闪烁次数表示不同状态
 * @param  code: 闪烁次数
 * @retval None
 */
void led_status_indicate(uint8_t code)
{
	uint8_t i;
	for (i = 0; i < code; i++)
	{
		LED1_ON();
		delay_ms(200);
		LED1_OFF();
		delay_ms(200);
	}
	delay_ms(1000);
}

/**
 * @brief  LED快速闪烁 - 用于表示正在进行的操作
 * @param  times: 闪烁次数
 * @param  interval_ms: 闪烁间隔（毫秒）
 * @retval None
 */
void led_fast_blink(uint8_t times, uint16_t interval_ms)
{
	for (uint8_t i = 0; i < times; i++)
	{
		LED1_ON();
		delay_ms(interval_ms);
		LED1_OFF();
		delay_ms(interval_ms);
	}
}
