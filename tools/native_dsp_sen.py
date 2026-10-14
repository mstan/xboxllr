"""Pinned MIT donor patch: bounded hardware-resident SEN, never spill/reload."""
FILES = frozenset({'crates/emu/src/core.rs', 'crates/emu/src/emit/mod.rs',
    'crates/emu/src/emit/regs.rs', 'crates/emu/src/emit/bitops.rs', 'crates/emu/src/emit/loops.rs', 'crates/emu/src/emit/control.rs', 'crates/emu/src/emit/logical.rs', 'crates/emu/src/jit.rs'})
CORE = r"""
// DSP56300FM 5.4.2/5.4.3: closed resident domain, not an extension algorithm.
// SP is logical in SEN mode; equality with SC proves no spilled history here.
impl DspState {
    pub(crate) fn guard_resident_sen(&self, pushes: u32, pulls: u32) {
        if self.registers[reg::OMR] & (1 << 20) == 0 { return; }
        let sp = self.registers[reg::SP];
        let sc = self.registers[reg::SC];
        assert!(self.registers[reg::OMR] & (7 << 17) == 0,
            "Unqualified SEN status/history");
        assert!(sp == sc && sc <= 14 && self.registers[reg::SZ] != 0,
            "Unqualified SEN logical/hardware stack state");
        assert!(self.interrupts.state == InterruptState::None && !self.interrupts.has_pending(),
            "Unqualified SEN interrupt entry");
        assert!(sp >= pulls, "Unqualified SEN underflow/refill");
        assert!(sp + pushes <= self.registers[reg::SZ], "Unqualified SEN SZ overflow");
        assert!(sc + pushes <= 14, "Unqualified SEN spill boundary");
    }
}
pub unsafe extern "C" fn jit_sen_stack_guard(state: *mut DspState, operation: u32) {
    let s = unsafe { &mut *state };
    if s.registers[reg::OMR] & (1 << 20) == 0 { return; }
    let (pushes, pulls) = match operation { 1 => (1,0), 2 => (0,1),
        3 => (2,0), 4 => (0,2), 5 => panic!("Unqualified SEN explicit/early-abort/interrupt operation"), _ => panic!("Invalid SEN operation") };
    s.guard_resident_sen(pushes,pulls);
}
pub unsafe extern "C" fn jit_write_omr(state: *mut DspState, value: u32) {
    let s = unsafe { &mut *state };
    let value = value & REG_MASKS[reg::OMR];
    if (s.registers[reg::OMR] | value) & (1 << 20) != 0 {
        assert!((s.registers[reg::OMR] | value) & (7 << 17) == 0,
            "Unqualified SEN proposed status/history");
    }
    if (s.registers[reg::OMR] ^ value) & (1 << 20) != 0 {
        assert!(s.registers[reg::SP] == 0 && s.registers[reg::SC] == 0,
            "Unqualified SEN occupied mode transition");
        assert!((s.registers[reg::OMR] | value) & (7 << 17) == 0,
            "Unqualified SEN status/history transition");
        assert!(s.interrupts.state == InterruptState::None && !s.interrupts.has_pending(),
            "Unqualified SEN interrupt transition");
        if value & (1 << 20) != 0 {
            assert!(s.registers[reg::SZ] != 0, "Unqualified SEN unset SZ");
        }
    } else { s.guard_resident_sen(0,0); }
    s.registers[reg::OMR] = value;
}
macro_rules! resident_config_write {
    ($name:ident, $reg:ident) => {
        pub unsafe extern "C" fn $name(state: *mut DspState, value: u32) {
            let s = unsafe { &mut *state };
            assert!(s.registers[reg::OMR] & (1 << 20) == 0,
                "Unqualified SEN direct task/configuration write");
            s.registers[reg::$reg] = value & REG_MASKS[reg::$reg];
        }
    }
}
resident_config_write!(jit_write_sc, SC);
resident_config_write!(jit_write_sz, SZ);
resident_config_write!(jit_write_ep, EP);
"""
EMITTER = r"""
    // Keep disabled-mode stack paths unchanged. Runtime guards see spilled
    // promoted register values and never mutate any architecture state.
    fn emit_resident_sen_guard(&mut self, operation: u32) {
        let omr = self.load_reg(reg::OMR);
        let enabled = self.builder.ins().band_imm_u(omr, 1 << 20);
        let guarded = self.builder.create_block();
        let merge = self.builder.create_block();
        let mut conditional = self.begin_conditional();
        self.builder.ins().brif(enabled, guarded, &[], merge, &[]);
        self.builder.switch_to_block(guarded);
        self.builder.seal_block(guarded);
        let op = self.builder.ins().iconst(types::I32, operation as i64);
        self.emit_call_extern_val(jit_sen_stack_guard as *const () as usize, op);
        self.end_conditional_arm(&mut conditional);
        self.builder.ins().jump(merge, &[]);
        self.builder.switch_to_block(merge);
        self.builder.seal_block(merge);
        self.merge_conditional(&conditional);
    }
"""
TESTS = r"""
#[cfg(test)]
mod resident_sen_tests {
    use super::*;
    use crate::jit::JitEngine;
    use std::panic::{catch_unwind, AssertUnwindSafe};
    fn resident() -> DspState {
        let mut s=DspState::new(MemoryMap::default());
        s.registers[reg::OMR] |= 1<<20;
        s.registers[reg::SZ]=32;
        s.registers[reg::EP]=0xbe0;
        s
    }
    #[test] fn sen_resident_balanced_and_capacity() {
        let mut s=resident();let omr=s.registers[reg::OMR];
        for n in 1..=14 {s.stack_push(n,0x100+n);assert_eq!(s.registers[reg::SP],n);assert_eq!(s.registers[reg::SC],n);}
        for n in (1..=14).rev(){assert_eq!(s.stack_pop(),(n,0x100+n));}
        assert_eq!(s.registers[reg::SP],0);assert_eq!(s.registers[reg::SC],0);
        assert_eq!(s.registers[reg::OMR],omr);assert_eq!(s.registers[reg::EP],0xbe0);assert_eq!(s.registers[reg::SZ],32);
    }
    #[test] fn sen_spill_fails_before_stack_mutation() {
        let mut s=resident();for n in 0..14{s.stack_push(n,n+1);}
        let before=s.registers;let stack=s.stack;
        assert!(catch_unwind(AssertUnwindSafe(||s.stack_push(99,100))).is_err());
        assert_eq!(s.registers,before);assert_eq!(s.stack,stack);assert!(!s.interrupts.has_pending());
    }
    #[test] fn sen_sz_and_underflow_before_mutation() {
        let mut s=resident();let before=s.registers;
        assert!(catch_unwind(AssertUnwindSafe(||s.stack_pop())).is_err());assert_eq!(s.registers,before);
        s.registers[reg::SZ]=1;s.stack_push(7,8);let before=s.registers;let stack=s.stack;
        assert!(catch_unwind(AssertUnwindSafe(||s.stack_push(9,10))).is_err());assert_eq!(s.registers,before);assert_eq!(s.stack,stack);
    }
    #[test] fn sen_history_and_interrupt_are_not_admitted() {
        for mode in 0..5 {
            let mut s=resident();
            match mode {0=>s.registers[reg::SP]=1,1=>s.registers[reg::SC]=15,
                2=>s.registers[reg::OMR]|=1<<19,3=>s.interrupts.set_pending(interrupt::TRAP),_=>s.interrupts.state=InterruptState::Fast}
            let before=s.registers;let stack=s.stack;
            assert!(catch_unwind(AssertUnwindSafe(||s.stack_push(1,2))).is_err());assert_eq!(s.registers,before);assert_eq!(s.stack,stack);
        }
    }
    #[test] fn sen_double_operation_preflight_is_atomic() {
        let mut s=resident();for n in 0..13{s.stack_push(n,n);}
        let before=s.registers;
        assert!(catch_unwind(AssertUnwindSafe(||s.guard_resident_sen(2,0))).is_err());assert_eq!(s.registers,before);
        let mut s=resident();s.stack_push(1,2);let before=s.registers;
        assert!(catch_unwind(AssertUnwindSafe(||s.guard_resident_sen(0,2))).is_err());assert_eq!(s.registers,before);
    }
    #[test] fn sen_actual_single_and_block_nested_call_dor() {
        for single in [true,false] {
            let mut x=[0x654321;3072];let mut y=[0x123456;256];let mut p=[0x87;4096];
            p[..8].copy_from_slice(&[0x0500bb,0x05f42a,0xbe0,0x0520b8,0x0afa74,0x0bf080,0x20,0x87]);
            p[0x20..0x23].copy_from_slice(&[0x0bf080,0x40,0x0c]);
            p[0x40..0x44].copy_from_slice(&[0x060290,2,0,0x0c]);
            let original_p=p;let mut s=DspState::new(MemoryMap::test(&mut x,&mut y,&mut p));let mut j=JitEngine::new(4096);
            if single {let mut n=0;while s.power_state!=PowerState::Stop {assert!(n<100);n+=1;s.execute_one(&mut j);}}
            else{s.run(&mut j,1000);}
            assert_eq!(s.power_state,PowerState::Stop);assert_eq!(s.registers[reg::SP],0);assert_eq!(s.registers[reg::SC],0);
            assert_eq!(s.registers[reg::OMR],0x100300);assert_eq!(s.registers[reg::EP],0xbe0);assert_eq!(s.registers[reg::SZ],32);
            assert_eq!(p,original_p);assert!(x.iter().all(|&v|v==0x654321));assert!(y.iter().all(|&v|v==0x123456));
        }
    }
}
"""

def once(text, old, new, name):
    if text.count(old)!=1: raise ValueError('SEN seam changed: '+name+' '+old[:72])
    return text.replace(old,new)
def patch(name,text):
    def replace(old,new):
        nonlocal text
        text=once(text,old,new,name)
    if name=='crates/emu/src/core.rs':
        replace('    pub fn stack_push(&mut self, ssh_val: u32, ssl_val: u32) {',
            '    pub fn stack_push(&mut self, ssh_val: u32, ssl_val: u32) {\n        self.guard_resident_sen(1,0);')
        replace('    pub fn stack_pop(&mut self) -> (u32, u32) {',
            '    pub fn stack_pop(&mut self) -> (u32, u32) {\n        self.guard_resident_sen(0,1);')
        replace('        const OMR_UNIMPL: u32 = (1 << 20) | (1 << 7); // SEN, MS',
            '        self.guard_resident_sen(0,0);\n        const OMR_UNIMPL: u32 = 1 << 7; // MS remains unsupported; SEN is guarded above.')
        replace('            self.registers[reg::LC] = self.registers[reg::LC].wrapping_sub(1) & REG_MASKS[reg::LC];',
            '            if self.registers[reg::LC] == 1 && (self.registers[reg::SR] & (1 << sr::FV)) == 0 { self.guard_resident_sen(0,2); }\n            self.registers[reg::LC] = self.registers[reg::LC].wrapping_sub(1) & REG_MASKS[reg::LC];')
        replace('    pub fn process_pending_interrupts(&mut self) {',
            '    pub fn process_pending_interrupts(&mut self) {\n        self.guard_resident_sen(0,0);')
        replace('pub unsafe extern "C" fn jit_write_ssh(state: *mut DspState, value: u32) {\n    let state = unsafe { &mut *state };',
            'pub unsafe extern "C" fn jit_write_ssh(state: *mut DspState, value: u32) {\n    let state = unsafe { &mut *state };\n    assert!(state.registers[reg::OMR] & (1 << 20) == 0, \"Unqualified SEN explicit SSH write\");')
        replace('pub unsafe extern "C" fn jit_read_ssh(state: *mut DspState) -> u32 {\n    let state = unsafe { &mut *state };', 'pub unsafe extern "C" fn jit_read_ssh(state: *mut DspState) -> u32 {\n    let state = unsafe { &mut *state };\n    assert!(state.registers[reg::OMR] & (1 << 20) == 0, "Unqualified SEN explicit SSH read");')
        replace('pub unsafe extern "C" fn jit_write_ssh_tos(state: *mut DspState, value: u32) {\n    let state = unsafe { &mut *state };',
            'pub unsafe extern "C" fn jit_write_ssh_tos(state: *mut DspState, value: u32) {\n    let state = unsafe { &mut *state };\n    state.guard_resident_sen(0,0);\n    assert!(state.registers[reg::OMR] & (1 << 20) == 0, "Unqualified SEN explicit SSH top write");')
        replace('pub unsafe extern "C" fn jit_write_ssl(state: *mut DspState, value: u32) {\n    let state = unsafe { &mut *state };',
            'pub unsafe extern "C" fn jit_write_ssl(state: *mut DspState, value: u32) {\n    let state = unsafe { &mut *state };\n    assert!(state.registers[reg::OMR] & (1 << 20) == 0, "Unqualified SEN explicit SSL write");')
        replace('pub unsafe extern "C" fn jit_write_sp(state: *mut DspState, value: u32) {\n    let state = unsafe { &mut *state };',
            'pub unsafe extern "C" fn jit_write_sp(state: *mut DspState, value: u32) {\n    let state = unsafe { &mut *state };\n    assert!(state.registers[reg::OMR] & (1 << 20) == 0, "Unqualified SEN direct SP task switch");')
        text += CORE + TESTS
    elif name=='crates/emu/src/emit/mod.rs':
        replace('    jit_write_ssh, jit_write_ssh_tos, jit_write_ssl, reg, sr,',
            '    jit_write_ssh, jit_write_ssh_tos, jit_write_ssl, jit_sen_stack_guard, jit_write_omr, jit_write_sc, jit_write_sz, jit_write_ep, reg, sr,')
        replace('    fn stack_push(&mut self, ssh_val: Value, ssl_val: Value, fault_budget: u32) {',
            EMITTER+'\n    fn stack_push(&mut self, ssh_val: Value, ssl_val: Value, fault_budget: u32) {\n        self.emit_resident_sen_guard(1);')
        replace('    fn stack_pop(&mut self, fault_budget: u32) -> (Value, Value) {',
            '    fn stack_pop(&mut self, fault_budget: u32) -> (Value, Value) {\n        self.emit_resident_sen_guard(2);')
        begin=text.index('    pub(super) fn may_arm_stack_fault(')
        end=text.index('    /// Emit IR for a single decoded instruction.',begin)
        function=text[begin:end]
        function=function.replace('may_arm_stack_fault','explicit_stack_port')
        function=function.replace('matches!(r as usize, reg::SP | reg::SSH | reg::SSL)','matches!(r as usize, reg::SSH | reg::SSL)')
        old='            Instruction::EndDo\n            | Instruction::Rts\n            | Instruction::Rti\n            | Instruction::Trap\n            | Instruction::Trapcc { .. }\n            | Instruction::Illegal => true,'
        function=once(function,old,'',name)
        text=text[:end]+function+text[end:]
        replace('        self.cur_decode_len = decode::instruction_length(inst);',
            '        self.cur_decode_len = decode::instruction_length(inst);\n        if Self::explicit_stack_port(inst) { self.emit_resident_sen_guard(5); }')
        replace('            matches!(r as usize, reg::SP | reg::SSH | reg::SSL)',
            '            matches!(r as usize, reg::SP | reg::SSH | reg::SSL | reg::SC | reg::SZ | reg::EP | reg::OMR)')
        replace('        match inst {\n            Instruction::BtstReg', '        match inst {\n            Instruction::AndI { dest, .. } | Instruction::OrI { dest, .. } => *dest >= 2,\n            Instruction::BtstReg')
        replace('        let ssh_or_sp = |r: u8| r as usize == reg::SSH || r as usize == reg::SP;',
            '        let ssh_or_sp = |r: u8| matches!(r as usize, reg::SSH | reg::SSL | reg::SP | reg::SC | reg::SZ | reg::EP | reg::OMR);')
    elif name=='crates/emu/src/emit/regs.rs':
        replace('        } else if idx == reg::SP {\n            self.emit_spill_fault_budget(FaultClass::SpWrite);',
            '        } else if idx == reg::OMR {\n            self.emit_call_extern_val(jit_write_omr as *const () as usize, val);\n            self.refresh_promoted_reg(reg::OMR);\n        } else if idx == reg::SC {\n            self.emit_call_extern_val(jit_write_sc as *const () as usize, val);\n        } else if idx == reg::SZ {\n            self.emit_call_extern_val(jit_write_sz as *const () as usize, val);\n        } else if idx == reg::EP {\n            self.emit_call_extern_val(jit_write_ep as *const () as usize, val);\n        } else if idx == reg::SP {\n            self.emit_spill_fault_budget(FaultClass::SpWrite);')
    elif name=='crates/emu/src/emit/bitops.rs':
        replace('            } else if is_full_acc {\n                self.write_reg_for_move(reg_idx as usize, result);',
            '            } else if is_full_acc || matches!(reg_idx as usize, reg::OMR | reg::SC | reg::SZ | reg::EP) {\n                self.write_reg_for_move(reg_idx as usize, result);')
    elif name=='crates/emu/src/jit.rs':
        replace('                self.registers[reg::LC] =\n                    self.registers[reg::LC].wrapping_sub(1) & REG_MASKS[reg::LC];',
            '                if self.registers[reg::LC] == 1 && (self.registers[reg::SR] & (1 << sr::FV)) == 0 { self.guard_resident_sen(0,2); }\n                self.registers[reg::LC] =\n                    self.registers[reg::LC].wrapping_sub(1) & REG_MASKS[reg::LC];')
        replace('    fn step_one(&mut self, jit: &mut JitEngine) -> i32 {','    fn step_one(&mut self, jit: &mut JitEngine) -> i32 {\n        self.guard_resident_sen(0,0);')
        replace('        while self.cycle_budget > 0 && !self.halt_requested {','        while self.cycle_budget > 0 && !self.halt_requested {\n            self.guard_resident_sen(0,0);')
    elif name=='crates/emu/src/emit/logical.rs':
        if text.count('self.store_reg(reg::OMR, r);')!=4: raise ValueError('SEN OMR logical seams changed')
        text=text.replace('self.store_reg(reg::OMR, r);','self.write_reg_for_move(reg::OMR, r);')
    elif name=='crates/emu/src/emit/control.rs':
        replace('    pub(super) fn emit_add_interrupt(&mut self, inter: usize) {','    pub(super) fn emit_add_interrupt(&mut self, inter: usize) {\n        self.emit_resident_sen_guard(5);')
    elif name=='crates/emu/src/emit/loops.rs':
        replace('    pub(super) fn emit_enddo_cleanup(&mut self) {',
            '    pub(super) fn emit_enddo_cleanup(&mut self) {\n        self.emit_resident_sen_guard(4);')
        replace('        let old_la = self.load_reg(reg::LA);',
            '        self.emit_resident_sen_guard(3);\n        let old_la = self.load_reg(reg::LA);')
        replace('    pub(super) fn emit_enddo(&mut self) {','    pub(super) fn emit_enddo(&mut self) {\n        self.emit_resident_sen_guard(5);')
        replace('    pub(super) fn emit_brkcc(&mut self, cc: CondCode) {','    pub(super) fn emit_brkcc(&mut self, cc: CondCode) {\n        self.emit_resident_sen_guard(5);')
    return text
