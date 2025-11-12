#include "firmware_verify.h"
#include "crc32.h"
#include "config_manager.h"
#include "bootloader.h"
#include "string.h"

/**
 * @brief  验证指定分区的固件完整性
 * @param  bank: 分区号 0=A区 1=B区
 * @retval 1=固件有效 0=固件无效
 */
uint8_t firmware_verify(uint8_t bank)
{
    system_config_t config;
    firmware_info_t *fw_info;
    uint32_t app_addr;
    uint32_t calculated_crc;

    // 读取配置
    if (!config_read(&config)) {
        return 0;  // 配置无效
    }

    // 确定分区地址和信息
    if (bank == 0) {
        fw_info = &config.bank_a_info;
        app_addr = APP_A_SECTOR_ADDR;
    } else {
        fw_info = &config.bank_b_info;
        app_addr = APP_B_SECTOR_ADDR;
    }

    // 检查魔术字
    if (fw_info->magic != FIRMWARE_MAGIC) {
        return 0;
    }

    // 检查有效标志
    if (fw_info->is_valid != FIRMWARE_VALID_FLAG) {
        return 0;
    }

    // 检查固件大小
    if (fw_info->firmware_size == 0 ||
        fw_info->firmware_size > APP_BANK_SIZE) {
        return 0;
    }

    // 计算固件CRC32（跳过头部24字节，只计算实际固件）
    // 注意：firmware_size字段存储的就是固件数据的大小（不包含24字节头）
    calculated_crc = crc32_calculate_flash(app_addr + 24,
                                          fw_info->firmware_size);


    // 比对CRC32
    if (calculated_crc != fw_info->firmware_crc32) {
        return 0;  // CRC校验失败
    }

    // 检查栈指针有效性（固件头部后面就是实际APP代码）
    uint32_t stack_ptr = *(__IO uint32_t*)(app_addr + 24);
    if ((stack_ptr & 0x2FFF0000) != 0x20000000) {
        return 0;  // 栈指针无效
    }

    return 1;  // 固件有效
}

/**
 * @brief  解析固件头部信息
 * @param  addr: 固件Flash地址
 * @param  fw_info: 固件信息结构体指针（输出）
 * @retval 1=成功 0=失败
 */
uint8_t firmware_parse_header(uint32_t addr, firmware_info_t *fw_info)
{
    // 从Flash读取固件头部（24字节）
    mcu_flash_read(addr, (uint8_t*)fw_info, sizeof(firmware_info_t));

    // 验证魔术字
    if (fw_info->magic != FIRMWARE_MAGIC) {
        return 0;  // 不是有效的固件包
    }

    // 验证固件大小
    if (fw_info->firmware_size == 0 ||
        fw_info->firmware_size > APP_BANK_SIZE) {
        return 0;  // 固件大小不合法
    }

    return 1;  // 成功
}

/**
 * @brief  比较版本号
 * @param  ver1: 版本号1 [major, minor, patch]
 * @param  ver2: 版本号2 [major, minor, patch]
 * @retval >0: ver1>ver2, 0: ver1==ver2, <0: ver1<ver2
 */
int8_t firmware_compare_version(uint8_t ver1[3], uint8_t ver2[3])
{
    // 比较主版本
    if (ver1[0] > ver2[0]) return 1;
    if (ver1[0] < ver2[0]) return -1;

    // 比较次版本
    if (ver1[1] > ver2[1]) return 1;
    if (ver1[1] < ver2[1]) return -1;

    // 比较补丁版本
    if (ver1[2] > ver2[2]) return 1;
    if (ver1[2] < ver2[2]) return -1;

    return 0;  // 版本相同
}
