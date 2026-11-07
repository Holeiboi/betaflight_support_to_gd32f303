/*
*****************************************************************************
**
**  File        : gd32_flash_f303_1024k.ld
**
**  Abstract    : Linker script for GD32F303RGT6
**                1024KByte Flash (2 x 512K banks), 96KByte SRAM
**
**  Layout (config in bank0, exactly like the STM32F405 layout):
**
**  0x08000000 - 0x08003FFF   16K  vector table + startup
**  0x08004000 - 0x08007FFF   16K  config storage (8 x 2K pages)
**  0x08008000 - 0x0807FFFF  480K  firmware, rest of bank0
**  0x08080000 - 0x080FFFFF  512K  bank1 (currently unused / spare)
**
*****************************************************************************
*/

/* Specify the memory areas. */
MEMORY
{
    FLASH (rx)         : ORIGIN = 0x08000000, LENGTH = 16K
    FLASH_CONFIG (r)   : ORIGIN = 0x08004000, LENGTH = 16K
    FLASH1 (rx)        : ORIGIN = 0x08008000, LENGTH = 480K
    FLASH2 (rx)        : ORIGIN = 0x08080000, LENGTH = 512K

    SYSTEM_MEMORY (rx) : ORIGIN = 0x1FFF0000, LENGTH = 28K

    RAM (rwx)          : ORIGIN = 0x20000000, LENGTH = 96K
    MEMORY_B1 (rx)     : ORIGIN = 0x60000000, LENGTH = 0K
}

REGION_ALIAS("STACKRAM", RAM)
REGION_ALIAS("FASTRAM", RAM)
REGION_ALIAS("VECTAB", FLASH)
REGION_ALIAS("MOVABLE_FLASH", FLASH1)

INCLUDE "gd32_flash_f3_split.ld"
