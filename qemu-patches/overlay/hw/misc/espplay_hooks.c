/*
 * ESP32 Playground - firmware function hook & call-in framework
 *
 * Intercepts firmware functions via CPU breakpoints at ELF symbol
 * addresses, and calls into firmware functions with synthetic ABI frames
 * (Xtensa windowed call8 / RISC-V a0-a5).
 *
 * Copyright (c) 2026 ESP32 Playground contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/error-report.h"
#include "hw/misc/espplay_internal.h"
#include "exec/breakpoint.h"
#include "sysemu/runstate.h"

#if defined(CONFIG_TARGET_XTENSA)
#include "target/xtensa/cpu.h"
#elif defined(CONFIG_TARGET_RISCV32)
#include "target/riscv/cpu.h"
#include "target/riscv/cpu_bits.h"
#endif

/* ------------------------------------------------------------------ ELF */

typedef struct {
    char name[64];
    uint32_t value;
} EspplaySym;

static EspplaySym *espplay_syms;
static int espplay_sym_count;

static uint16_t elf16(const uint8_t *p, bool le)
{
    return le ? p[0] | (p[1] << 8) : p[1] | (p[0] << 8);
}
static uint32_t elf32w(const uint8_t *p, bool le)
{
    return le ? p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24)
              : p[3] | (p[2] << 8) | (p[1] << 16) | (p[0] << 24);
}

static void espplay_elf_load(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        error_report("espplay: cannot open elf %s", path);
        return;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *buf = g_malloc(sz);
    if (fread(buf, 1, sz, f) != (size_t)sz) {
        fclose(f);
        g_free(buf);
        return;
    }
    fclose(f);
    if (sz < 52 || buf[0] != 0x7F || buf[1] != 'E' || buf[2] != 'L' || buf[3] != 'F') {
        g_free(buf);
        return;
    }
    bool le = buf[5] != 2;
    uint32_t shoff = elf32w(buf + 32, le);
    uint16_t shentsize = elf16(buf + 46, le);
    uint16_t shnum = elf16(buf + 48, le);
    if (shoff == 0 || shnum == 0 || shoff + (long)shnum * shentsize > sz) {
        g_free(buf);
        return;
    }
    for (int i = 0; i < shnum; i++) {
        const uint8_t *sh = buf + shoff + (long)i * shentsize;
        if (elf32w(sh + 4, le) != 2 /* SHT_SYMTAB */) {
            continue;
        }
        uint32_t off = elf32w(sh + 16, le);
        uint32_t size = elf32w(sh + 20, le);
        uint32_t link = elf32w(sh + 24, le);
        uint32_t entsize = elf32w(sh + 36, le) ?: 16;
        const uint8_t *strsh = buf + shoff + (long)link * shentsize;
        uint32_t stroff = elf32w(strsh + 16, le);
        uint32_t strsz = elf32w(strsh + 20, le);
        int n = size / entsize;
        espplay_syms = g_malloc(sizeof(EspplaySym) * (n > 0 ? n : 1));
        espplay_sym_count = 0;
        for (int k = 0; k < n; k++) {
            const uint8_t *sym = buf + off + (long)k * entsize;
            uint32_t nameoff = elf32w(sym, le);
            uint32_t value = elf32w(sym + 4, le);
            if (nameoff >= strsz || (sym[12] >> 4) == 0 /* STB_LOCAL */) {
                continue;
            }
            const char *name = (const char *)(buf + stroff + nameoff);
            if (!name[0] || strlen(name) >= sizeof(espplay_syms[0].name)) {
                continue;
            }
            g_strlcpy(espplay_syms[espplay_sym_count].name, name,
                      sizeof(espplay_syms[0].name));
            espplay_syms[espplay_sym_count].value = value;
            espplay_sym_count++;
        }
        break;
    }
    g_free(buf);
    espplay_log("elf symbols loaded: %d", espplay_sym_count);
}

bool espplay_sym_lookup(const char *name, uint32_t *addr)
{
    for (int i = 0; i < espplay_sym_count; i++) {
        if (strcmp(espplay_syms[i].name, name) == 0) {
            *addr = espplay_syms[i].value;
            return true;
        }
    }
    return false;
}

/* ------------------------------------------------- target register API */

uint32_t espplay_get_pc(CPUState *cpu)
{
#if defined(CONFIG_TARGET_XTENSA) || defined(CONFIG_TARGET_RISCV32)
    return ((CPUArchState *)cpu->env_ptr)->pc;
#else
    return 0;
#endif
}

void espplay_set_pc(CPUState *cpu, uint32_t pc)
{
#if defined(CONFIG_TARGET_XTENSA) || defined(CONFIG_TARGET_RISCV32)
    ((CPUArchState *)cpu->env_ptr)->pc = pc;
#endif
}

uint32_t espplay_get_arg(CPUState *cpu, int idx)
{
#if defined(CONFIG_TARGET_XTENSA)
    return ((CPUXtensaState *)cpu->env_ptr)->regs[10 + idx];
#elif defined(CONFIG_TARGET_RISCV32)
    return ((CPURISCVState *)cpu->env_ptr)->gpr[10 + idx];
#else
    return 0;
#endif
}

uint32_t espplay_get_ra(CPUState *cpu)
{
#if defined(CONFIG_TARGET_XTENSA)
    uint32_t a0 = ((CPUXtensaState *)cpu->env_ptr)->regs[0];
    return a0 & 0x3FFFFFFFu;
#elif defined(CONFIG_TARGET_RISCV32)
    return ((CPURISCVState *)cpu->env_ptr)->gpr[1];
#else
    return 0;
#endif
}

void espplay_set_ret(CPUState *cpu, uint32_t v0, uint32_t v1)
{
#if defined(CONFIG_TARGET_XTENSA)
    CPUXtensaState *env = cpu->env_ptr;
    env->regs[10] = v0;
    env->regs[11] = v1;
#elif defined(CONFIG_TARGET_RISCV32)
    CPURISCVState *env = cpu->env_ptr;
    env->gpr[10] = v0;
    env->gpr[11] = v1;
#endif
}

/* --------------------------------------------------------------- qcall */

static CPUState *espplay_qcall_cpu;
static uint32_t espplay_cave_addr;

static struct {
    bool active;
    bool pending;           /* setup scheduled, frame not yet committed */
    bool queued;            /* one follow-up request staged while busy */
    uint32_t q_func;
    uint32_t q_args[6];
    int q_nargs;
    void (*q_done)(uint32_t, uint32_t, void *);
    void *q_ctx;
    uint32_t func;
    uint32_t args[6];
    int nargs;
    void (*done)(uint32_t, uint32_t, void *);
    void *ctx;
#if defined(CONFIG_TARGET_XTENSA)
    uint32_t pc;
    uint32_t regs[16];
    uint32_t phys_regs[256];
    uint32_t window_base;
    uint32_t window_start;
    uint32_t ps;
    uint32_t windowbase_next;
#elif defined(CONFIG_TARGET_RISCV32)
    uint32_t pc;
    uint32_t gpr[32];
    uint64_t mstatus;
#endif
} qcall;

bool espplay_qcall_active(void)
{
    return qcall.active;
}

static void espplay_setup_frame(CPUState *cpu)
{
#if defined(CONFIG_TARGET_XTENSA)
    CPUXtensaState *env = cpu->env_ptr;
    if (!qcall.active) {
        qcall.active = true;
        qcall.pc = env->pc;
        memcpy(qcall.regs, env->regs, sizeof(qcall.regs));
        memcpy(qcall.phys_regs, env->phys_regs, sizeof(qcall.phys_regs));
        qcall.window_base = env->sregs[WINDOW_BASE];
        qcall.window_start = env->sregs[WINDOW_START];
        qcall.ps = env->sregs[PS];
        qcall.windowbase_next = env->windowbase_next;
    }
    /* apply any pending lazy window rotation first */
    xtensa_sync_phys_from_window(env);
    env->sregs[WINDOW_BASE] = env->windowbase_next &
        (env->config->nareg / 4 - 1);
    xtensa_sync_window_from_phys(env);

    /* call8 frame: RA -> a8 (= callee a0 after ENTRY rotation),
     * args -> a10..a15 (= callee a2..a7) */
    env->regs[8] = (2u << 30) | (espplay_cave_addr & 0x3FFFFFFFu);
    for (int i = 0; i < 6; i++) {
        env->regs[10 + i] = i < qcall.nargs ? qcall.args[i] : 0;
    }
    /* PS_CALLINC lives in bits 16-17 (0x30000); the OWB field is 8-11. */
    env->sregs[PS] = (env->sregs[PS] & ~0x30000u) | 0x20000u; /* CALLINC=2 */
    env->sregs[PS] = (env->sregs[PS] & ~0xFu) | 15u;          /* INTLEVEL */
    /* Single-base-bit windowstart for the committed base: guarantees the
     * callee ENTRY rotation lands on a legal window. */
    env->sregs[WINDOW_START] =
        1u << (env->sregs[WINDOW_BASE] & (env->config->nareg / 4 - 1));
    env->windowbase_next = env->sregs[WINDOW_BASE];
    env->pc = qcall.func;
#elif defined(CONFIG_TARGET_RISCV32)
    CPURISCVState *env = cpu->env_ptr;
    if (!qcall.active) {
        qcall.active = true;
        qcall.pc = env->pc;
        memcpy(qcall.gpr, env->gpr, sizeof(qcall.gpr));
        qcall.mstatus = env->mstatus;
    }
    for (int i = 0; i < 6; i++) {
        env->gpr[10 + i] = i < qcall.nargs ? qcall.args[i] : 0;
    }
    env->gpr[1] = espplay_cave_addr; /* ra */
    env->mstatus &= ~MSTATUS_MIE;    /* no interrupts inside injected call */
    env->pc = qcall.func;
#endif
}

static void espplay_finish_qcall(CPUState *cpu)
{
    uint32_t r0 = 0, r1 = 0;
#if defined(CONFIG_TARGET_XTENSA)
    CPUXtensaState *env = cpu->env_ptr;
    r0 = env->regs[10];
    r1 = env->regs[11];
    env->pc = qcall.pc;
    memcpy(env->regs, qcall.regs, sizeof(qcall.regs));
    memcpy(env->phys_regs, qcall.phys_regs, sizeof(qcall.phys_regs));
    env->sregs[WINDOW_BASE] = qcall.window_base;
    env->sregs[WINDOW_START] = qcall.window_start;
    env->sregs[PS] = qcall.ps;
    env->windowbase_next = qcall.windowbase_next;
#elif defined(CONFIG_TARGET_RISCV32)
    CPURISCVState *env = cpu->env_ptr;
    r0 = env->gpr[10];
    r1 = env->gpr[11];
    env->pc = qcall.pc;
    memcpy(env->gpr, qcall.gpr, sizeof(qcall.gpr));
    env->mstatus = qcall.mstatus;
#endif
    void (*done)(uint32_t, uint32_t, void *) = qcall.done;
    void *ctx = qcall.ctx;
    qcall.active = false;
    qcall.done = NULL;
    if (done) {
        done(r0, r1, ctx);
    }
    /* Promote a follow-up request staged while we were busy (re-requests
     * from done() above start fresh; they never collide with this one). */
    if (qcall.queued) {
        qcall.queued = false;
        qcall.func = qcall.q_func;
        qcall.nargs = qcall.q_nargs;
        memcpy(qcall.args, qcall.q_args, sizeof(qcall.args));
        qcall.done = qcall.q_done;
        qcall.ctx = qcall.q_ctx;
        qcall.pending = true;
        async_run_on_cpu(espplay_qcall_cpu, espplay_qcall_setup_on_cpu,
                         RUN_ON_CPU_NULL);
    }
    /* Anything queued during the call may be waiting on qcall_active. */
    espplay_wifi_ev_pump();
}

void espplay_qcall_setup_on_cpu(CPUState *cpu, run_on_cpu_data data)
{
    /* Exactly one setup per request: a stale async_run_on_cpu delivery (or
     * a second request racing in) must not re-commit the frame while the
     * injected call is in flight. */
    if (!qcall.pending || qcall.active) {
        cpu_loop_exit(cpu);
        return;
    }
    qcall.pending = false;
    espplay_setup_frame(cpu);
    cpu_loop_exit(cpu);
}

void espplay_qcall_request(uint32_t func, const uint32_t *args, int nargs,
                           void (*done)(uint32_t, uint32_t, void *), void *ctx)
{
    if (!espplay_qcall_cpu || !espplay_cave_addr || !func) {
        if (done) {
            done(0, 0, ctx);
        }
        return;
    }
    if (qcall.active || qcall.pending) {
        if (qcall.queued) {
            espplay_log("qcall busy, dropping request to 0x%x", func);
            if (done) {
                done(0, 0, ctx);
            }
            return;
        }
        qcall.queued = true;
        qcall.q_func = func;
        qcall.q_nargs = nargs > 6 ? 6 : nargs;
        for (int i = 0; i < qcall.q_nargs; i++) {
            qcall.q_args[i] = args[i];
        }
        qcall.q_done = done;
        qcall.q_ctx = ctx;
        return;
    }
    qcall.func = func;
    qcall.nargs = nargs > 6 ? 6 : nargs;
    for (int i = 0; i < qcall.nargs; i++) {
        qcall.args[i] = args[i];
    }
    qcall.done = done;
    qcall.ctx = ctx;
    qcall.pending = true;
    async_run_on_cpu(espplay_qcall_cpu, espplay_qcall_setup_on_cpu,
                     RUN_ON_CPU_NULL);
}

/* --------------------------------------------------- debug bp dispatch */

bool espplay_debug_check(CPUState *cpu)
{
    if (cpu != espplay_qcall_cpu) {
        return false;
    }
    uint32_t pc = espplay_get_pc(cpu);

    if (qcall.active && pc == espplay_cave_addr) {
        espplay_finish_qcall(cpu);
        cpu_loop_exit(cpu);
        return true;
    }
    for (int i = 0; i < NUM_ESPPLAY_HOOKS; i++) {
        if (espplay_hooks[i].armed && espplay_hooks[i].addr == pc) {
            bool handled = espplay_hooks[i].fn(cpu, pc, espplay_hooks[i].symbol);
            if (handled) {
                uint32_t ra = espplay_get_ra(cpu);
                espplay_set_pc(cpu, ra);
                cpu_loop_exit(cpu);
                return true;
            }
            return false;
        }
    }
    return false;
}

/* ------------------------------------------------------- initialization */

void espplay_hooks_init(EspplayBridgeState *s)
{
    espplay_qcall_cpu = qemu_get_cpu(0);
    espplay_cave_addr = s->cave_addr;
    if (!s->elf_path) {
        espplay_log("no elf: firmware hooks disabled");
        return;
    }
    espplay_elf_load(s->elf_path);
    if (!espplay_qcall_cpu) {
        return;
    }
    for (int i = 0; i < NUM_ESPPLAY_HOOKS; i++) {
        uint32_t addr;
        if (espplay_sym_lookup(espplay_hooks[i].symbol, &addr) &&
            !(addr & 1)) {
            espplay_hooks[i].addr = addr;
            CPUState *cpu;
            CPU_FOREACH(cpu) {
                CPUBreakpoint *bp = NULL;
                cpu_breakpoint_insert(cpu, addr, BP_GDB, &bp);
            }
            espplay_hooks[i].armed = true;
        }
    }
    CPUBreakpoint *bp = NULL;
    cpu_breakpoint_insert(espplay_qcall_cpu, espplay_cave_addr, BP_GDB, &bp);
    espplay_log("hooks armed, cave 0x%x", espplay_cave_addr);
}
