#ifndef __FIRMWARE_VERIFY_H
#define __FIRMWARE_VERIFY_H

#include "iap_config.h"

/**
 * @brief  验证指定分区的固件完整性
 * @param  bank: 分区号 0=A区 1=B区
 * @retval 1=固件有效 0=固件无效
 */
uint8_t firmware_verify(uint8_t bank);

/**
 * @brief  解析固件头部信息
 * @param  addr: 固件Flash地址
 * @param  fw_info: 固件信息结构体指针（输出）
 * @retval 1=成功 0=失败
 */
uint8_t firmware_parse_header(uint32_t addr, firmware_info_t *fw_info);

/**
 * @brief  比较版本号
 * @param  ver1: 版本号1 [major, minor, patch]
 * @param  ver2: 版本号2 [major, minor, patch]
 * @retval >0: ver1>ver2, 0: ver1==ver2, <0: ver1<ver2
 */
int8_t firmware_compare_version(uint8_t ver1[3], uint8_t ver2[3]);

#endif // __FIRMWARE_VERIFY_H
