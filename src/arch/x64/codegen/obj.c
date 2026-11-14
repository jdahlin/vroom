/* x86 records -> structured assembler operands. Encoding, relocations and
 * branch relaxation belong to the existing assembler, shared with -S input. */
#include "x64-internal.h"
#include "../as/operand.h"

VPRIV bool x64_record_object_prepare(codegen_context *e, const record *i, record *out)
{
	switch (i->opcode) {
	case X64_OP_LABEL: case X64_OP_MOV: case X64_OP_MOVI: case X64_OP_LD: case X64_OP_LDX:
	case X64_OP_ST: case X64_OP_STX: case X64_OP_LEA: case X64_OP_LEASCALE:
	case X64_OP_ADD: case X64_OP_SUB: case X64_OP_AND: case X64_OP_OR: case X64_OP_XOR:
	case X64_OP_IMUL: case X64_OP_ADC: case X64_OP_SBB:
	case X64_OP_ADDI: case X64_OP_SUBI: case X64_OP_ANDI: case X64_OP_ORI: case X64_OP_XORI:
	case X64_OP_SHLI: case X64_OP_SHRI: case X64_OP_SARI: case X64_OP_SHL: case X64_OP_SHR: case X64_OP_SAR:
	case X64_OP_IMULI: case X64_OP_NEG: case X64_OP_NOT: case X64_OP_CMP: case X64_OP_CMPI:
	case X64_OP_TEST: case X64_OP_CMOV: case X64_OP_SETCC: case X64_OP_JMP: case X64_OP_JCC:
	case X64_OP_PUSH: case X64_OP_POP: case X64_OP_CQO:
	case X64_OP_LEAL: case X64_OP_MFENCE: case X64_OP_UD2: case X64_OP_BSF: case X64_OP_BSR:
	case X64_OP_MULW: case X64_OP_SMULW: case X64_OP_DIV: case X64_OP_IDIV: case X64_OP_EXT:
	case X64_OP_FADD: case X64_OP_FSUB: case X64_OP_FMUL: case X64_OP_FDIV: case X64_OP_FXOR:
		break;
	case X64_OP_CALLR: case X64_OP_JMPR:
		if (e->unit->options->xcf & 4) return false;
		break;
	case X64_OP_CALL:
		if (CODEGEN_IS_WIN64(e->compile_context) && codegen_symbol_needs_got(e->compile_context, i->reference)) return false;	/* (callq *__imp_f(%rip): text) */
		break;
	default: return false;
	}
	*out = *i;
	if (i->opcode == X64_OP_LD || i->opcode == X64_OP_LDX || i->opcode == X64_OP_ST || i->opcode == X64_OP_STX || i->opcode == X64_OP_LEA) {
		if (i->registers[1] == X64_REGISTER_FRAME) {
			int64_t off = i->offset + e->x64_context->frame_size - 8 - e->x64_context->stack_pointer_delta;
			/* Preserve the textual path for a displacement wider than rec.off. */
			if (off < INT32_MIN || off > INT32_MAX) return false;
			out->registers[1] = RSP; out->offset = (int32_t)off;
		} else if (i->registers[1] == RBP && i->offset < 0 && e->walk_state->frame_alignment > 16)
			out->registers[1] = R12;
		else if (i->registers[1] == RBP && i->offset < 0)
			out->offset = (int32_t)(i->offset - e->x64_context->wsave);	/* (the Windows frame's save area, print.c) */
	}
	return true;
}

static void x64_encode_register(XOp *operand, int r, int size)
{
	*operand = (XOp){ .kind = XK_REG, .cls = X64_REGISTER_IS_XMM(r) ? XR_XMM : XR_GPR,
		.reg = (uint8_t)(X64_REGISTER_IS_XMM(r) ? r - XMM0 : r), .size = (uint8_t)(X64_REGISTER_IS_XMM(r) ? 16 : size),
		.needrex = !X64_REGISTER_IS_XMM(r) && size == 1 && r >= 4 };
}

static XOp x64_encode_immediate(int64_t v)
{
	return (XOp){ .kind = XK_IMM, .v = { .c = v }, .hasv = 1 };
}

static uint32_t x64_encode_label(codegen_commit_context *R, uint32_t fid, uint32_t label)
{
	R->name_buffer.n = 0;
	x64_label_name_append(&R->name_buffer, fid, label);
	return as_symbol_by_name(R->assembler, R->name_buffer.p, R->name_buffer.n);
}

static XOp x64_encode_memory(codegen_commit_context *R, const record *i)
{
	XOp m = { .kind = XK_MEM, .base = (int8_t)(i->registers[1] == RIP ? 16 : i->registers[1]), .index = -1, .scale = 1,
		.v = { .c = i->offset }, .hasv = 1 };
	if (i->registers[1] == RIP)
		m.v = as_symbol_value(R->assembler, codegen_commit_symbol_find(R, i->reference), i->offset);
	if (i->opcode == X64_OP_LDX || i->opcode == X64_OP_STX) {
		m.index = (int8_t)i->registers[3]; m.scale = (uint8_t)(1u << i->shift);
	}
	return m;
}

static int x64_encode_instruction(codegen_commit_context *R, const char *name, int sz, XOp *o, int n)
{
	char mn[32];
	size_t k = strlen(name);
	memcpy(mn, name, k);
	if (sz) mn[k++] = sz == 8 ? 'q' : sz == 4 ? 'l' : sz == 2 ? 'w' : 'b';
	mn[k] = 0;
	return as_x64_insn(R->assembler, mn, o, n);
}

VPRIV int x64_function_commit(codegen_commit_context *R, const char *p, size_t n)
{
	uint32_t fid = *(const uint32_t *)p;
	const record *in = (const record *)(p + 8);
	static const char *const ccname[] = { "o", "no", "b", "ae", "e", "ne", "be", "a", "s", "ns", "p", "np", "l", "ge", "le", "g" };
	static const enum X64Op integer_op[] = {
		[X64_OP_ADD]=X64O_ADD, [X64_OP_ADDI]=X64O_ADD, [X64_OP_SUB]=X64O_SUB, [X64_OP_SUBI]=X64O_SUB,
		[X64_OP_AND]=X64O_AND, [X64_OP_ANDI]=X64O_AND, [X64_OP_OR]=X64O_OR, [X64_OP_ORI]=X64O_OR,
		[X64_OP_XOR]=X64O_XOR, [X64_OP_XORI]=X64O_XOR, [X64_OP_ADC]=X64O_ADC, [X64_OP_SBB]=X64O_SBB,
	};
	static const char *const shift[] = {
		[X64_OP_SHLI]="shl", [X64_OP_SHRI]="shr", [X64_OP_SARI]="sar", [X64_OP_SHL]="shl", [X64_OP_SHR]="shr", [X64_OP_SAR]="sar",
	};
	for (size_t k = 0; k < (n - 8) / sizeof(record); k++) {
		const record *i = &in[k];
		int sz = i->size, count = 2, op = -1;
		const char *mn = NULL;
		char name[24];
		XOp o[3];
		if (i->opcode == X64_OP_LABEL) {
			if (as_label_define_symbol(R->assembler, x64_encode_label(R, fid, i->label))) return -1;
			continue;
		}
		x64_encode_register(&o[0], i->registers[1], sz);
		x64_encode_register(&o[1], i->registers[0], sz);
		switch (i->opcode) {
		case X64_OP_MOV:
			mn = "mov";
			if (!X64_REGISTER_IS_XMM(i->registers[0]) && !X64_REGISTER_IS_XMM(i->registers[1])) op=X64O_MOV;
			if (X64_REGISTER_IS_XMM(i->registers[0]) && X64_REGISTER_IS_XMM(i->registers[1])) { mn="movaps"; sz=0; }
			else if (X64_REGISTER_IS_XMM(i->registers[0]) || X64_REGISTER_IS_XMM(i->registers[1])) { mn=sz==4?"movd":"movq"; sz=0; }
			break;
		case X64_OP_MOVI:
			mn="mov"; op=X64O_MOV;
			if (sz==4) o[0]=x64_encode_immediate((int32_t)i->immediate);
			else if (i->immediate >= INT32_MIN && i->immediate <= INT32_MAX) { sz=8; o[0]=x64_encode_immediate(i->immediate); }
			else if ((uint64_t)i->immediate <= UINT32_MAX) { sz=4; o[0]=x64_encode_immediate(i->immediate); }
			else { op=-1; mn="movabs"; sz=8; o[0]=x64_encode_immediate(i->immediate); }
			x64_encode_register(&o[1], i->registers[0],sz); break;
		case X64_OP_LD: case X64_OP_LDX: case X64_OP_EXT:
			if (i->opcode != X64_OP_EXT) o[0] = x64_encode_memory(R, i);
			if (X64_REGISTER_IS_XMM(i->registers[0])) mn=sz==4?"movss":sz==8?"movsd":"movups";
			else {
				static const char *const sx[] = { [1]="movsbq",[2]="movswq",[4]="movslq",[8]="movq" };
				static const char *const zx[] = { [1]="movzbl",[2]="movzwl",[4]="movl",[8]="movq" };
				mn=i->condition?sx[sz]:zx[sz]; x64_encode_register(&o[1], i->registers[0],i->condition||sz==8?8:4);
				if (sz==8 || (sz==4 && !i->condition)) op=X64O_MOV;
			}
			sz=0; break;
		case X64_OP_ST: case X64_OP_STX:
			mn="mov"; x64_encode_register(&o[0], i->registers[2],sz); o[1]=x64_encode_memory(R,i);
			if (!X64_REGISTER_IS_XMM(i->registers[2])) op=X64O_MOV;
			if (X64_REGISTER_IS_XMM(i->registers[2])) { mn=sz==4?"movss":sz==8?"movsd":"movups"; sz=0; } break;
		case X64_OP_LEA: mn="lea"; o[0]=x64_encode_memory(R,i); break;
		case X64_OP_LEASCALE:
			mn="lea"; o[0]=(XOp){ .kind=XK_MEM,.base=(int8_t)i->registers[1],.index=(int8_t)i->registers[2],.scale=(uint8_t)(1u<<i->shift) }; break;
		case X64_OP_ADD: case X64_OP_SUB: case X64_OP_AND: case X64_OP_OR: case X64_OP_XOR: case X64_OP_ADC: case X64_OP_SBB:
			op = integer_op[i->opcode]; x64_encode_register(&o[0], i->registers[2], sz); break;
		case X64_OP_ADDI: case X64_OP_SUBI: case X64_OP_ANDI: case X64_OP_ORI: case X64_OP_XORI:
			op = integer_op[i->opcode]; o[0] = x64_encode_immediate(i->immediate); break;
		case X64_OP_IMUL: mn = "imul"; x64_encode_register(&o[0], i->registers[2], sz); break;
		case X64_OP_SHLI: case X64_OP_SHRI: case X64_OP_SARI: mn = shift[i->opcode]; o[0] = x64_encode_immediate(i->immediate); break;
		case X64_OP_SHL: case X64_OP_SHR: case X64_OP_SAR: mn = shift[i->opcode]; x64_encode_register(&o[0], RCX, 1); break;
		case X64_OP_IMULI: mn="imul"; o[2]=o[1]; o[1]=o[0]; o[0]=x64_encode_immediate(i->immediate); count=3; break;
		case X64_OP_NEG: case X64_OP_NOT: mn=i->opcode==X64_OP_NEG?"neg":"not"; o[0]=o[1]; count=1; break;
		case X64_OP_CMP: op=X64O_CMP; mn="cmp"; o[1]=o[0]; x64_encode_register(&o[0], i->registers[2],sz); break;
		case X64_OP_CMPI: op=X64O_CMP; mn="cmp"; o[1]=o[0]; o[0]=x64_encode_immediate(i->immediate); break;
		case X64_OP_TEST: mn="test"; o[1]=o[0]; break;
		case X64_OP_CMOV: snprintf(name,sizeof name,"cmov%s",ccname[i->condition]); mn=name; x64_encode_register(&o[0], i->registers[2],sz); break;
		case X64_OP_SETCC:
			snprintf(name,sizeof name,"set%s",ccname[i->condition]); x64_encode_register(&o[0], i->registers[0],1);
			if (x64_encode_instruction(R,name,0,o,1)) return -1;
			mn="movzbl"; sz=0; x64_encode_register(&o[1], i->registers[0],4); break;
		case X64_OP_JMP: case X64_OP_JCC: case X64_OP_LEAL:
			o[0]=(XOp){ .kind=XK_EXPR,.v=as_symbol_value(R->assembler,x64_encode_label(R,fid,i->label),0),.hasv=1 };
			if (i->opcode==X64_OP_LEAL) { mn="lea";sz=8;o[0].kind=XK_MEM;o[0].base=16;o[0].index=-1;o[0].scale=1; }
			else { snprintf(name,sizeof name,"j%s",i->opcode==X64_OP_JMP?"mp":ccname[i->condition]);mn=name;sz=0;count=1; } break;
		case X64_OP_CALL:
			mn="callq";sz=0;count=1;o[0]=(XOp){ .kind=XK_EXPR,.v=as_symbol_value(R->assembler,codegen_commit_symbol_find(R,i->reference),0),.hasv=1 };
			if(R->compile_context->o->pic&&(CODEGEN_REFERENCE_KIND(i->reference)==CODEGEN_REFERENCE_NAME||codegen_symbol_needs_got(R->compile_context,i->reference)))o[0].mod=XM_PLT;
			break;
		case X64_OP_CALLR: case X64_OP_JMPR: mn=i->opcode==X64_OP_CALLR?"callq":"jmpq";sz=0;count=1;x64_encode_register(&o[0], i->registers[1],8);o[0].star=1;break;
		case X64_OP_PUSH: case X64_OP_POP: mn=i->opcode==X64_OP_PUSH?"pushq":"popq";sz=0;count=1;x64_encode_register(&o[0], i->opcode==X64_OP_PUSH?i->registers[1]:i->registers[0],8);break;
		case X64_OP_CQO: mn=sz==8?"cqto":"cltd";sz=0;count=0;break;
		case X64_OP_MFENCE: case X64_OP_UD2: mn=i->opcode==X64_OP_MFENCE?"mfence":"ud2";sz=0;count=0;break;
		case X64_OP_BSF: case X64_OP_BSR: mn=i->opcode==X64_OP_BSF?"bsf":"bsr";break;
		case X64_OP_MULW: case X64_OP_SMULW: case X64_OP_DIV: case X64_OP_IDIV:
			mn=i->opcode==X64_OP_MULW?"mul":i->opcode==X64_OP_SMULW?"imul":i->opcode==X64_OP_DIV?"div":"idiv";count=1;break;
		case X64_OP_FADD: case X64_OP_FSUB: case X64_OP_FMUL: case X64_OP_FDIV: {
			static const char *const op[]={"add","sub","mul","div"};
			snprintf(name,sizeof name,"%s%s",op[i->opcode-X64_OP_FADD],sz==4?"ss":"sd");mn=name;sz=0;x64_encode_register(&o[0], i->registers[2],8);break;
		}
		case X64_OP_FXOR: mn="xorps";sz=0;x64_encode_register(&o[0], i->registers[2],8);break;
		default: return -1;
		}
		if (op >= 0) {
			if (as_x64_op(R->assembler, (enum X64Op)op, sz ? sz : o[1].size, o)) return -1;
		} else if(x64_encode_instruction(R,mn,sz,o,count)) return -1;
	}
	return 0;
}
