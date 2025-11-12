#include "bootloader.h"
#include "bsp_led.h"
#include "config_manager.h"
#include "crc32.h"
#include "firmware_verify.h"
#include "iap_config.h"
#include "sysTick.h"
#include "ymodem.h"
#include <stdio.h>

// 外部配置变量声明
extern system_config_t g_config;

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
	uint32_t stack_ptr = *(__IO uint32_t*)appxaddr;

    if ((stack_ptr & 0x2FFF0000 ) == 0x20000000)
	{
        jump_addr = *(__IO uint32_t*) (appxaddr + 4);
        jump2app = (iapfun)jump_addr;

		/* 1. 关闭全局中断 */
		__disable_irq();

		/* 2. 关闭滴答定时器 */
		SysTick->CTRL = 0;
		SysTick->LOAD = 0;
		SysTick->VAL  = 0;

		/* 3. 关闭并清除所有中断 */
		for (i = 0; i < 8; i++)
		{
			NVIC->ICER[i] = 0xFFFFFFFF;
			NVIC->ICPR[i] = 0xFFFFFFFF;
		}

		/* 4. 设置向量表偏移*/
		SCB->VTOR = appxaddr;

		/* 5. 设置栈指针 */
		__set_MSP(stack_ptr);

		/* 6. 设置为特权模式 */
		__set_CONTROL(0);
		__ISB();  /* 指令同步屏障 */

		/* 7. 跳转到APP */
		jump2app();

		/* 不应该到达这里 */
        return 1;
    }

	/* 栈指针无效 */
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

/**
 * @brief  跳转到指定分区的APP
 * @param  bank: 分区号 (0=A区, 1=B区)
 * @retval None (此函数不会返回)
 * @note   固件格式: [24字节头部] + [实际代码]
 *         跳转地址需要跳过24字节头部
 */
void jump_to_app(uint8_t bank)
{
	uint32_t app_addr;

	// 确定跳转地址（跳过固件头）
	if (bank == 0)
	{
		app_addr = APP_A_SECTOR_ADDR + 24;
	}
	else
	{
		app_addr = APP_B_SECTOR_ADDR + 24;
	}

	LED1_OFF();

	// 执行跳转
	iap_load_app(app_addr);
}

/**
 * @brief  固件升级流程处理
 * @param  None
 * @retval None
 * @note   升级流程：
 *         1. 确定目标分区（写入非激活分区）
 *         2. 通过Ymodem接收固件数据
 *         3. 验证固件头部和CRC32
 *         4. 更新配置并切换分区
 *         5. 跳转到新固件
 */
void upgrade_process(void)
{
	uint8_t target_bank;
	firmware_info_t fw_info;
	uint32_t target_addr;
	uint32_t calculated_crc;

	// ========== 步骤1：确定目标分区 ==========
	target_bank = !g_config.active_bank;
	target_addr = (target_bank == 0) ? APP_A_SECTOR_ADDR : APP_B_SECTOR_ADDR;

	// 设置升级状态为"下载中"
	g_config.upgrade_status = UPGRADE_STATUS_DOWNLOADING;
	config_save(&g_config);

	// 进入升级模式
	led_fast_blink(6, 100);

	// ========== 步骤2：通过Ymodem接收固件 ==========
	LED1_ON();

	// 重置Ymodem状态机
	ymodem_reset();

	// 设置目标地址并启动接收
	g_ymodem_target_addr = target_addr;
	ymodem_c();

	// 等待传输完成
	while (g_ymodem_success == 0)
	{
	}

	// ========== 步骤3：验证固件 ==========
	g_config.upgrade_status = UPGRADE_STATUS_VERIFYING;
	config_save(&g_config);

	// 解析固件头部（读取版本号、大小、CRC等信息）
	if (!firmware_parse_header(target_addr, &fw_info))
	{
		// 固件头部解析失败
		LED1_OFF();
		led_status_indicate(5); // 固件格式错误
		g_config.upgrade_status = UPGRADE_STATUS_FAILED;
		config_save(&g_config);
		return;
	}

	// 计算固件CRC32（跳过头部）
	calculated_crc = crc32_calculate_flash(target_addr + 24, fw_info.firmware_size);

	// 验证CRC32
	if (calculated_crc != fw_info.firmware_crc32)
	{
		// CRC32校验失败
		LED1_OFF();
		led_status_indicate(2); // CRC错误
		g_config.upgrade_status = UPGRADE_STATUS_FAILED;
		config_save(&g_config);
		return;
	}

	// ========== 步骤4：固件更新 ==========
	g_config.upgrade_status = UPGRADE_STATUS_INSTALLING;

	// 更新目标分区的固件信息
	if (target_bank == 0)
	{
		g_config.bank_a_info = fw_info;
		g_config.bank_a_info.is_valid = FIRMWARE_VALID_FLAG;
	}
	else
	{
		g_config.bank_b_info = fw_info;
		g_config.bank_b_info.is_valid = FIRMWARE_VALID_FLAG;
	}

	// 切换激活分区
	g_config.active_bank = target_bank;
	g_config.boot_count = 0; // 重置启动计数器
	g_config.upgrade_status = UPGRADE_STATUS_SUCCESS;
	config_save(&g_config);

	// ========== 步骤5：跳转到新固件 ==========
	LED1_OFF();
	led_fast_blink(10, 100);

	// 跳转到新固件（不会返回）
	jump_to_app(g_config.active_bank);
}

/**
 * @brief  尝试启动固件
 * @param  None
 * @retval 0=启动失败需要升级, 1=启动成功（不会返回）
 * @note   会先尝试当前激活分区，失败则尝试备份分区
 */
uint8_t try_boot_firmware(void)
{
	// 验证当前激活分区的固件
	if (firmware_verify(g_config.active_bank))
	{
		// 固件有效，跳转运行
		LED1_ON();
		delay_ms(200);
		LED1_OFF();
		jump_to_app(g_config.active_bank);
	}

	// 当前分区固件无效，尝试备份分区
	uint8_t backup_bank = !g_config.active_bank;

	if (firmware_verify(backup_bank))
	{
		// 备份分区有效，切换并跳转
		g_config.active_bank = backup_bank;
		g_config.boot_count = 0;
		config_save(&g_config);

		led_status_indicate(4); // 4次闪烁：分区切换
		jump_to_app(g_config.active_bank);
	}

	// 两个分区都无效
	return 0;
}

/**
 * @brief  进入无固件等待升级模式
 * @param  None
 * @retval None (此函数不会返回)
 * @note   LED持续闪烁表示等待升级
 */
void enter_upgrade_wait_mode(void)
{
	led_status_indicate(5); // 5次闪烁：无有效固件

	// 持续等待，通过串口接收升级命令
	while (1)
	{
		led_fast_blink(2, 500); // 缓慢闪烁提示用户
		delay_ms(2000);
	}
}
