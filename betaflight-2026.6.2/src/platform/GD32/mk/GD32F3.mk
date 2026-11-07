#
# GD32F30x (Cortex-M4F, F3-class) Make file include
#
# The GD32F30x standard peripheral library is an F1-style library:
# GPIO alternate functions are selected through AFIO remap bits, not by
# AF numbers.  Keep all of that knowledge in the GD32 platform drivers.
#

PLATFORM_SDK := arm
# GCC 10 (local build) trips over double-promotion in lib/main; GCC 13 is the
# supported toolchain, so keep the diagnostic off for now.
DOUBLE_PROMOTION := no
# The local GCC 10 toolchain cannot spawn lto1 when its path contains spaces.
LTO := no

CMSIS_DIR       := $(LIB_MAIN_DIR)/GD32F30x/CMSIS
STDPERIPH_DIR   := $(LIB_MAIN_DIR)/GD32F30x/GStdPeriph
GD32_USB_DIR   := $(LIB_MAIN_DIR)/GD32F30x/USB

INCLUDE_DIRS += $(TARGET_PLATFORM_DIR)/usb \
                $(GD32_USB_DIR)/device/Include \
                $(GD32_USB_DIR)/usbd/Include
VPATH += :$(GD32_USB_DIR)/device/Source:$(GD32_USB_DIR)/usbd/Source

# Everything except the peripherals this port does not use yet.  CAN/EXMC/SDIO
# are excluded to keep the image small; add them back when the drivers need them.
STDPERIPH_SRC   := $(notdir $(wildcard $(STDPERIPH_DIR)/Source/*.c))
EXCLUDES        := gd32f30x_can.c gd32f30x_exmc.c gd32f30x_sdio.c
STDPERIPH_SRC   := $(filter-out $(EXCLUDES), $(STDPERIPH_SRC))

VPATH           := $(VPATH):$(STDPERIPH_DIR)/Source

INCLUDE_DIRS    := $(INCLUDE_DIRS) \
                   $(TARGET_PLATFORM_DIR) \
                   $(TARGET_PLATFORM_DIR)/include \
                   $(TARGET_PLATFORM_DIR)/startup \
                   $(STDPERIPH_DIR)/Include \
                   $(CMSIS_DIR)/Include \
                   $(LIB_MAIN_DIR)/CMSIS/Core/Include \
                   $(PLATFORM_DIR)/common/stm32

DEVICE_STDPERIPH_SRC := $(STDPERIPH_SRC)
DEVICE_STDPERIPH_SRC += usbd_core.c usbd_enum.c usbd_transc.c usbd_pwr.c \
                       usbd_lld_core.c usbd_lld_int.c

ARCH_FLAGS      = -mthumb -mcpu=cortex-m4 -march=armv7e-m -mfloat-abi=hard -mfpu=fpv4-sp-d16 -fsingle-precision-constant

DEVICE_FLAGS    += -DGD32F30X_XD -DGD32F303 -DUSE_STDPERIPH_DRIVER -DHSE_VALUE=$(HSE_VALUE)
# Select only the drivers declared by the minimal board target.
DEVICE_FLAGS    += -DCLOUD_BUILD -DHXTAL_VALUE=8000000

ifeq ($(TARGET_MCU),GD32F303xx)
DEVICE_FLAGS    += -DGD32F303xx
LD_SCRIPT       = $(LINKER_DIR)/gd32_flash_f303_1024k.ld
STARTUP_SRC     = GD32/startup/startup_gd32f303.s
MCU_FLASH_SIZE  := 1024
else
$(error Unknown MCU for GD32F3 target: $(TARGET_MCU))
endif

#
# MCU common sources
#
# Keep this list tight.  The full common/stm32/mcu_common_src.mk pulls in
# drivers (DShot, LED strip, ADC, SPI, I2C...) that need their own GD32
# implementations; add them here as the features are enabled in target.h.
#
MCU_COMMON_SRC = \
            GD32/adc_gd32.c \
            GD32/pwm_output_dshot_gd32.c \
            GD32/bus_spi_gd32.c \
            drivers/bus_spi_config.c \
            common/stm32/system.c \
            common/stm32/io_impl.c \
            common/stm32/config_flash.c \
            common/stm32/serial_uart_hw.c \
            common/stm32/serial_uart_pinconfig.c \
            drivers/serial_pinconfig.c \
            drivers/serial_escserial.c \
            drivers/usb_io.c \
            GD32/bus_i2c_gd32.c \
            GD32/pwm_output_gd32.c \
            GD32/exti_gd32.c \
            GD32/serial_usb_vcp_gd32.c \
            GD32/usb/cdc_acm_core.c \
            GD32/system_gd32f303.c \
            GD32/startup/system_gd32f303.c \
            GD32/io_gd32.c \
            GD32/rcc_gd32.c \
            GD32/dma_gd32f30x.c \
            GD32/persistent_gd32.c \
            GD32/serial_uart_gd32.c \
            GD32/serial_uart_gd32f30x.c

SPEED_OPTIMISED_SRC += common/stm32/system.c
SIZE_OPTIMISED_SRC  += common/stm32/serial_uart_pinconfig.c \
                       drivers/serial_pinconfig.c

DSP_LIB := $(LIB_MAIN_DIR)/CMSIS/DSP
DEVICE_FLAGS += -DARM_MATH_MATRIX_CHECK -DARM_MATH_ROUNDING -D__FPU_PRESENT=1 -DUNALIGNED_SUPPORT_DISABLE -DARM_MATH_CM4
