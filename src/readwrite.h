// -------------------------------------------------- //
// This file is autogenerated by pioasm; do not edit! //
// -------------------------------------------------- //

#pragma once

#if !PICO_NO_HARDWARE
#include "hardware/pio.h"
#endif

// ------ //
// readFM //
// ------ //

#define readFM_wrap_target 0
#define readFM_wrap 3

static const uint16_t readFM_program_instructions[] = {
            //     .wrap_target
    0xe001, //  0: set    pins, 1                    
    0xe200, //  1: set    pins, 0                [2] 
    0x6001, //  2: out    pins, 1                    
    0xe200, //  3: set    pins, 0                [2] 
            //     .wrap
};

#if !PICO_NO_HARDWARE
static const struct pio_program readFM_program = {
    .instructions = readFM_program_instructions,
    .length = 4,
    .origin = -1,
};

static inline pio_sm_config readFM_program_get_default_config(uint offset) {
    pio_sm_config c = pio_get_default_sm_config();
    sm_config_set_wrap(&c, offset + readFM_wrap_target, offset + readFM_wrap);
    return c;
}
#endif

// ----- //
// write //
// ----- //

#define write_wrap_target 0
#define write_wrap 3

static const uint16_t write_program_instructions[] = {
            //     .wrap_target
    0x00c2, //  0: jmp    pin, 2                     
    0x0040, //  1: jmp    x--, 0                     
    0xe33f, //  2: set    x, 31                  [3] 
    0x4101, //  3: in     pins, 1                [1] 
            //     .wrap
};

#if !PICO_NO_HARDWARE
static const struct pio_program write_program = {
    .instructions = write_program_instructions,
    .length = 4,
    .origin = -1,
};

static inline pio_sm_config write_program_get_default_config(uint offset) {
    pio_sm_config c = pio_get_default_sm_config();
    sm_config_set_wrap(&c, offset + write_wrap_target, offset + write_wrap);
    return c;
}
#endif

// ------- //
// readMFM //
// ------- //

#define readMFM_wrap_target 5
#define readMFM_wrap 8

static const uint16_t readMFM_program_instructions[] = {
    0xe301, //  0: set    pins, 1                [3] 
    0xe300, //  1: set    pins, 0                [3] 
    0x6621, //  2: out    x, 1                   [6] 
    0x0020, //  3: jmp    !x, 0                      
    0xa742, //  4: nop                           [7] 
            //     .wrap_target
    0xe301, //  5: set    pins, 1                [3] 
    0xe100, //  6: set    pins, 0                [1] 
    0x6021, //  7: out    x, 1                       
    0x0722, //  8: jmp    !x, 2                  [7] 
            //     .wrap
};

#if !PICO_NO_HARDWARE
static const struct pio_program readMFM_program = {
    .instructions = readMFM_program_instructions,
    .length = 9,
    .origin = -1,
};

static inline pio_sm_config readMFM_program_get_default_config(uint offset) {
    pio_sm_config c = pio_get_default_sm_config();
    sm_config_set_wrap(&c, offset + readMFM_wrap_target, offset + readMFM_wrap);
    return c;
}
#endif

// -------- //
// readMFM2 //
// -------- //

#define readMFM2_wrap_target 1
#define readMFM2_wrap 5

static const uint16_t readMFM2_program_instructions[] = {
    0x0703, //  0: jmp    3                      [7] 
            //     .wrap_target
    0xa309, //  1: mov    pins, !x               [3] 
    0xe300, //  2: set    pins, 0                [3] 
    0xa201, //  3: mov    pins, x                [2] 
    0x6021, //  4: out    x, 1                       
    0x13c0, //  5: jmp    pin, 0          side 0 [3] 
            //     .wrap
};

#if !PICO_NO_HARDWARE
static const struct pio_program readMFM2_program = {
    .instructions = readMFM2_program_instructions,
    .length = 6,
    .origin = -1,
};

static inline pio_sm_config readMFM2_program_get_default_config(uint offset) {
    pio_sm_config c = pio_get_default_sm_config();
    sm_config_set_wrap(&c, offset + readMFM2_wrap_target, offset + readMFM2_wrap);
    sm_config_set_sideset(&c, 2, true, false);
    return c;
}
#endif

// --------- //
// readrawFM //
// --------- //

#define readrawFM_wrap_target 0
#define readrawFM_wrap 1

static const uint16_t readrawFM_program_instructions[] = {
            //     .wrap_target
    0x6001, //  0: out    pins, 1                    
    0xe200, //  1: set    pins, 0                [2] 
            //     .wrap
};

#if !PICO_NO_HARDWARE
static const struct pio_program readrawFM_program = {
    .instructions = readrawFM_program_instructions,
    .length = 2,
    .origin = -1,
};

static inline pio_sm_config readrawFM_program_get_default_config(uint offset) {
    pio_sm_config c = pio_get_default_sm_config();
    sm_config_set_wrap(&c, offset + readrawFM_wrap_target, offset + readrawFM_wrap);
    return c;
}
#endif

// ---------- //
// readrawMFM //
// ---------- //

#define readrawMFM_wrap_target 0
#define readrawMFM_wrap 1

static const uint16_t readrawMFM_program_instructions[] = {
            //     .wrap_target
    0x6001, //  0: out    pins, 1                    
    0xe000, //  1: set    pins, 0                    
            //     .wrap
};

#if !PICO_NO_HARDWARE
static const struct pio_program readrawMFM_program = {
    .instructions = readrawMFM_program_instructions,
    .length = 2,
    .origin = -1,
};

static inline pio_sm_config readrawMFM_program_get_default_config(uint offset) {
    pio_sm_config c = pio_get_default_sm_config();
    sm_config_set_wrap(&c, offset + readrawMFM_wrap_target, offset + readrawMFM_wrap);
    return c;
}
#endif

// ------------- //
// rawreadFM_irq //
// ------------- //

#define rawreadFM_irq_wrap_target 1
#define rawreadFM_irq_wrap 6

static const uint16_t rawreadFM_irq_program_instructions[] = {
    0xa225, //  0: mov    x, status              [2] 
            //     .wrap_target
    0x80c0, //  1: pull   ifempty noblock            
    0x6101, //  2: out    pins, 1                [1] 
    0x11e0, //  3: jmp    !osre, 0        side 0 [1] 
    0x0221, //  4: jmp    !x, 1                  [2] 
    0xc001, //  5: irq    nowait 1                   
    0x20c0, //  6: wait   1 irq, 0                   
            //     .wrap
};

#if !PICO_NO_HARDWARE
static const struct pio_program rawreadFM_irq_program = {
    .instructions = rawreadFM_irq_program_instructions,
    .length = 7,
    .origin = -1,
};

static inline pio_sm_config rawreadFM_irq_program_get_default_config(uint offset) {
    pio_sm_config c = pio_get_default_sm_config();
    sm_config_set_wrap(&c, offset + rawreadFM_irq_wrap_target, offset + rawreadFM_irq_wrap);
    sm_config_set_sideset(&c, 2, true, false);
    return c;
}
#endif

// ---------- //
// readFM_irq //
// ---------- //

#define readFM_irq_wrap_target 2
#define readFM_irq_wrap 8

static const uint16_t readFM_irq_program_instructions[] = {
    0xa125, //  0: mov    x, status              [1] 
    0xa242, //  1: nop                           [2] 
            //     .wrap_target
    0x99c0, //  2: pull   ifempty noblock side 1 [1] 
    0xe500, //  3: set    pins, 0                [5] 
    0x6101, //  4: out    pins, 1                [1] 
    0x10e0, //  5: jmp    !osre, 0        side 0     
    0x0121, //  6: jmp    !x, 1                  [1] 
    0xc000, //  7: irq    nowait 0                   
    0x20c1, //  8: wait   1 irq, 1                   
            //     .wrap
};

#if !PICO_NO_HARDWARE
static const struct pio_program readFM_irq_program = {
    .instructions = readFM_irq_program_instructions,
    .length = 9,
    .origin = -1,
};

static inline pio_sm_config readFM_irq_program_get_default_config(uint offset) {
    pio_sm_config c = pio_get_default_sm_config();
    sm_config_set_wrap(&c, offset + readFM_irq_wrap_target, offset + readFM_irq_wrap);
    sm_config_set_sideset(&c, 2, true, false);
    return c;
}
#endif

// -------------- //
// rawreadMFM_irq //
// -------------- //

#define rawreadMFM_irq_wrap_target 1
#define rawreadMFM_irq_wrap 6

static const uint16_t rawreadMFM_irq_program_instructions[] = {
    0xa125, //  0: mov    x, status              [1] 
            //     .wrap_target
    0x93c0, //  1: pull   ifempty noblock side 0 [3] 
    0x6001, //  2: out    pins, 1                    
    0x00e0, //  3: jmp    !osre, 0                   
    0x0021, //  4: jmp    !x, 1                      
    0xc001, //  5: irq    nowait 1                   
    0x30c0, //  6: wait   1 irq, 0        side 0     
            //     .wrap
};

#if !PICO_NO_HARDWARE
static const struct pio_program rawreadMFM_irq_program = {
    .instructions = rawreadMFM_irq_program_instructions,
    .length = 7,
    .origin = -1,
};

static inline pio_sm_config rawreadMFM_irq_program_get_default_config(uint offset) {
    pio_sm_config c = pio_get_default_sm_config();
    sm_config_set_wrap(&c, offset + rawreadMFM_irq_wrap_target, offset + rawreadMFM_irq_wrap);
    sm_config_set_sideset(&c, 2, true, false);
    return c;
}
#endif

// ------------ //
// readMFM2_irq //
// ------------ //

#define readMFM2_irq_wrap_target 2
#define readMFM2_irq_wrap 17

static const uint16_t readMFM2_irq_program_instructions[] = {
    0xa045, //  0: mov    y, status                  
    0x0404, //  1: jmp    4                      [4] 
            //     .wrap_target
    0xa309, //  2: mov    pins, !x               [3] 
    0xb245, //  3: mov    y, status       side 0 [2] 
    0x00ee, //  4: jmp    !osre, 14                  
    0xa001, //  5: mov    pins, x                    
    0x006f, //  6: jmp    !y, 15                     
    0xc100, //  7: irq    nowait 0               [1] 
    0x30c1, //  8: wait   1 irq, 1        side 0     
    0x80c0, //  9: pull   ifempty noblock            
    0x6021, // 10: out    x, 1                       
    0x00c0, // 11: jmp    pin, 0                     
    0xa309, // 12: mov    pins, !x               [3] 
    0x1204, // 13: jmp    4               side 0 [2] 
    0xa101, // 14: mov    pins, x                [1] 
    0x80c0, // 15: pull   ifempty noblock            
    0x6021, // 16: out    x, 1                       
    0x13c0, // 17: jmp    pin, 0          side 0 [3] 
            //     .wrap
};

#if !PICO_NO_HARDWARE
static const struct pio_program readMFM2_irq_program = {
    .instructions = readMFM2_irq_program_instructions,
    .length = 18,
    .origin = -1,
};

static inline pio_sm_config readMFM2_irq_program_get_default_config(uint offset) {
    pio_sm_config c = pio_get_default_sm_config();
    sm_config_set_wrap(&c, offset + readMFM2_irq_wrap_target, offset + readMFM2_irq_wrap);
    sm_config_set_sideset(&c, 2, true, false);
    return c;
}
#endif
