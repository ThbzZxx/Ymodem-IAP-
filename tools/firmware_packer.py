"""
固件打包工具 - 为APP.bin添加固件信息头

功能：
1. 读取原始APP.bin文件
2. 计算固件CRC32
3. 在文件前添加28字节固件信息头
4. 生成新的固件包

使用方法：
    python firmware_packer.py <输入.bin> <版本号> <输出.bin>

示例：
    python firmware_packer.py app.bin 1.0.0 app_v1.0.0.bin -打包固件
    python firmware_packer.py info app_v1.0.0.bin -查看固件包信息
"""

import struct
import binascii
import sys
import os
from datetime import datetime

class FirmwarePacker:
    def __init__(self):
        self.MAGIC = 0x5AA5F00F
        self.VALID_FLAG = 0xAA

    def calculate_crc32(self, data):
        """计算CRC32校验值（与STM32端算法一致）"""
        return binascii.crc32(data) & 0xFFFFFFFF

    def pack_firmware(self, bin_file, version, output_file):
        """
        打包固件

        参数：
            bin_file: 输入的原始bin文件路径
            version: 版本号字符串，格式："major.minor.patch" 如 "1.0.0"
            output_file: 输出的打包固件文件路径
        """
        # 检查输入文件是否存在
        if not os.path.exists(bin_file):
            print(f"错误：输入文件不存在 - {bin_file}")
            return False

        # 读取原始bin文件
        print(f"\n正在读取固件文件: {bin_file}")
        with open(bin_file, 'rb') as f:
            firmware_data = f.read()

        firmware_size = len(firmware_data)
        print(f"固件大小: {firmware_size} 字节 ({firmware_size/1024:.2f} KB)")

        # 检查固件大小（不能超过24KB）
        MAX_SIZE = 24 * 1024  # 24KB
        if firmware_size > MAX_SIZE:
            print(f"\n警告：固件大小 {firmware_size/1024:.2f}KB 超过限制 {MAX_SIZE/1024}KB")
            print("新方案中每个APP分区只有24KB！")
            response = input("是否继续打包？(y/n): ")
            if response.lower() != 'y':
                return False

        # 计算固件CRC32
        firmware_crc = self.calculate_crc32(firmware_data)
        print(f"固件CRC32: 0x{firmware_crc:08X}")

        # 解析版本号
        try:
            ver_parts = version.split('.')
            ver_major = int(ver_parts[0])
            ver_minor = int(ver_parts[1]) if len(ver_parts) > 1 else 0
            ver_patch = int(ver_parts[2]) if len(ver_parts) > 2 else 0
        except (ValueError, IndexError):
            print(f"错误：版本号格式不正确 - {version}")
            print("正确格式：major.minor.patch  例如：1.0.0")
            return False

        # 检查版本号范围
        if ver_major > 255 or ver_minor > 255 or ver_patch > 255:
            print("错误：版本号各部分必须在0-255之间")
            return False

        print(f"固件版本: {ver_major}.{ver_minor}.{ver_patch}")

        # 获取时间戳（Unix时间戳）
        timestamp = int(datetime.now().timestamp())
        print(f"编译时间戳: {timestamp} ({datetime.fromtimestamp(timestamp).strftime('%Y-%m-%d %H:%M:%S')})")

        # 构建固件信息头（24字节）
        # 结构：magic(4) + version(4) + size(4) + crc(4) + timestamp(4) + valid(4) = 24
        header = struct.pack('<I',      # magic
                           self.MAGIC)
        header += struct.pack('<BBBB',  # version_major, minor, patch, reserved1
                            ver_major,
                            ver_minor,
                            ver_patch,
                            0)  # reserved1
        header += struct.pack('<I',     # firmware_size
                            firmware_size)
        header += struct.pack('<I',     # firmware_crc32
                            firmware_crc)
        header += struct.pack('<I',     # build_timestamp
                            timestamp)
        header += struct.pack('<BBBB',  # is_valid + reserved2[3]
                            self.VALID_FLAG,
                            0, 0, 0)  # reserved2

        # 验证头部大小
        if len(header) != 24:
            print(f"错误：头部大小不正确 ({len(header)} 字节，应为 24 字节)")
            return False

        # 写入输出文件：头部(24B) + 固件数据
        print(f"\n正在生成固件包: {output_file}")
        with open(output_file, 'wb') as f:
            f.write(header)
            f.write(firmware_data)

        output_size = os.path.getsize(output_file)
        print(f"\n固件包生成完成！")
        print(f"  输出文件: {output_file}")
        print(f"  总大小: {output_size} 字节 ({output_size/1024:.2f} KB)")
        print(f"  头部: 24 字节")
        print(f"  固件: {firmware_size} 字节")
        print(f"  版本: v{ver_major}.{ver_minor}.{ver_patch}")
        print(f"  CRC32: 0x{firmware_crc:08X}")
        print(f"\n请使用 UpdateUI.py 发送此固件包到设备")

        return True

    def unpack_firmware(self, packed_file):
        """
        解析固件包，显示固件信息

        参数：
            packed_file: 打包后的固件文件
        """
        if not os.path.exists(packed_file):
            print(f"错误：文件不存在 - {packed_file}")
            return False

        with open(packed_file, 'rb') as f:
            header_data = f.read(24)

        if len(header_data) < 24:
            print("错误：文件太小，不是有效的固件包")
            return False

        # 解析头部
        magic = struct.unpack('<I', header_data[0:4])[0]
        ver_major = header_data[4]
        ver_minor = header_data[5]
        ver_patch = header_data[6]
        reserved1 = header_data[7]
        firmware_size = struct.unpack('<I', header_data[8:12])[0]
        firmware_crc32 = struct.unpack('<I', header_data[12:16])[0]
        build_timestamp = struct.unpack('<I', header_data[16:20])[0]
        is_valid = header_data[20]

        # 验证魔术字
        if magic != self.MAGIC:
            print(f"警告：魔术字不匹配 (0x{magic:08X} != 0x{self.MAGIC:08X})")
            print("这可能不是一个有效的固件包")
            return False

        print(f"\n固件包信息：")
        print(f"  文件: {packed_file}")
        print(f"  版本: v{ver_major}.{ver_minor}.{ver_patch}")
        print(f"  固件大小: {firmware_size} 字节 ({firmware_size/1024:.2f} KB)")
        print(f"  CRC32: 0x{firmware_crc32:08X}")
        print(f"  编译时间: {datetime.fromtimestamp(build_timestamp).strftime('%Y-%m-%d %H:%M:%S')}")
        print(f"  有效标志: 0x{is_valid:02X} {'(有效)' if is_valid == self.VALID_FLAG else '(无效)'}")
        print(f"  总大小: {os.path.getsize(packed_file)} 字节")

        return True

def print_usage():
    print("\n固件打包工具")
    print("=" * 50)
    print("\n用法1 - 打包固件：")
    print("  python firmware_packer.py pack <输入.bin> <版本号> <输出.bin>")
    print("\n用法2 - 查看固件信息：")
    print("  python firmware_packer.py info <固件包.bin>")
    print("\n示例：")
    print("  python firmware_packer.py pack app.bin 1.0.0 app_v1.0.0.bin")
    print("  python firmware_packer.py info app_v1.0.0.bin")
    print("\n版本号格式：major.minor.patch")
    print("  例如：1.0.0, 2.1.5, 0.9.15")
    print("\n注意事项：")
    print("  - 固件大小不应超过 24KB (新方案限制)")
    print("  - 打包后的文件比原文件多 28 字节（固件头部）")
    print("  - 使用 UpdateUI.py 发送打包后的固件")
    print("=" * 50)

def main():
    packer = FirmwarePacker()

    # 检查参数
    if len(sys.argv) < 2:
        print_usage()
        sys.exit(1)

    command = sys.argv[1].lower()

    if command == 'pack':
        # 打包模式
        if len(sys.argv) != 5:
            print("错误：参数数量不正确")
            print_usage()
            sys.exit(1)

        input_file = sys.argv[2]
        version = sys.argv[3]
        output_file = sys.argv[4]

        success = packer.pack_firmware(input_file, version, output_file)
        sys.exit(0 if success else 1)

    elif command == 'info':
        # 信息查看模式
        if len(sys.argv) != 3:
            print("错误：参数数量不正确")
            print_usage()
            sys.exit(1)

        packed_file = sys.argv[2]
        success = packer.unpack_firmware(packed_file)
        sys.exit(0 if success else 1)

    else:
        # 兼容旧用法（不带命令）
        if len(sys.argv) == 4:
            input_file = sys.argv[1]
            version = sys.argv[2]
            output_file = sys.argv[3]
            success = packer.pack_firmware(input_file, version, output_file)
            sys.exit(0 if success else 1)
        else:
            print(f"错误：未知命令 '{command}'")
            print_usage()
            sys.exit(1)

if __name__ == "__main__":
    main()
