# Location of top-level MicroPython directory
MPY_DIR = ../micropython-embed/
PICO_SDK_DIR = ${MPY_DIR}/lib/pico-sdk

# Name of module
MOD = bootstrap

# Source files (.c or .py)
SRC = main.c lib1funcs.S #${PICO_SDK_DIR}/src/rp2_common/pico_mem_ops/mem_ops_aeabi.S

# Architecture to build for (x86, x64, armv7m, xtensa, xtensawin, rv32imc)
ARCH = armv6m

CFLAGS = -Wno-discarded-qualifiers -DL_thumb1_case_uqi -DL_thumb1_case_shi -DL_thumb1_case_sqi -DL_udivsi3 -DL_dvmd_tls
#CFLAGS += -DNO_PICO_PLATFORM
CFLAGS += -DPICO_RP2040

CFLAGS += -Wno-unused-function

# For Pico pico_mem_ops
#CFLAGS += -I${PICO_SDK_DIR}/src/host/pico_runtime/include
#CFLAGS += -I${PICO_SDK_DIR}/src/rp2_common/pico_runtime_init/include

CFLAGS += -I${PICO_SDK_DIR}/src/rp2040/pico_platform/include
CFLAGS += -I${PICO_SDK_DIR}/src/rp2040/hardware_regs/include
CFLAGS += -I${PICO_SDK_DIR}/src/rp2040/hardware_structs/include

CFLAGS += -I${PICO_SDK_DIR}/src/common/pico_base_headers/include

CFLAGS += -I${PICO_SDK_DIR}/src/rp2_common/pico_bootrom/include
CFLAGS += -I${PICO_SDK_DIR}/src/rp2_common/boot_bootrom_headers/include
CFLAGS += -I${PICO_SDK_DIR}/src/rp2_common/pico_flash/include
CFLAGS += -I${PICO_SDK_DIR}/src/rp2_common/hardware_base/include
CFLAGS += -I${PICO_SDK_DIR}/src/rp2_common/hardware_boot_lock/include
CFLAGS += -I${PICO_SDK_DIR}/src/rp2_common/hardware_flash/include
CFLAGS += -I${PICO_SDK_DIR}/src/rp2_common/hardware_sync/include
CFLAGS += -I${PICO_SDK_DIR}/src/rp2_common/hardware_xip_cache/include


CFLAGS += -I${PICO_SDK_DIR}/src/rp2_common/pico_platform_compiler/include
CFLAGS += -I${PICO_SDK_DIR}/src/rp2_common/pico_platform_sections/include
CFLAGS += -I${PICO_SDK_DIR}/src/rp2_common/pico_platform_panic/include

# Include to get the rules for compiling and linking the module
include $(MPY_DIR)/py/dynruntime.mk
