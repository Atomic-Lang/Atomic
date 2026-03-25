// codegen_x86.hpp
// Helpers de encoding x86-64 para o codegen Atomic — emite instrucoes diretamente numa Section COFF

#ifndef ATOMIC_CODEGEN_X86_HPP
#define ATOMIC_CODEGEN_X86_HPP

#include "../platform_defs/coff_emitter.hpp"
#include <cstdint>

namespace atomic {

// ============================================================================
// Condition codes
// ============================================================================

namespace cc {
    constexpr uint8_t E  = 0x04;  // equal / zero
    constexpr uint8_t NE = 0x05;  // not equal / not zero
    constexpr uint8_t L  = 0x0C;  // less (signed)
    constexpr uint8_t GE = 0x0D;  // greater or equal (signed)
    constexpr uint8_t LE = 0x0E;  // less or equal (signed)
    constexpr uint8_t G  = 0x0F;  // greater (signed)
}

// ============================================================================
// X86 Emitter — opera sobre uma Section&
// ============================================================================

class X86Emitter {
public:
    explicit X86Emitter(Section& text) : m_text(text) {}

    // Referencia a secao de codigo
    Section& text() { return m_text; }
    size_t pos() const { return m_text.pos(); }

    // ========================================================================
    // Encoding primitivos
    // ========================================================================

    // REX prefix
    void rex(bool w, bool r, bool x, bool b) {
        uint8_t val = 0x40;
        if (w) val |= 0x08;
        if (r) val |= 0x04;
        if (x) val |= 0x02;
        if (b) val |= 0x01;
        m_text.emit_u8(val);
    }

    // ModR/M byte
    void modrm(uint8_t mod, uint8_t reg, uint8_t rm) {
        m_text.emit_u8((mod << 6) | ((reg & 7) << 3) | (rm & 7));
    }

    // SIB byte
    void sib(uint8_t scale, uint8_t index, uint8_t base) {
        m_text.emit_u8((scale << 6) | ((index & 7) << 3) | (base & 7));
    }

    // ========================================================================
    // Push / Pop
    // ========================================================================

    void emit_push(uint8_t reg) {
        if (reg >= 8) { m_text.emit_u8(0x41); reg -= 8; }
        m_text.emit_u8(0x50 + reg);
    }

    void emit_pop(uint8_t reg) {
        if (reg >= 8) { m_text.emit_u8(0x41); reg -= 8; }
        m_text.emit_u8(0x58 + reg);
    }

    // ========================================================================
    // Mov
    // ========================================================================

    // mov reg, imm64
    void emit_mov_reg_imm64(uint8_t reg, uint64_t imm) {
        rex(true, false, false, reg >= 8);
        m_text.emit_u8(0xB8 + (reg & 7));
        m_text.emit_u64(imm);
    }

    // mov reg, imm32 (sign-extended to 64-bit)
    void emit_mov_reg_imm32(uint8_t reg, int32_t imm) {
        rex(true, false, false, reg >= 8);
        m_text.emit_u8(0xC7);
        modrm(0b11, 0, reg & 7);
        m_text.emit_i32(imm);
    }

    // mov [rbp + offset], reg
    void emit_mov_rbp_offset_reg(int32_t offset, uint8_t reg) {
        rex(true, reg >= 8, false, false);
        m_text.emit_u8(0x89);
        modrm(0b10, reg & 7, 5); // [rbp + disp32]
        m_text.emit_i32(offset);
    }

    // mov reg, [rbp + offset]
    void emit_mov_reg_rbp_offset(uint8_t reg, int32_t offset) {
        rex(true, reg >= 8, false, false);
        m_text.emit_u8(0x8B);
        modrm(0b10, reg & 7, 5); // [rbp + disp32]
        m_text.emit_i32(offset);
    }

    // mov dst, src (reg to reg)
    void emit_mov_reg_reg(uint8_t dst, uint8_t src) {
        rex(true, src >= 8, false, dst >= 8);
        m_text.emit_u8(0x89);
        modrm(0b11, src & 7, dst & 7);
    }

    // mov [rsp + offset], reg (com SIB)
    void emit_mov_rsp_offset_reg(int32_t offset, uint8_t reg) {
        rex(true, reg >= 8, false, false);
        m_text.emit_u8(0x89);
        modrm(0b10, reg & 7, 4); // RSP precisa de SIB
        sib(0, 4, 4);            // SIB: base=RSP, index=none
        m_text.emit_i32(offset);
    }

    // mov reg, [rsp + offset] (com SIB)
    void emit_mov_reg_rsp_offset(uint8_t reg, int32_t offset) {
        rex(true, reg >= 8, false, false);
        m_text.emit_u8(0x8B);
        modrm(0b10, reg & 7, 4); // RSP precisa de SIB
        sib(0, 4, 4);            // SIB: base=RSP, index=none
        m_text.emit_i32(offset);
    }

    // movzx rax, al
    void emit_movzx_rax_al() {
        rex(true, false, false, false);
        m_text.emit_u8(0x0F);
        m_text.emit_u8(0xB6);
        modrm(0b11, 0, 0);
    }

    // ========================================================================
    // Aritmetica
    // ========================================================================

    // add dst, src
    void emit_add_reg_reg(uint8_t dst, uint8_t src) {
        rex(true, src >= 8, false, dst >= 8);
        m_text.emit_u8(0x01);
        modrm(0b11, src & 7, dst & 7);
    }

    // sub dst, src
    void emit_sub_reg_reg(uint8_t dst, uint8_t src) {
        rex(true, src >= 8, false, dst >= 8);
        m_text.emit_u8(0x29);
        modrm(0b11, src & 7, dst & 7);
    }

    // imul dst, src
    void emit_imul_reg_reg(uint8_t dst, uint8_t src) {
        rex(true, dst >= 8, false, src >= 8);
        m_text.emit_u8(0x0F);
        m_text.emit_u8(0xAF);
        modrm(0b11, dst & 7, src & 7);
    }

    // cqo (sign-extend rax into rdx:rax)
    void emit_cqo() {
        rex(true, false, false, false);
        m_text.emit_u8(0x99);
    }

    // idiv reg
    void emit_idiv_reg(uint8_t reg) {
        rex(true, false, false, reg >= 8);
        m_text.emit_u8(0xF7);
        modrm(0b11, 7, reg & 7);
    }

    // neg reg
    void emit_neg_reg(uint8_t reg) {
        rex(true, false, false, reg >= 8);
        m_text.emit_u8(0xF7);
        modrm(0b11, 3, reg & 7);
    }

    // not reg (bitwise)
    void emit_not_reg(uint8_t reg) {
        rex(true, false, false, reg >= 8);
        m_text.emit_u8(0xF7);
        modrm(0b11, 2, reg & 7);
    }

    // inc reg
    void emit_inc_reg(uint8_t reg) {
        rex(true, false, false, reg >= 8);
        m_text.emit_u8(0xFF);
        modrm(0b11, 0, reg & 7);
    }

    // dec reg
    void emit_dec_reg(uint8_t reg) {
        rex(true, false, false, reg >= 8);
        m_text.emit_u8(0xFF);
        modrm(0b11, 1, reg & 7);
    }

    // sub rsp, imm32
    void emit_sub_rsp_imm32(int32_t imm) {
        rex(true, false, false, false);
        m_text.emit_u8(0x81);
        modrm(0b11, 5, 4); // sub, rsp
        m_text.emit_i32(imm);
    }

    // add rsp, imm32
    void emit_add_rsp_imm32(int32_t imm) {
        rex(true, false, false, false);
        m_text.emit_u8(0x81);
        modrm(0b11, 0, 4); // add, rsp
        m_text.emit_i32(imm);
    }

    // ========================================================================
    // Logica / Bitwise
    // ========================================================================

    // xor dst, src
    void emit_xor_reg_reg(uint8_t dst, uint8_t src) {
        rex(true, src >= 8, false, dst >= 8);
        m_text.emit_u8(0x31);
        modrm(0b11, src & 7, dst & 7);
    }

    // and dst, src
    void emit_and_reg_reg(uint8_t dst, uint8_t src) {
        rex(true, src >= 8, false, dst >= 8);
        m_text.emit_u8(0x21);
        modrm(0b11, src & 7, dst & 7);
    }

    // or dst, src
    void emit_or_reg_reg(uint8_t dst, uint8_t src) {
        rex(true, src >= 8, false, dst >= 8);
        m_text.emit_u8(0x09);
        modrm(0b11, src & 7, dst & 7);
    }

    // xor dst, src (alias para bitwise xor — mesmo opcode que emit_xor_reg_reg)
    void emit_bxor_reg_reg(uint8_t dst, uint8_t src) {
        emit_xor_reg_reg(dst, src);
    }

    // shl rax, cl
    void emit_shl_reg_cl(uint8_t reg) {
        rex(true, false, false, reg >= 8);
        m_text.emit_u8(0xD3);
        modrm(0b11, 4, reg & 7);
    }

    // sar rax, cl (arithmetic shift right)
    void emit_sar_reg_cl(uint8_t reg) {
        rex(true, false, false, reg >= 8);
        m_text.emit_u8(0xD3);
        modrm(0b11, 7, reg & 7);
    }

    // ========================================================================
    // Comparacao
    // ========================================================================

    // cmp a, b
    void emit_cmp_reg_reg(uint8_t a, uint8_t b) {
        rex(true, b >= 8, false, a >= 8);
        m_text.emit_u8(0x39);
        modrm(0b11, b & 7, a & 7);
    }

    // test a, b
    void emit_test_reg_reg(uint8_t a, uint8_t b) {
        rex(true, b >= 8, false, a >= 8);
        m_text.emit_u8(0x85);
        modrm(0b11, b & 7, a & 7);
    }

    // setcc al
    void emit_setcc(uint8_t cond) {
        m_text.emit_u8(0x0F);
        m_text.emit_u8(0x90 + cond);
        modrm(0b11, 0, 0); // al
    }

    // ========================================================================
    // Controle de fluxo
    // ========================================================================

    // call rel32
    void emit_call_rel32(uint32_t rel) {
        m_text.emit_u8(0xE8);
        m_text.emit_u32(rel);
    }

    // ret
    void emit_ret() {
        m_text.emit_u8(0xC3);
    }

    // jmp rel32 (placeholder, retorna offset do rel32 para patch)
    size_t emit_jmp_rel32() {
        m_text.emit_u8(0xE9);
        size_t patch_off = m_text.pos();
        m_text.emit_u32(0); // placeholder
        return patch_off;
    }

    // jcc rel32 (conditional jump, retorna offset do rel32 para patch)
    size_t emit_jcc_rel32(uint8_t cond) {
        m_text.emit_u8(0x0F);
        m_text.emit_u8(0x80 + cond);
        size_t patch_off = m_text.pos();
        m_text.emit_u32(0); // placeholder
        return patch_off;
    }

    // Patch um rel32 para apontar de patch_off para target
    void patch_jump(size_t patch_off, size_t target) {
        int32_t rel = static_cast<int32_t>(target - (patch_off + 4));
        m_text.patch_i32(patch_off, rel);
    }

    // ========================================================================
    // LEA / Enderecos
    // ========================================================================

    // lea reg, [rip + rel32] (placeholder, retorna offset do rel32)
    size_t emit_lea_reg_rip_rel32(uint8_t reg) {
        rex(true, reg >= 8, false, false);
        m_text.emit_u8(0x8D);
        modrm(0b00, reg & 7, 5); // RIP-relative
        size_t patch_off = m_text.pos();
        m_text.emit_u32(0); // placeholder for relocation
        return patch_off;
    }

    // lea reg, [rbp + offset]
    void emit_lea_reg_rbp_offset(uint8_t reg, int32_t offset) {
        rex(true, reg >= 8, false, false);
        m_text.emit_u8(0x8D);
        modrm(0b10, reg & 7, 5); // [rbp + disp32]
        m_text.emit_i32(offset);
    }

    // ========================================================================
    // Prologo / Epilogo de funcao
    // ========================================================================

    // push rbp; mov rbp, rsp; sub rsp, 0 (retorna offset do imm32 para patch)
    size_t emit_prologue() {
        constexpr uint8_t RBP = 5, RSP = 4;
        emit_push(RBP);
        emit_mov_reg_reg(RBP, RSP);
        size_t patch_off = m_text.pos();
        emit_sub_rsp_imm32(0); // sera patcheado depois
        return patch_off;
    }

    // mov rsp, rbp; pop rbp; ret
    void emit_epilogue() {
        constexpr uint8_t RBP = 5, RSP = 4;
        emit_mov_reg_reg(RSP, RBP);
        emit_pop(RBP);
        emit_ret();
    }

    // Patch o tamanho da stack no prologo (offset retornado por emit_prologue)
    void patch_stack_size(size_t patch_off, int32_t max_stack, int32_t min_extra = 32) {
        int32_t stack_size = max_stack + min_extra;
        stack_size = (stack_size + 15) & ~15; // align 16
        m_text.patch_i32(patch_off + 3, stack_size); // +3 pula rex+opcode+modrm
    }

    // ========================================================================
    // SSE2 — operacoes com double (float64)
    // ========================================================================

    // movq xmm, reg (mover inteiro de reg GPR para xmm)
    // 66 48 0F 6E /r — MOVQ xmm, r/m64
    void emit_movq_xmm_reg(uint8_t xmm, uint8_t gpr) {
        m_text.emit_u8(0x66);
        rex(true, xmm >= 8, false, gpr >= 8);
        m_text.emit_u8(0x0F);
        m_text.emit_u8(0x6E);
        modrm(0b11, xmm & 7, gpr & 7);
    }

    // movq reg, xmm (mover de xmm para reg GPR)
    // 66 48 0F 7E /r — MOVQ r/m64, xmm
    void emit_movq_reg_xmm(uint8_t gpr, uint8_t xmm) {
        m_text.emit_u8(0x66);
        rex(true, xmm >= 8, false, gpr >= 8);
        m_text.emit_u8(0x0F);
        m_text.emit_u8(0x7E);
        modrm(0b11, xmm & 7, gpr & 7);
    }

    // movsd xmm, [rbp + offset]
    // F2 0F 10 /r
    void emit_movsd_xmm_rbp_offset(uint8_t xmm, int32_t offset) {
        m_text.emit_u8(0xF2);
        if (xmm >= 8) rex(false, true, false, false);
        m_text.emit_u8(0x0F);
        m_text.emit_u8(0x10);
        modrm(0b10, xmm & 7, 5); // [rbp + disp32]
        m_text.emit_i32(offset);
    }

    // movsd [rbp + offset], xmm
    // F2 0F 11 /r
    void emit_movsd_rbp_offset_xmm(int32_t offset, uint8_t xmm) {
        m_text.emit_u8(0xF2);
        if (xmm >= 8) rex(false, true, false, false);
        m_text.emit_u8(0x0F);
        m_text.emit_u8(0x11);
        modrm(0b10, xmm & 7, 5); // [rbp + disp32]
        m_text.emit_i32(offset);
    }

    // movsd xmm_dst, xmm_src
    // F2 0F 10 /r (com mod=11)
    void emit_movsd_xmm_xmm(uint8_t dst, uint8_t src) {
        m_text.emit_u8(0xF2);
        // REX se necessario
        bool need_rex = (dst >= 8 || src >= 8);
        if (need_rex) {
            uint8_t val = 0x40;
            if (dst >= 8) val |= 0x04; // R
            if (src >= 8) val |= 0x01; // B
            m_text.emit_u8(val);
        }
        m_text.emit_u8(0x0F);
        m_text.emit_u8(0x10);
        modrm(0b11, dst & 7, src & 7);
    }

    // addsd xmm, xmm
    // F2 0F 58 /r
    void emit_addsd_xmm_xmm(uint8_t dst, uint8_t src) {
        m_text.emit_u8(0xF2);
        bool need_rex = (dst >= 8 || src >= 8);
        if (need_rex) {
            uint8_t val = 0x40;
            if (dst >= 8) val |= 0x04;
            if (src >= 8) val |= 0x01;
            m_text.emit_u8(val);
        }
        m_text.emit_u8(0x0F);
        m_text.emit_u8(0x58);
        modrm(0b11, dst & 7, src & 7);
    }

    // subsd xmm, xmm
    // F2 0F 5C /r
    void emit_subsd_xmm_xmm(uint8_t dst, uint8_t src) {
        m_text.emit_u8(0xF2);
        bool need_rex = (dst >= 8 || src >= 8);
        if (need_rex) {
            uint8_t val = 0x40;
            if (dst >= 8) val |= 0x04;
            if (src >= 8) val |= 0x01;
            m_text.emit_u8(val);
        }
        m_text.emit_u8(0x0F);
        m_text.emit_u8(0x5C);
        modrm(0b11, dst & 7, src & 7);
    }

    // mulsd xmm, xmm
    // F2 0F 59 /r
    void emit_mulsd_xmm_xmm(uint8_t dst, uint8_t src) {
        m_text.emit_u8(0xF2);
        bool need_rex = (dst >= 8 || src >= 8);
        if (need_rex) {
            uint8_t val = 0x40;
            if (dst >= 8) val |= 0x04;
            if (src >= 8) val |= 0x01;
            m_text.emit_u8(val);
        }
        m_text.emit_u8(0x0F);
        m_text.emit_u8(0x59);
        modrm(0b11, dst & 7, src & 7);
    }

    // divsd xmm, xmm
    // F2 0F 5E /r
    void emit_divsd_xmm_xmm(uint8_t dst, uint8_t src) {
        m_text.emit_u8(0xF2);
        bool need_rex = (dst >= 8 || src >= 8);
        if (need_rex) {
            uint8_t val = 0x40;
            if (dst >= 8) val |= 0x04;
            if (src >= 8) val |= 0x01;
            m_text.emit_u8(val);
        }
        m_text.emit_u8(0x0F);
        m_text.emit_u8(0x5E);
        modrm(0b11, dst & 7, src & 7);
    }

    // cvtsi2sd xmm, reg (converter inteiro 64-bit para double)
    // F2 48 0F 2A /r
    void emit_cvtsi2sd_xmm_reg(uint8_t xmm, uint8_t gpr) {
        m_text.emit_u8(0xF2);
        rex(true, xmm >= 8, false, gpr >= 8);
        m_text.emit_u8(0x0F);
        m_text.emit_u8(0x2A);
        modrm(0b11, xmm & 7, gpr & 7);
    }

    // cvttsd2si reg, xmm (converter double para inteiro 64-bit, truncado)
    // F2 48 0F 2C /r
    void emit_cvttsd2si_reg_xmm(uint8_t gpr, uint8_t xmm) {
        m_text.emit_u8(0xF2);
        rex(true, gpr >= 8, false, xmm >= 8);
        m_text.emit_u8(0x0F);
        m_text.emit_u8(0x2C);
        modrm(0b11, gpr & 7, xmm & 7);
    }

    // ucomisd xmm, xmm (comparacao unordered de doubles — seta flags)
    // 66 0F 2E /r
    void emit_ucomisd_xmm_xmm(uint8_t a, uint8_t b) {
        m_text.emit_u8(0x66);
        bool need_rex = (a >= 8 || b >= 8);
        if (need_rex) {
            uint8_t val = 0x40;
            if (a >= 8) val |= 0x04;
            if (b >= 8) val |= 0x01;
            m_text.emit_u8(val);
        }
        m_text.emit_u8(0x0F);
        m_text.emit_u8(0x2E);
        modrm(0b11, a & 7, b & 7);
    }

    // xorpd xmm, xmm (zerar xmm)
    // 66 0F 57 /r
    void emit_xorpd_xmm_xmm(uint8_t dst, uint8_t src) {
        m_text.emit_u8(0x66);
        bool need_rex = (dst >= 8 || src >= 8);
        if (need_rex) {
            uint8_t val = 0x40;
            if (dst >= 8) val |= 0x04;
            if (src >= 8) val |= 0x01;
            m_text.emit_u8(val);
        }
        m_text.emit_u8(0x0F);
        m_text.emit_u8(0x57);
        modrm(0b11, dst & 7, src & 7);
    }

    // ========================================================================
    // Alinhamento
    // ========================================================================

    void align_code(size_t alignment) {
        while (m_text.pos() % alignment != 0) {
            m_text.emit_u8(0x90); // NOP
        }
    }

private:
    Section& m_text;
};

} // namespace atomic

#endif // ATOMIC_CODEGEN_X86_HPP