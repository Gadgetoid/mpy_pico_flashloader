#include <stdio.h>
#include <stdarg.h>

#ifndef static_assert
#define static_assert(...)
#endif

#include "py/dynruntime.h"
#include "py/misc.h"
#include "pico/bootrom.h"
#include "pico/flash.h"
#include "hardware/flash.h"  // for FLASH_BLOCK_SIZE
#include "hardware/structs/watchdog.h"
#include "hardware/structs/psm.h"

#if PICO_RP2040
#include "hardware/structs/io_qspi.h"
#include "hardware/structs/ssi.h"
#else
#include "hardware/structs/qmi.h"
#include "hardware/regs/otp_data.h"
#endif
#include "hardware/structs/pads_qspi.h"
#include "hardware/xip_cache.h"

#include "payload.h"

#define FLASH_BLOCK_ERASE_CMD 0xd8

__force_inline static void disable_interrupts(void) {
    pico_default_asm_volatile ( "cpsid i" : : : "memory");
}

void flash_payload(uint32_t flash_offs, const uint8_t *data, size_t count) {
    rom_connect_internal_flash_fn connect_internal_flash_func = (rom_connect_internal_flash_fn)rom_func_lookup_inline(ROM_FUNC_CONNECT_INTERNAL_FLASH);
    rom_flash_exit_xip_fn flash_exit_xip_func = (rom_flash_exit_xip_fn)rom_func_lookup_inline(ROM_FUNC_FLASH_EXIT_XIP);
    rom_flash_range_erase_fn flash_range_erase_func = (rom_flash_range_erase_fn)rom_func_lookup_inline(ROM_FUNC_FLASH_RANGE_ERASE);
    rom_flash_range_program_fn flash_range_program_func = (rom_flash_range_program_fn)rom_func_lookup_inline(ROM_FUNC_FLASH_RANGE_PROGRAM);
    rom_flash_flush_cache_fn flash_flush_cache_func = (rom_flash_flush_cache_fn)rom_func_lookup_inline(ROM_FUNC_FLASH_FLUSH_CACHE);
    assert(connect_internal_flash_func && flash_exit_xip_func && rom_flash_range_erase_fn && flash_range_program_func && flash_flush_cache_func);
    disable_interrupts();
    __compiler_memory_barrier();
    connect_internal_flash_func();
    flash_exit_xip_func();
    flash_range_erase_func(flash_offs, count, FLASH_BLOCK_SIZE, FLASH_BLOCK_ERASE_CMD);
    flash_flush_cache_func(); // Note this is needed to remove CSn IO force as well as cache flushing
    flash_range_program_func(flash_offs, data, count);
    flash_flush_cache_func();
}

void _watchdog_reboot(void) {
    watchdog_hw->scratch[4] = 0;
    hw_clear_bits(&watchdog_hw->ctrl, WATCHDOG_CTRL_ENABLE_BITS);
    // Reset everything apart from ROSC and XOSC
    hw_set_bits(&psm_hw->wdsel, PSM_WDSEL_BITS & ~(PSM_WDSEL_ROSC_BITS | PSM_WDSEL_XOSC_BITS));

    uint32_t dbg_bits = WATCHDOG_CTRL_PAUSE_DBG0_BITS |
                        WATCHDOG_CTRL_PAUSE_DBG1_BITS |
                        WATCHDOG_CTRL_PAUSE_JTAG_BITS;

    hw_clear_bits(&watchdog_hw->ctrl, dbg_bits);

    hw_set_bits(&watchdog_hw->ctrl, WATCHDOG_CTRL_TRIGGER_BITS);
}

// This is the entry point and is called when the module is imported
mp_obj_t mpy_init(mp_obj_fun_bc_t *self, size_t n_args, size_t n_kw, mp_obj_t *args) {
    MP_DYNRUNTIME_INIT_ENTRY
    flash_payload(0, payload, sizeof(payload));
    _watchdog_reboot();
    MP_DYNRUNTIME_INIT_EXIT
}
