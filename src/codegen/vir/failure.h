/*
 * Why the VIR path gives up on a function (VIR-MIGRATION.md, gate G1).
 *
 * Every bail-out of the builder (codegen/vir/, not a64.c), of arm64's lowering (a64.c), of
 * x86-64's (arch/x64/codegen/vir.c) and of the driver (try.c) names one of
 * these.  The message is what VCC_VIR=s prints; the name is what the
 * statistic VCC_VIR_STATS=1 counts (try.c, tu.c).  Add a reason here and use
 * VIRFAIL(name) at the bail-out; never test the message text.
 */
#ifndef VCC_CODEGEN_VIR_FAILURE_H
#define VCC_CODEGEN_VIR_FAILURE_H

#define VIR_FAILURES(X) \
	X(out_of_memory, "out of memory") \
	X(variable_read_distance, "VIR: a variable read too many blocks from its definition") \
	X(value_type_mismatch, "VIR: a value of one type read as another") \
	X(no_such_register, "VIR: no such register") \
	X(integer_constant_as_float, "VIR: an integer constant as a float") \
	X(unsupported_value_kind, "VIR: this value kind") \
	X(out_of_names, "VIR: out of names") \
	X(out_of_float_names, "VIR: out of float names") \
	X(access_across_slots, "VIR: an access across two slots") \
	X(access_outside_slots, "VIR: a frame access outside every slot") \
	X(unsupported_lvalue_kind, "VIR: this lvalue kind") \
	X(bit_field_lvalue, "VIR: a bit-field lvalue") \
	X(float_load_size, "VIR: a float load of this size") \
	X(float_store_size, "VIR: a float store of this size") \
	X(register_variable_address, "VIR: the address of a register variable") \
	X(unsupported_operator, "VIR: this operator") \
	X(float_extension, "VIR: extension of a float") \
	X(switch_bit_set, "VIR: switch bit set") \
	X(wide_integer, "VIR: __int128") \
	X(bit_precise_integer, "VIR: _BitInt") \
	X(unsupported_floating_type, "VIR: this floating type") \
	X(root4, "VIR: root4") \
	X(string_compare, "VIR: pcmpstr") \
	X(unsupported_float_comparison, "VIR: this float comparison") \
	X(unsupported_builtin, "VIR: this builtin") \
	X(thread_local, "VIR: thread-local") \
	X(frame_address, "VIR: frame address") \
	X(alloca, "VIR: alloca") \
	X(stack_pointer, "VIR: stack pointer") \
	X(setjmp, "VIR: setjmp") \
	X(longjmp, "VIR: longjmp") \
	X(inline_assembly, "VIR: inline asm") \
	X(variadic_start, "VIR: va_start (AAPCS64's register save areas)") \
	X(variadic_argument, "VIR: va_arg (AAPCS64's register save areas)") \
	X(record_result, "VIR: a record result") \
	X(computed_goto, "VIR: computed goto") \
	X(label_address, "VIR: an address-taken label") \
	X(record_argument_or_result, "VIR: an argument or result passed as a record") \
	X(record_argument, "VIR: a record argument") \
	X(record_parameter, "VIR: a record parameter") \
	X(variadic_function, "VIR: a variadic function (AAPCS64's register save areas)") \
	X(parameters, "VIR: parameters") \
	X(joined_conditions, "VIR: joined conditions") \
	X(use_before_definition, "VIR: a value used before its definition") \
	X(block_without_terminator, "VIR: a block without a terminator") \
	X(build_failed, "VIR: the function could not be built") \
	X(emitter_error, "VIR: the walker reported an error") \
	X(verifier_rejected, "VIR: the verifier rejected the function") \
	X(debug_info, "VIR lowering: -g") \
	X(loop_analysis, "VIR: loops") \
	X(allocation_failed, "VIR: allocation failed") \
	X(allocation_check_failed, "VIR: the allocation check failed") \
	X(lowering_failed, "VIR lowering: failed") \
	X(constant_not_materializable, "VIR lowering: a constant it cannot materialize") \
	X(float_constant_not_fconst, "VIR lowering: a float constant that is not fconst") \
	X(operand_without_location, "VIR lowering: an operand without a location") \
	X(float_operand_without_register, "VIR lowering: a float operand without a register") \
	X(move_between_register_classes, "VIR lowering: a move between register classes") \
	X(swapped_operands, "VIR lowering: swapped operands") \
	X(vector_spill_move_scratch_taken, "VIR lowering: a vector moved between spill slots while the scratch register is taken") \
	X(exchange_with_slot, "VIR lowering: an exchange with a slot") \
	X(unknown_edge_move, "VIR lowering: an unknown edge move") \
	X(too_many_edge_moves, "VIR lowering: too many edges with moves from one block") \
	X(memset_nonzero_byte, "VIR lowering: memset of a nonzero byte") \
	X(memcpy_address_without_register, "VIR lowering: a memcpy address without a register") \
	X(call_operands, "VIR lowering: call operands") \
	X(argument_not_in_register, "VIR lowering: an argument not in its register") \
	X(indirect_callee_without_register, "VIR lowering: an indirect callee without a register") \
	X(lowering_parameter, "VIR lowering: a parameter") \
	X(parameter_not_in_register, "VIR lowering: a parameter not in its register") \
	X(switch_lowering, "VIR lowering: switch") \
	X(vector_fma_without_register, "VIR lowering: a vector fma without a free register") \
	X(vector_select, "VIR lowering: select of vectors") \
	X(vector_compare_without_scratch, "VIR lowering: a vector compare without a scratch register") \
	X(constant_remainder, "VIR lowering: a remainder of a constant") \
	X(variadic_start_lowering, "VIR lowering: va_start") \
	X(too_many_operands, "VIR lowering: too many operands") \
	X(lowering_block_without_terminator, "VIR lowering: a block without a terminator") \
	X(over_aligned_slot, "VIR lowering: an over-aligned slot") \
	X(large_frame_scratch, "VIR lowering: x17 across a large frame") \
	X(unsupported_instruction, "VIR lowering: an instruction it does not lower") \
	X(x64_unsupported_target_option, "VIR x86-64: -g, stack protector or no SIMD") \
	X(x64_interrupt_function, "VIR x86-64: an interrupt function") \
	X(x64_constant_kind, "VIR x86-64: a constant of a kind it cannot place") \
	X(x64_constant_range, "VIR x86-64: a symbol or slot address beyond the 32-bit range") \
	X(x64_too_many_call_arguments, "VIR x86-64: too many call arguments") \
	X(x64_returns_twice, "VIR x86-64: returns_twice") \
	X(x64_unsupported_instruction, "VIR x86-64: an instruction it does not lower") \
	X(x64_slot_size, "VIR x86-64: an over-aligned or huge slot") \
	X(x64_frame_size, "VIR x86-64: a frame beyond the 32-bit range") \
	X(x64_loop_analysis, "VIR x86-64: loop analysis failed") \
	X(x64_allocation_failed, "VIR x86-64: allocation failed") \
	X(x64_allocation_check_failed, "VIR x86-64: allocation check failed") \
	X(x64_lowering_failed, "VIR x86-64: lowering failed")

typedef enum {
#define X(name, message) VIRFAIL_##name,
	VIR_FAILURES(X)
#undef X
	VIRFAIL_COUNT
} vir_failure;

typedef struct { const char *name, *message; } vir_failure_info;

/* the table is defined in try.c; the message of a reason, which is what vfail holds */
#define VIRFAIL(name) (vir_failure_table[VIRFAIL_##name].message)

#endif
