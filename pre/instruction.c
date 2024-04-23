#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "instruction.h"
#include "emulator.h"
#include "emulator_function.h"
#include "io.h"

#include "modrm.h"

#include "debug.h"

/* x86命令の配列、opecode番目の関数がx86の
   opcodeに対応した命令となっている */
instruction_func_t* instructions[256];

static void mov_r8_imm8(Emulator* emu)
{
    uint8_t reg = get_code8(emu, 0) - 0xB0;
    set_register8(emu, reg, get_code8(emu, 1));
    emu->eip += 2;
}

static void mov_r32_imm32(Emulator* emu)
{
    uint8_t reg = get_code8(emu, 0) - 0xB8;
    set_register32(emu, reg, get_code32(emu, 1));
    emu->eip += 5;
}

static void mov_r8_rm8(Emulator* emu)
{
    emu->eip += 1;
    ModRM modrm;
    parse_modrm(emu, &modrm);
    uint32_t rm8 = get_rm8(emu, &modrm);
    set_r8(emu, &modrm, rm8);
}

static void mov_r32_rm32(Emulator* emu)
{
    emu->eip += 1;
    ModRM modrm;
    parse_modrm(emu, &modrm);
    uint32_t rm32 = get_rm32(emu, &modrm);
    set_r32(emu, &modrm, rm32);
}

static void add_rm32_r32(Emulator* emu)
{
    emu->eip += 1;
    ModRM modrm;
    dprintf("eip 0x%08x\n", emu->eip);
    parse_modrm(emu, &modrm);
    dprintf("eip 0x%08x\n", emu->eip);
    uint32_t r32 = get_r32(emu, &modrm);
    uint32_t rm32 = get_rm32(emu, &modrm);
    uint64_t result = (uint64_t)rm32 + (uint64_t)r32;
    dprintf("mod %d, reg %d, rm %d, r32 %d, rm32 %d\n",
            modrm.mod, modrm.reg_index, modrm.rm, r32, rm32);
    set_rm32(emu, &modrm, result);
    update_eflags_add(emu, rm32, r32, result);
}

static void mov_rm8_r8(Emulator* emu)
{
    emu->eip += 1;
    ModRM modrm;
    parse_modrm(emu, &modrm);
    uint32_t r8 = get_r8(emu, &modrm);
    set_rm8(emu, &modrm, r8);
}

static void mov_rm32_r32(Emulator* emu)
{
    emu->eip += 1;
    ModRM modrm;
    parse_modrm(emu, &modrm);
    uint32_t r32 = get_r32(emu, &modrm);
    set_rm32(emu, &modrm, r32);
}

static void inc_r32(Emulator* emu)
{
    uint8_t reg = get_code8(emu, 0) - 0x40;
    set_register32(emu, reg, get_register32(emu, reg) + 1);
    emu->eip += 1;
}

static void push_r32(Emulator* emu)
{
    uint8_t reg = get_code8(emu, 0) - 0x50;
    push32(emu, get_register32(emu, reg));
    emu->eip += 1;
}

static void pop_r32(Emulator* emu)
{
    uint8_t reg = get_code8(emu, 0) - 0x58;
    set_register32(emu, reg, pop32(emu));
    emu->eip += 1;
}

static void push_imm32(Emulator* emu)
{
    uint32_t value = get_code32(emu, 1);
    push32(emu, value);
    emu->eip += 5;
}

static void push_imm8(Emulator* emu)
{
    uint8_t value = get_code8(emu, 1);
    push32(emu, value);
    emu->eip += 2;
}

static void add_rm32_imm8(Emulator* emu, ModRM* modrm)
{
    uint32_t rm32 = get_rm32(emu, modrm);
    uint32_t imm8 = (int32_t)get_sign_code8(emu, 0);
    emu->eip += 1;
    set_rm32(emu, modrm, rm32 + imm8);
}

static void sub_rm32_imm8_(Emulator* emu, ModRM* modrm, int set)
{
    uint32_t rm32 = get_rm32(emu, modrm);
    uint32_t imm8 = (int32_t)get_sign_code8(emu, 0);
    emu->eip += 1;
    uint64_t result = (uint64_t)rm32 - (uint64_t)imm8;
    if (set) {
        set_rm32(emu, modrm, result);
    }
    update_eflags_sub(emu, rm32, imm8, result);
}

static void sub_rm32_imm8(Emulator* emu, ModRM* modrm)
{
    sub_rm32_imm8_(emu, modrm, TRUE);
}

static void cmp_rm32_imm8(Emulator* emu, ModRM* modrm)
{
    sub_rm32_imm8_(emu, modrm, FALSE);
}

static void code_83(Emulator* emu)
{
    emu->eip += 1;
    ModRM modrm;
    parse_modrm(emu, &modrm);

    switch (modrm.opecode) {
    case 0:
        add_rm32_imm8(emu, &modrm);
        break;
    case 5:
        sub_rm32_imm8(emu, &modrm);
        break;
    case 7:
        cmp_rm32_imm8(emu, &modrm);
        break;

    default:
        printf("83: modrm opecode == %x is not implemented\n", modrm.opecode);
        exit(0);
    }
}

static void mov_rm32_imm32(Emulator* emu)
{
    emu->eip += 1;
    ModRM modrm;
    parse_modrm(emu, &modrm);
    uint32_t value = get_code32(emu, 0);
    emu->eip += 4;
    set_rm32(emu, &modrm, value);
}

static void in_al_dx(Emulator* emu)
{
    uint16_t address = get_register32(emu, EDX) & 0xffff;
    uint8_t value = io_in8(address);
    set_register8(emu, AL, value);
    emu->eip += 1;
}

static void out_dx_al(Emulator* emu)
{
    uint16_t address = get_register32(emu, EDX) & 0xffff;
    uint8_t value = get_register8(emu, AL);
    io_out8(address, value);
    emu->eip += 1;
}

static void idiv_rm32(Emulator* emu, ModRM* modrm)
{
    uint32_t div = get_rm32(emu, modrm);
    uint32_t eax = get_register32(emu, EAX);
    uint32_t edx = get_register32(emu, EDX);

    uint64_t divsrc, quot, rem;

    if (div == 0) {
        printf("Divide Error: Divide 0!");
        exit(1);
    }

    divsrc = ((uint64_t)edx << 32) | eax;
    quot = divsrc / div;
    rem = divsrc % div;

    if (quot > 0xFFFFFFFF) {
        printf("Divide Error: quot > 0xFFFFFFFF");
        exit(1);
    }

    set_register32(emu, EAX, (uint32_t)quot);
    set_register32(emu, EDX, (uint32_t)rem);
}

static void code_f7(Emulator* emu)
{
    emu->eip += 1;
    ModRM modrm;
    parse_modrm(emu, &modrm);

    switch (modrm.opecode) {
    case 7:
        idiv_rm32(emu, &modrm);
        break;
    default:
        printf("not implemented: F7 /%d\n", modrm.opecode);
        exit(1);
    }
}

static void inc_rm32(Emulator* emu, ModRM* modrm)
{
    uint32_t value = get_rm32(emu, modrm);
    set_rm32(emu, modrm, value + 1);
}

static void code_ff(Emulator* emu)
{
    emu->eip += 1;
    ModRM modrm;
    parse_modrm(emu, &modrm);

    switch (modrm.opecode) {
    case 0:
        inc_rm32(emu, &modrm);
        break;
    default:
        printf("not implemented: FF /%d\n", modrm.opecode);
        exit(1);
    }
}

static void call_rel32(Emulator* emu)
{
    int32_t diff = get_sign_code32(emu, 1);
    push32(emu, emu->eip + 5);
    emu->eip += (diff + 5);
}

static void ret(Emulator* emu)
{
    emu->eip = pop32(emu);
}

static void leave(Emulator* emu)
{
    uint32_t ebp = get_register32(emu, EBP);
    set_register32(emu, ESP, ebp);
    set_register32(emu, EBP, pop32(emu));

    emu->eip += 1;
}

static void short_jump(Emulator* emu)
{
    int8_t diff = get_sign_code8(emu, 1);
    emu->eip += (diff + 2);
}

static void near_jump(Emulator* emu)
{
    int32_t diff = get_sign_code32(emu, 1);
    emu->eip += (diff + 5);
}

static void cmp_al_imm8(Emulator* emu)
{
    uint8_t value = get_code8(emu, 1);
    uint8_t al = get_register8(emu, AL);
    uint64_t result = (uint64_t)al - (uint64_t)value;
    update_eflags_sub(emu, al, value, result);
    emu->eip += 2;
}

static void cmp_eax_imm32(Emulator* emu)
{
    uint32_t value = get_code32(emu, 1);
    uint32_t eax = get_register32(emu, EAX);
    uint64_t result = (uint64_t)eax - (uint64_t)value;
    update_eflags_sub(emu, eax, value, result);
    emu->eip += 5;
}

static void cmp_r32_rm32(Emulator* emu)
{
    emu->eip += 1;
    ModRM modrm;
    parse_modrm(emu, &modrm);
    uint32_t r32 = get_r32(emu, &modrm);
    uint32_t rm32 = get_rm32(emu, &modrm);
    uint64_t result = (uint64_t)r32 - (uint64_t)rm32;
    update_eflags_sub(emu, r32, rm32, result);
}

static void lea(Emulator* emu)
{
    emu->eip += 1;
    ModRM modrm;
    parse_modrm(emu, &modrm);
    uint32_t address = calc_memory_address(emu, &modrm);
    set_r32(emu, &modrm, address);
}

#define DEFINE_JX(flag, is_flag) \
static void j ## flag(Emulator* emu) \
{ \
    int diff = is_flag(emu) ? get_sign_code8(emu, 1) : 0; \
    emu->eip += (diff + 2); \
} \
static void jn ## flag(Emulator* emu) \
{ \
    int diff = is_flag(emu) ? 0 : get_sign_code8(emu, 1); \
    emu->eip += (diff + 2); \
}

DEFINE_JX(c, is_carry)
DEFINE_JX(z, is_zero)
DEFINE_JX(s, is_sign)
DEFINE_JX(o, is_overflow)

#undef DEFINE_JX

static void jl(Emulator* emu)
{
    int diff = (is_sign(emu) != is_overflow(emu)) ? get_sign_code8(emu, 1) : 0;
    emu->eip += (diff + 2);
}

static void jle(Emulator* emu)
{
    int diff = (is_zero(emu) || (is_sign(emu) != is_overflow(emu))) ? get_sign_code8(emu, 1) : 0;
    emu->eip += (diff + 2);
}

static void mov_eax_moffs(Emulator* emu)
{
    uint32_t address = get_code32(emu, 1);
    uint32_t value = get_memory32(emu, address);
    set_register32(emu, EAX, value);

    emu->eip += 5;
}

static void mov_moffs_eax(Emulator* emu)
{
    uint32_t value = get_register32(emu, EAX);
    uint32_t address = get_code32(emu, 1);
    set_memory32(emu, address, value);

    emu->eip += 5;
}

static void cwd(Emulator* emu)
{
    uint32_t eax = get_register32(emu, EAX);
    set_register32(emu, EDX, (eax >> 31) ? 0xffffffff : 0x00000000);

    emu->eip += 1;
}

static void swi(Emulator* emu)
{
    emu->int_index = get_code8(emu, 1);
    emu->eip += 2;
}

static void iretd(Emulator* emu)
{
    emu->eip = pop32(emu);
    emu->eflags = pop32(emu);
}

void init_instructions(void)
{
    int32_t i;

    memset(instructions, 0, sizeof(instructions));

    instructions[0x01] = add_rm32_r32;

    instructions[0x3B] = cmp_r32_rm32;
    instructions[0x3C] = cmp_al_imm8;
    instructions[0x3D] = cmp_eax_imm32;

    for (i = 0; i < 8; i++) {
        instructions[0x40 + i] = inc_r32;
    }

    for (i = 0; i < 8; i++) {
        instructions[0x50 + i] = push_r32;
    }

    for (i = 0; i < 8; i++) {
        instructions[0x58 + i] = pop_r32;
    }

    instructions[0x68] = push_imm32;
    instructions[0x6A] = push_imm8;

    instructions[0x70] = jo;
    instructions[0x71] = jno;
    instructions[0x72] = jc;
    instructions[0x73] = jnc;
    instructions[0x74] = jz;
    instructions[0x75] = jnz;
    instructions[0x78] = js;
    instructions[0x79] = jns;
    instructions[0x7C] = jl;
    instructions[0x7E] = jle;

    instructions[0x83] = code_83;
    instructions[0x88] = mov_rm8_r8;
    instructions[0x89] = mov_rm32_r32;
    instructions[0x8A] = mov_r8_rm8;
    instructions[0x8B] = mov_r32_rm32;
    instructions[0x8D] = lea;

    instructions[0x99] = cwd;

    instructions[0xA1] = mov_eax_moffs;
    instructions[0xA3] = mov_moffs_eax;

    for (i = 0; i < 8; i++) {
        instructions[0xB0 + i] = mov_r8_imm8;
    }

    for (i = 0; i < 8; i++) {
        instructions[0xB8 + i] = mov_r32_imm32;
    }

    instructions[0xC3] = ret;
    instructions[0xC7] = mov_rm32_imm32;
    instructions[0xC9] = leave;
    instructions[0xCD] = swi;

    instructions[0xE8] = call_rel32;
    instructions[0xE9] = near_jump;
    instructions[0xEB] = short_jump;

    instructions[0xEC] = in_al_dx;
    instructions[0xEE] = out_dx_al;

    instructions[0xF7] = code_f7;
    instructions[0xFF] = code_ff;
}
