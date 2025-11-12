#ifndef __CRC32_H
#define __CRC32_H

#include "stdint.h"

/**
 * @brief  计算数据的CRC32校验值
 * @param  data: 数据指针
 * @param  length: 数据长度
 * @retval CRC32校验值
 */
uint32_t crc32_calculate(const uint8_t *data, uint32_t length);

/**
 * @brief  计算Flash区域的CRC32校验值
 * @param  addr: Flash起始地址
 * @param  length: 数据长度
 * @retval CRC32校验值
 */
uint32_t crc32_calculate_flash(uint32_t addr, uint32_t length);

#endif // __CRC32_H
