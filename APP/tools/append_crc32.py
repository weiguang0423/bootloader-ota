#!/usr/bin/env python3
"""
固件 CRC32 附加工具（STM32F4 硬件 CRC 兼容）

用法:
    py append_crc32.py <input.bin> <output.bin>

在固件末尾附加 4 字节小端序 CRC32，与 STM32F4 内置硬件 CRC 单元结果一致。
OTA 下载完成后会调用 HAL_CRC_Calculate 校验最后 4 字节。

注意：STM32F4 硬件 CRC 固定使用多项式 0x04C11DB7、初始值 0xFFFFFFFF，
输入/输出不反转、不按字节反转，按 32-bit 小端序字处理。
"""

import sys
import struct


def calc_stm32f4_hw_crc(data: bytes) -> int:
    """模拟 STM32F4 硬件 CRC 单元（与 HAL_CRC_Calculate 结果一致）。"""
    crc = 0xFFFFFFFF
    poly = 0x04C11DB7

    # 填充到 4 字节对齐（不足部分补 0，与 STM32 尾部处理一致）
    pad_len = (4 - len(data) % 4) % 4
    padded = data + b"\x00" * pad_len

    for i in range(0, len(padded), 4):
        # 小端序读取 32-bit 字，与 ARM Cortex-M 一致
        word = (
            padded[i]
            | (padded[i + 1] << 8)
            | (padded[i + 2] << 16)
            | (padded[i + 3] << 24)
        )
        crc ^= word
        for _ in range(32):
            if crc & 0x80000000:
                crc = ((crc << 1) & 0xFFFFFFFF) ^ poly
            else:
                crc = (crc << 1) & 0xFFFFFFFF

    return crc


def main():
    if len(sys.argv) != 3:
        print(f"Usage: {sys.argv[0]} <input.bin> <output.bin>")
        sys.exit(1)

    in_path = sys.argv[1]
    out_path = sys.argv[2]

    with open(in_path, "rb") as f:
        firmware = f.read()

    if len(firmware) < 4:
        print("Error: firmware too small")
        sys.exit(1)

    if len(firmware) % 4 != 0:
        print(f"Warning: firmware size {len(firmware)} is not 4-byte aligned, "
              f"padding {4 - len(firmware) % 4} bytes for CRC calculation")

    crc = calc_stm32f4_hw_crc(firmware)
    print(f"Firmware size: {len(firmware)} bytes")
    print(f"STM32F4 HW CRC: 0x{crc:08X}")

    with open(out_path, "wb") as f:
        f.write(firmware)
        f.write(struct.pack("<I", crc))

    print(f"Output: {out_path} ({len(firmware) + 4} bytes)")


if __name__ == "__main__":
    main()
