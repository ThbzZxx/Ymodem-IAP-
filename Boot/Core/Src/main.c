#include "bootloader.h"
#include "bsp_key.h"
#include "bsp_led.h"
#include "bsp_usart.h"
#include "config_manager.h"
#include "crc32.h"
#include "firmware_verify.h"
#include "stm32f10x.h"
#include "sysTick.h"
#include "ymodem.h"

/* ============================================================================
 *                           全局变量定义
 * ============================================================================
 */
system_config_t g_config; // 系统配置（版本、状态、分区信息等）



/* ============================================================================
 *                           主函数
 * ============================================================================
 */

/**
 * @brief  Bootloader主程序
 * @note   工作流程：
 *         1. 硬件初始化
 *         2. 读取配置区
 *         3. 检查按键强制升级
 *         4. 检查升级标志
 *         5. 启动计数器检查（容错机制）
 *         6. 验证并跳转到APP
 *         7. 失败则进入等待升级模式
 */
int main(void)
{
	// ========== 步骤1：硬件初始化 ==========
	LED_GPIO_Config();
	Key_GPIO_Config();
	ymodem_init(); // Ymodem协议和UART初始化

	// ========== 步骤2：读取并初始化配置 ==========
	if (!init_system_config())
	{
		// 配置初始化失败
		while (1)
		{
			led_fast_blink(1, 100); // 持续快闪表示严重错误
		}
	}

	// ========== 步骤3：检查按键强制升级 ==========
	if (Key_Scan(KEY1_GPIO_PORT, KEY1_GPIO_PIN) == 1)
	{
		upgrade_process(); // 进入升级流程
		// 升级失败
		NVIC_SystemReset();
	}

	// ========== 步骤4：检查升级标志 ==========
	// 功能：如果上次升级未完成，重新进入升级模式
	if (g_config.upgrade_status == UPGRADE_STATUS_DOWNLOADING)
	{
		upgrade_process(); // 重新进入升级流程
		// 升级失败
		NVIC_SystemReset();
	}

	// ========== 步骤5：容错机制 ==========
	// 功能：防止坏固件导致系统无法启动，失败3次自动切换备份分区
	if (!handle_boot_counter())
	{
		// 无有效固件，进入等待升级模式
		enter_upgrade_wait_mode();
	}

	// ========== 步骤6：验证并启动固件 ==========
	if (!try_boot_firmware())
	{
		// 两个分区都无效，进入等待升级模式
		enter_upgrade_wait_mode();
	}

	// 跳转失败，严重错误
	led_status_indicate(9); // 9次闪烁：未知错误
	NVIC_SystemReset();
}
