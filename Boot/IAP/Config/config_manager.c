#include "config_manager.h"
#include "bsp_led.h"
#include "bootloader.h"
#include "crc32.h"
#include "firmware_verify.h"
#include <string.h>

// 外部配置变量声明
extern system_config_t g_config;

/**
 * @brief  读取配置区数据
 * @param  config: 配置结构体指针
 * @retval 1=成功 0=失败(配置无效)
 */
uint8_t config_read(system_config_t *config)
{
    // 从Flash读取配置
    mcu_flash_read(CONFIG_AREA_ADDR, (uint8_t*)config, sizeof(system_config_t));

    // 验证魔术字
    if (config->magic != CONFIG_MAGIC) {
        return 0;  // 配置无效
    }

    // 验证CRC32
    uint32_t crc = crc32_calculate((uint8_t*)config,sizeof(system_config_t) - 4); // 减去crc32字段本身
    if (crc != config->config_crc32) {
        return 0;  // CRC校验失败
    }

    return 1;  // 配置有效
}

/**
 * @brief  保存配置到Flash
 * @param  config: 配置结构体指针
 * @retval 1=成功 0=失败
 */
uint8_t config_save(system_config_t *config)
{
    uint8_t result;

    // 计算CRC32（不包含crc32字段本身）
    config->config_crc32 = crc32_calculate((uint8_t*)config,
                                          sizeof(system_config_t) - 4);

    // 擦除配置区（2KB = 2个扇区）
    result = mcu_flash_erase(CONFIG_AREA_ADDR, CONFIG_AREA_SIZE / FLASH_SECTOR_SIZE);
    if (!result) {
        return 0;
    }

    // 写入Flash
    result = mcu_flash_write(CONFIG_AREA_ADDR,
                           (uint8_t*)config,
                           sizeof(system_config_t));

    return result;
}

/**
 * @brief  初始化默认配置
 * @param  config: 配置结构体指针
 * @retval None
 */
void config_init_default(system_config_t *config)
{
    // 清空结构体
    memset(config, 0, sizeof(system_config_t));

    // 设置基本参数
    config->magic = CONFIG_MAGIC;
    config->active_bank = 1;  // 默认B区（首次升级写入A区）
    config->upgrade_status = UPGRADE_STATUS_IDLE;
    config->boot_count = 0;
    config->max_boot_retry = 3;  // 最大重试3次

    // 初始化A区固件信息
    config->bank_a_info.magic = FIRMWARE_MAGIC;
    config->bank_a_info.version_major = 0;
    config->bank_a_info.version_minor = 0;
    config->bank_a_info.version_patch = 0;
    config->bank_a_info.firmware_size = 0;
    config->bank_a_info.firmware_crc32 = 0;
    config->bank_a_info.build_timestamp = 0;
    config->bank_a_info.is_valid = 0x00;  // 无效

    // 初始化B区固件信息
    config->bank_b_info.magic = FIRMWARE_MAGIC;
    config->bank_b_info.version_major = 0;
    config->bank_b_info.version_minor = 0;
    config->bank_b_info.version_patch = 0;
    config->bank_b_info.firmware_size = 0;
    config->bank_b_info.firmware_crc32 = 0;
    config->bank_b_info.build_timestamp = 0;
    config->bank_b_info.is_valid = 0x00;  // 无效

    // 保存到Flash
    config_save(config);
}

/**
 * @brief  标记固件为有效
 * @param  bank: 分区号 0=A区 1=B区
 * @param  fw_info: 固件信息
 * @retval 1=成功 0=失败
 */
uint8_t config_mark_firmware_valid(uint8_t bank, firmware_info_t *fw_info)
{
    system_config_t config;

    // 读取当前配置
    if (!config_read(&config)) {
        // 配置无效，初始化默认配置
        config_init_default(&config);
        if (!config_read(&config)) {
            return 0;  // 初始化失败
        }
    }

    // 更新对应分区的固件信息
    if (bank == 0) {
        memcpy(&config.bank_a_info, fw_info, sizeof(firmware_info_t));
        config.bank_a_info.is_valid = FIRMWARE_VALID_FLAG;
    } else {
        memcpy(&config.bank_b_info, fw_info, sizeof(firmware_info_t));
        config.bank_b_info.is_valid = FIRMWARE_VALID_FLAG;
    }

    // 保存配置
    return config_save(&config);
}

/**
 * @brief  初始化系统配置
 * @param  None
 * @retval 0=失败, 1=成功
 * @note   首次使用或配置损坏时会初始化默认配置
 */
uint8_t init_system_config(void)
{
	uint8_t config_valid;

	// 读取配置区
	config_valid = config_read(&g_config);

	if (!config_valid)
	{
		// 初始化默认配置
		led_status_indicate(1);
		config_init_default(&g_config);

		// 重新读取验证
		if (!config_read(&g_config))
		{
			// 配置区初始化失败，严重错误（不应该发生）
			return 0;
		}
	}

	return 1;
}

/**
 * @brief  处理启动计数器和分区切换逻辑
 * @param  None
 * @retval 0=需要进入升级模式, 1=继续启动流程
 * @note   如果当前分区启动失败超过3次，会自动切换到备份分区
 *         此函数只负责计数器管理和分区切换决策，不执行跳转
 */
uint8_t handle_boot_counter(void)
{
	// 检查是否有任何有效固件
	if (!has_valid_firmware())
	{
		// 没有任何有效固件，需要进入升级模式
		return 0;
	}

	// 递增启动计数器
	g_config.boot_count++;

	// 检查是否超过最大重试次数
	if (g_config.boot_count > g_config.max_boot_retry)
	{
		// 超过最大重试次数，切换到备份分区
		g_config.active_bank = !g_config.active_bank;
		g_config.boot_count = 0;
		config_save(&g_config);

		led_status_indicate(4); // 4次闪烁：分区回退

		// 注意：不在这里跳转，让步骤6统一处理跳转
	}
	else
	{
		// 保存增加的启动计数器
		config_save(&g_config);
	}

	return 1; // 继续启动流程
}

/**
 * @brief  检查是否有任何有效的固件
 * @param  None
 * @retval 0=无有效固件, 1=有有效固件
 */
uint8_t has_valid_firmware(void)
{
	if (firmware_verify(g_config.active_bank) || firmware_verify(!g_config.active_bank))
	{
		return 1;
	}
	return 0;
}
