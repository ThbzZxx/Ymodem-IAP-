#ifndef __CONFIG_MANAGER_H
#define __CONFIG_MANAGER_H

#include "iap_config.h"

/**
 * @brief  读取配置区数据
 * @param  config: 配置结构体指针
 * @retval 1=成功 0=失败(配置无效)
 */
uint8_t config_read(system_config_t *config);

/**
 * @brief  保存配置到Flash
 * @param  config: 配置结构体指针
 * @retval 1=成功 0=失败
 */
uint8_t config_save(system_config_t *config);

/**
 * @brief  初始化默认配置
 * @param  config: 配置结构体指针
 * @retval None
 */
void config_init_default(system_config_t *config);

/**
 * @brief  标记固件为有效
 * @param  bank: 分区号 0=A区 1=B区
 * @param  fw_info: 固件信息
 * @retval 1=成功 0=失败
 */
uint8_t config_mark_firmware_valid(uint8_t bank, firmware_info_t *fw_info);

// ========== 配置工具函数 ==========
uint8_t init_system_config(void);
uint8_t handle_boot_counter(void);
uint8_t has_valid_firmware(void);

#endif // __CONFIG_MANAGER_H
