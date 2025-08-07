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
//#include "hardware/sync.h" // to save/restore interrupts

#if PICO_RP2040
#include "hardware/structs/io_qspi.h"
#include "hardware/structs/ssi.h"
#else
#include "hardware/structs/qmi.h"
#include "hardware/regs/otp_data.h"
#endif
#include "hardware/structs/pads_qspi.h"
#include "hardware/xip_cache.h"

#define NANOPRINTF_IMPLEMENTATION
#define NANOPRINTF_USE_FIELD_WIDTH_FORMAT_SPECIFIERS 1
#define NANOPRINTF_USE_PRECISION_FORMAT_SPECIFIERS 1
#define NANOPRINTF_USE_FLOAT_FORMAT_SPECIFIERS 0
#define NANOPRINTF_USE_LARGE_FORMAT_SPECIFIERS 0
#define NANOPRINTF_USE_BINARY_FORMAT_SPECIFIERS 0
#define NANOPRINTF_USE_WRITEBACK_FORMAT_SPECIFIERS 0
#include "nanoprintf.h"

#define FLASH_BLOCK_ERASE_CMD 0xd8

__force_inline static uint32_t save_and_disable_interrupts(void) {
    uint32_t status;
    pico_default_asm_volatile (
            "mrs %0, PRIMASK\n"
            "cpsid i"
            : "=r" (status) :: "memory");
    return status;
}

__force_inline static void restore_interrupts(uint32_t status) {
    pico_default_asm_volatile ("msr PRIMASK,%0"::"r" (status) : "memory" );
}

void *memset (void *dst, int val, size_t len) {
  unsigned char *p = dst;
  while(len > 0){
      *p++ = val;
      p++;
      len--;
  }
  return dst;
}

#define BOOT2_SIZE_WORDS 64

uint32_t boot2_copyout[BOOT2_SIZE_WORDS];
bool boot2_copyout_valid = false;

static void flash_init_boot2_copyout(void) {
    if (boot2_copyout_valid)
        return;
    // todo we may want the option of boot2 just being a free function in
    //      user RAM, e.g. if it is larger than 256 bytes
#if PICO_RP2040
    const volatile uint32_t *copy_from = (uint32_t *)XIP_BASE;
#else
    const volatile uint32_t *copy_from = (uint32_t *)BOOTRAM_BASE;
#endif
    for (int i = 0; i < BOOT2_SIZE_WORDS; ++i)
        boot2_copyout[i] = copy_from[i];
    __compiler_memory_barrier();
    boot2_copyout_valid = true;
}


static void flash_enable_xip_via_boot2(void) {
    ((void (*)(void))((intptr_t)boot2_copyout+1))();
}


//-----------------------------------------------------------------------------
// State save/restore

// Most functions save and restore the QSPI pad state over the call. (The main
// exception is flash_start_xip() which is explicitly intended to initialise
// them). The expectation is that by the time you do any flash operations,
// you have either gone through a normal flash boot process or (in the case
// of PICO_NO_FLASH=1) you have called flash_start_xip(). Any further
// modifications to the pad state are therefore deliberate changes that we
// should preserve.
//
// Additionally, on RP2350, we save and restore the window 1 QMI configuration
// if the user has not opted into bootrom CS1 support via FLASH_DEVINFO OTP
// flags. This avoids clobbering CS1 setup (e.g. PSRAM) performed by the
// application.

#if !PICO_RP2040
// This is specifically for saving/restoring the registers modified by RP2350
// flash_exit_xip() ROM func, not the entirety of the QMI window state.
typedef struct flash_rp2350_qmi_save_state {
    uint32_t timing;
    uint32_t rcmd;
    uint32_t rfmt;
} flash_rp2350_qmi_save_state_t;

static void flash_rp2350_save_qmi_cs1(flash_rp2350_qmi_save_state_t *state) {
    state->timing = qmi_hw->m[1].timing;
    state->rcmd = qmi_hw->m[1].rcmd;
    state->rfmt = qmi_hw->m[1].rfmt;
}

static void flash_rp2350_restore_qmi_cs1(const flash_rp2350_qmi_save_state_t *state) {
    if (flash_devinfo_get_cs_size(1) == FLASH_DEVINFO_SIZE_NONE) {
        // Case 1: The RP2350 ROM sets QMI to a clean (03h read) configuration
        // during flash_exit_xip(), even though when CS1 is not enabled via
        // FLASH_DEVINFO it does not issue an XIP exit sequence to CS1. In
        // this case, restore the original register config for CS1 as it is
        // still the correct config.
        qmi_hw->m[1].timing = state->timing;
        qmi_hw->m[1].rcmd = state->rcmd;
        qmi_hw->m[1].rfmt = state->rfmt;
    } else {
        // Case 2: If RAM is attached to CS1, and the ROM has issued an XIP
        // exit sequence to it, then the ROM re-initialisation of the QMI
        // registers has actually not gone far enough. The old XIP write mode
        // is no longer valid when the QSPI RAM is returned to a serial
        // command state. Restore the default 02h serial write command config.
        qmi_hw->m[1].wfmt = QMI_M1_WFMT_RESET;
        qmi_hw->m[1].wcmd = QMI_M1_WCMD_RESET;
    }
}
#endif


typedef struct flash_hardware_save_state {
#if !PICO_RP2040
    flash_rp2350_qmi_save_state_t qmi_save;
#endif
    uint32_t qspi_pads[count_of(pads_qspi_hw->io)];
} flash_hardware_save_state_t;

static void flash_save_hardware_state(flash_hardware_save_state_t *state) {
    // Commit any pending writes to external RAM, to avoid losing them in a subsequent flush:
    xip_cache_clean_all();
    for (size_t i = 0; i < count_of(pads_qspi_hw->io); ++i) {
        state->qspi_pads[i] = pads_qspi_hw->io[i];
    }
#if !PICO_RP2040
    flash_rp2350_save_qmi_cs1(&state->qmi_save);
#endif
}

static void flash_restore_hardware_state(flash_hardware_save_state_t *state) {
    for (size_t i = 0; i < count_of(pads_qspi_hw->io); ++i) {
        pads_qspi_hw->io[i] = state->qspi_pads[i];
    }
#if !PICO_RP2040
    // Tail call!
    flash_rp2350_restore_qmi_cs1(&state->qmi_save);
#endif
}

void flash_range_erase(uint32_t flash_offs, size_t count) {
    /*
#ifdef PICO_FLASH_SIZE_BYTES
    debug("flash_offs + count <= PICO_FLASH_SIZE_BYTES");
#endif
    invalid_params_if(HARDWARE_FLASH, flash_offs & (FLASH_SECTOR_SIZE - 1));
    invalid_params_if(HARDWARE_FLASH, count & (FLASH_SECTOR_SIZE - 1));
*/
    uint32_t interrupt_state = save_and_disable_interrupts();

    rom_connect_internal_flash_fn connect_internal_flash_func = (rom_connect_internal_flash_fn)rom_func_lookup_inline(ROM_FUNC_CONNECT_INTERNAL_FLASH);
    rom_flash_exit_xip_fn flash_exit_xip_func = (rom_flash_exit_xip_fn)rom_func_lookup_inline(ROM_FUNC_FLASH_EXIT_XIP);
    rom_flash_range_erase_fn flash_range_erase_func = (rom_flash_range_erase_fn)rom_func_lookup_inline(ROM_FUNC_FLASH_RANGE_ERASE);
    rom_flash_flush_cache_fn flash_flush_cache_func = (rom_flash_flush_cache_fn)rom_func_lookup_inline(ROM_FUNC_FLASH_FLUSH_CACHE);
    assert(connect_internal_flash_func && flash_exit_xip_func && flash_range_erase_func && flash_flush_cache_func);
    flash_init_boot2_copyout();
    flash_hardware_save_state_t state;
    flash_save_hardware_state(&state);

    // No flash accesses after this point
    __compiler_memory_barrier();

    connect_internal_flash_func();
    flash_exit_xip_func();
    flash_range_erase_func(flash_offs, count, FLASH_BLOCK_SIZE, FLASH_BLOCK_ERASE_CMD);
    flash_flush_cache_func(); // Note this is needed to remove CSn IO force as well as cache flushing
    flash_enable_xip_via_boot2();
    flash_restore_hardware_state(&state);

    restore_interrupts(interrupt_state);
}


void flash_range_program(uint32_t flash_offs, const uint8_t *data, size_t count) {
    /*
#ifdef PICO_FLASH_SIZE_BYTES
    debug("flash_offs + count <= PICO_FLASH_SIZE_BYTES");
#endif
    invalid_params_if(HARDWARE_FLASH, flash_offs & (FLASH_PAGE_SIZE - 1));
    invalid_params_if(HARDWARE_FLASH, count & (FLASH_PAGE_SIZE - 1));
*/
    uint32_t interrupt_state = save_and_disable_interrupts();

    rom_connect_internal_flash_fn connect_internal_flash_func = (rom_connect_internal_flash_fn)rom_func_lookup_inline(ROM_FUNC_CONNECT_INTERNAL_FLASH);
    rom_flash_exit_xip_fn flash_exit_xip_func = (rom_flash_exit_xip_fn)rom_func_lookup_inline(ROM_FUNC_FLASH_EXIT_XIP);
    rom_flash_range_program_fn flash_range_program_func = (rom_flash_range_program_fn)rom_func_lookup_inline(ROM_FUNC_FLASH_RANGE_PROGRAM);
    rom_flash_flush_cache_fn flash_flush_cache_func = (rom_flash_flush_cache_fn)rom_func_lookup_inline(ROM_FUNC_FLASH_FLUSH_CACHE);
    assert(connect_internal_flash_func && flash_exit_xip_func && flash_range_program_func && flash_flush_cache_func);
    flash_init_boot2_copyout();
    flash_hardware_save_state_t state;
    flash_save_hardware_state(&state);

    __compiler_memory_barrier();

    connect_internal_flash_func();
    flash_exit_xip_func();
    flash_range_program_func(flash_offs, data, count);
    flash_flush_cache_func(); // Note this is needed to remove CSn IO force as well as cache flushing
    flash_enable_xip_via_boot2();

    flash_restore_hardware_state(&state);

    restore_interrupts(interrupt_state);
}


static void debug(char *fmt, ...)
{   
    char buf[512];
    va_list args;
    va_start(args, fmt);
    //sprintf(buf, fmt, args);
    npf_vsnprintf(buf, sizeof(buf), fmt, args);
    mp_printf(&mp_plat_print, "%s\n", buf);
    va_end(args);
}

// A simple function that adds its 2 arguments (must be integers)
static mp_obj_t init() {
    debug("Hello World");

    int offset = 2 * 1024 * 1024;
    offset -= 4096;

    flash_range_erase(offset, 128);
    uint8_t *data = m_malloc(128);
    data[0] = 'h';
    data[1] = 'e';
    data[2] = 'l';
    data[3] = 'l';
    data[4] = 'o';
    for(int i = 5; i < 128; i++){
        data[i] = 61;
    }
    flash_range_program(offset, data, 128);
    debug("Done!");
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(init_obj, init);

// This is the entry point and is called when the module is imported
mp_obj_t mpy_init(mp_obj_fun_bc_t *self, size_t n_args, size_t n_kw, mp_obj_t *args) {
    // This must be first, it sets up the globals dict and other things
    MP_DYNRUNTIME_INIT_ENTRY

    // Make the functions available in the module's namespace
    mp_store_global(MP_QSTR_init, MP_OBJ_FROM_PTR(&init_obj));

    // This must be last, it restores the globals dict
    MP_DYNRUNTIME_INIT_EXIT
}
