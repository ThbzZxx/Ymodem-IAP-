#include "bsp_led.h"
#include "stm32f10x.h"
#include "sysTick.h"
	  
int main(void) {

  LED_GPIO_Config();

  while (1) {
    LED1_ON();
    delay_ms(2000);
    LED1_OFF();
    delay_ms(2000);
  }
}

// HardFault异常处理 - 用于调试
void HardFault_Handler(void) {

}
