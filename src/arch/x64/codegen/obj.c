/* x86 records -> structured assembler operands. Encoding, relocations and
 * branch relaxation belong to the existing assembler, shared with -S input. */
#include "x64impl.h"
#include "../as/operand.h"

VPRIV bool x64_obj_record(codegen_context *e, const record *i, record *out)
{
	switch (i->opcode) {
	case X_LABEL: case X_MOV: case X_MOVI: case X_LD: case X_LDX:
	case X_ST: case X_STX: case X_LEA: case X_LEASCALE:
	case X_ADD: case X_SUB: case X_AND: case X_OR: case X_XOR:
	case X_IMUL: case X_ADC: case X_SBB:
	case X_ADDI: case X_SUBI: case X_ANDI: case X_ORI: case X_XORI:
	case X_SHLI: case X_SHRI: case X_SARI: case X_SHL: case X_SHR: case X_SAR:
	case X_IMULI: case X_NEG: case X_NOT: case X_CMP: case X_CMPI:
	case X_TEST: case X_CMOV: case X_SETCC: case X_JMP: case X_JCC:
	case X_CALL: case X_PUSH: case X_POP: case X_CQO:
	case X_LEAL: case X_MFENCE: case X_UD2: case X_BSF: case X_BSR:
	case X_MULW: case X_SMULW: case X_DIV: case X_IDIV: case X_EXT:
	case X_FADD: case X_FSUB: case X_FMUL: case X_FDIV: case X_FXOR:
		break;
	case X_CALLR: case X_JMPR:
		if (e->unit->options->xcf & 4) return false;
		break;
	default: return false;
	}
	*out = *i;
	if (i->opcode == X_LD || i->opcode == X_LDX || i->opcode == X_ST || i->opcode == X_STX || i->opcode == X_LEA) {
		if (i->registers[1] == XFRAME) {
			int64_t off = i->offset + e->x64_context->framesz - 8 - e->x64_context->spdelta;
			/* Preserve the textual path for a displacement wider than rec.off. */
			if (off < INT32_MIN || off > INT32_MAX) return false;
			out->registers[1] = RSP; out->offset = (int32_t)off;
		} else if (i->registers[1] == RBP && i->offset < 0 && e->walk_state->frame_alignment > 16)
			out->registers[1] = R12;
	}
	return true;
}

static void xo_reg(XOp *operand, int r, int size)
{
	*operand = (XOp){ .kind = XK_REG, .cls = IS_XMM(r) ? XR_XMM : XR_GPR,
		.reg = (uint8_t)(IS_XMM(r) ? r - XMM0 : r), .size = (uint8_t)(IS_XMM(r) ? 16 : size),
		.needrex = !IS_XMM(r) && size == 1 && r >= 4 };
}

static XOp xo_imm(int64_t v)
{
	return (XOp){ .kind = XK_IMM, .v = { .c = v }, .hasv = 1 };
}

static uint32_t xo_label(codegen_commit_context *R, uint32_t fid, uint32_t label)
{
	R->name_buffer.n = 0;
	x64_lbl(&R->name_buffer, fid, label);
	return as_symbol_by_name(R->assembler, R->name_buffer.p, R->name_buffer.n);
}

static XOp xo_mem(codegen_commit_context *R, const record *i)
{
	XOp m = { .kind = XK_MEM, .base = (int8_t)(i->registers[1] == RIP ? 16 : i->registers[1]), .index = -1, .scale = 1,
		.v = { .c = i->offset }, .hasv = 1 };
	if (i->registers[1] == RIP)
		m.v = as_symbol_value(R->assembler, codegen_commit_symbol_find(R, i->reference), i->offset);
	if (i->opcode == X_LDX || i->opcode == X_STX) {
		m.index = (int8_t)i->registers[3]; m.scale = (uint8_t)(1u << i->shift);
	}
	return m;
}

static int xo_emit(codegen_commit_context *R, const char *name, int sz, XOp *o, int n)
{
	char mn[32];
	size_t k = strlen(name);
	memcpy(mn, name, k);
	if (sz) mn[k++] = sz == 8 ? 'q' : sz == 4 ? 'l' : sz == 2 ? 'w' : 'b';
	mn[k] = 0;
	return as_x64_insn(R->assembler, mn, o, n);
}

VPRIV int x64_replay(codegen_commit_context *R, const char *p, size_t n)
{
	uint32_t fid = *(const uint32_t *)p;
	const record *in = (const record *)(p + 8);
	static const char *const ccname[] = { "o", "no", "b", "ae", "e", "ne", "be", "a", "s", "ns", "p", "np", "l", "ge", "le", "g" };
	static const enum X64Op integer_op[] = {
		[X_ADD]=X64O_ADD, [X_ADDI]=X64O_ADD, [X_SUB]=X64O_SUB, [X_SUBI]=X64O_SUB,
		[X_AND]=X64O_AND, [X_ANDI]=X64O_AND, [X_OR]=X64O_OR, [X_ORI]=X64O_OR,
		[X_XOR]=X64O_XOR, [X_XORI]=X64O_XOR, [X_ADC]=X64O_ADC, [X_SBB]=X64O_SBB,
	};
	static const char *const shift[] = {
		[X_SHLI]="shl", [X_SHRI]="shr", [X_SARI]="sar", [X_SHL]="shl", [X_SHR]="shr", [X_SAR]="sar",
	};
	for (size_t k = 0; k < (n - 8) / sizeof(record); k++) {
		const record *i = &in[k];
		int sz = i->size, count = 2, op = -1;
		const char *mn = NULL;
		char name[24];
		XOp o[3];
		if (i->opcode == X_LABEL) {
			if (as_label_define_symbol(R->assembler, xo_label(R, fid, i->label))) return -1;
			continue;
		}
		xo_reg(&o[0], i->registers[1], sz);
		xo_reg(&o[1], i->registers[0], sz);
		switch (i->opcode) {
		case X_MOV:
			mn = "mov";
			if (!IS_XMM(i->registers[0]) && !IS_XMM(i->registers[1])) op=X64O_MOV;
			if (IS_XMM(i->registers[0]) && IS_XMM(i->registers[1])) { mn="movaps"; sz=0; }
			else if (IS_XMM(i->registers[0]) || IS_XMM(i->registers[1])) { mn=sz==4?"movd":"movq"; sz=0; }
			break;
		case X_MOVI:
			mn="mov"; op=X64O_MOV;
			if (sz==4) o[0]=xo_imm((int32_t)i->immediate);
			else if (i->immediate >= INT32_MIN && i->immediate <= INT32_MAX) { sz=8; o[0]=xo_imm(i->immediate); }
			else if ((uint64_t)i->immediate <= UINT32_MAX) { sz=4; o[0]=xo_imm(i->immediate); }
			else { op=-1; mn="movabs"; sz=8; o[0]=xo_imm(i->immediate); }
			xo_reg(&o[1], i->registers[0],sz); break;
		case X_LD: case X_LDX: case X_EXT:
			if (i->opcode != X_EXT) o[0] = xo_mem(R, i);
			if (IS_XMM(i->registers[0])) mn=sz==4?"movss":sz==8?"movsd":"movups";
			else {
				static const char *const sx[] = { [1]="movsbq",[2]="movswq",[4]="movslq",[8]="movq" };
				static const char *const zx[] = { [1]="movzbl",[2]="movzwl",[4]="movl",[8]="movq" };
				mn=i->condition?sx[sz]:zx[sz]; xo_reg(&o[1], i->registers[0],i->condition||sz==8?8:4);
				if (sz==8 || (sz==4 && !i->condition)) op=X64O_MOV;
			}
			sz=0; break;
		case X_ST: case X_STX:
			mn="mov"; xo_reg(&o[0], i->registers[2],sz); o[1]=xo_mem(R,i);
			if (!IS_XMM(i->registers[2])) op=X64O_MOV;
			if (IS_XMM(i->registers[2])) { mn=sz==4?"movss":sz==8?"movsd":"movups"; sz=0; } break;
		case X_LEA: mn="lea"; o[0]=xo_mem(R,i); break;
		case X_LEASCALE:
			mn="lea"; o[0]=(XOp){ .kind=XK_MEM,.base=(int8_t)i->registers[1],.index=(int8_t)i->registers[2],.scale=(uint8_t)(1u<<i->shift) }; break;
		case X_ADD: case X_SUB: case X_AND: case X_OR: case X_XOR: case X_ADC: case X_SBB:
			op = integer_op[i->opcode]; xo_reg(&o[0], i->registers[2], sz); break;
		case X_ADDI: case X_SUBI: case X_ANDI: case X_ORI: case X_XORI:
			op = integer_op[i->opcode]; o[0] = xo_imm(i->immediate); break;
		case X_IMUL: mn = "imul"; xo_reg(&o[0], i->registers[2], sz); break;
		case X_SHLI: case X_SHRI: case X_SARI: mn = shift[i->opcode]; o[0] = xo_imm(i->immediate); break;
		case X_SHL: case X_SHR: case X_SAR: mn = shift[i->opcode]; xo_reg(&o[0], RCX, 1); break;
		case X_IMULI: mn="imul"; o[2]=o[1]; o[1]=o[0]; o[0]=xo_imm(i->immediate); count=3; break;
		case X_NEG: case X_NOT: mn=i->opcode==X_NEG?"neg":"not"; o[0]=o[1]; count=1; break;
		case X_CMP: op=X64O_CMP; mn="cmp"; o[1]=o[0]; xo_reg(&o[0], i->registers[2],sz); break;
		case X_CMPI: op=X64O_CMP; mn="cmp"; o[1]=o[0]; o[0]=xo_imm(i->immediate); break;
		case X_TEST: mn="test"; o[1]=o[0]; break;
		case X_CMOV: snprintf(name,sizeof name,"cmov%s",ccname[i->condition]); mn=name; xo_reg(&o[0], i->registers[2],sz); break;
		case X_SETCC:
			snprintf(name,sizeof name,"set%s",ccname[i->condition]); xo_reg(&o[0], i->registers[0],1);
			if (xo_emit(R,name,0,o,1)) return -1;
			mn="movzbl"; sz=0; xo_reg(&o[1], i->registers[0],4); break;
		case X_JMP: case X_JCC: case X_LEAL:
			o[0]=(XOp){ .kind=XK_EXPR,.v=as_symbol_value(R->assembler,xo_label(R,fid,i->label),0),.hasv=1 };
			if (i->opcode==X_LEAL) { mn="lea";sz=8;o[0].kind=XK_MEM;o[0].base=16;o[0].index=-1;o[0].scale=1; }
			else { snprintf(name,sizeof name,"j%s",i->opcode==X_JMP?"mp":ccname[i->condition]);mn=name;sz=0;count=1; } break;
		case X_CALL:
			mn="callq";sz=0;count=1;o[0]=(XOp){ .kind=XK_EXPR,.v=as_symbol_value(R->assembler,codegen_commit_symbol_find(R,i->reference),0),.hasv=1 };
			if(R->compile_context->o->pic&&(CODEGEN_REFERENCE_KIND(i->reference)==CODEGEN_REFERENCE_NAME||codegen_symbol_needs_got(R->compile_context,i->reference)))o[0].mod=XM_PLT;
			break;
		case X_CALLR: case X_JMPR: mn=i->opcode==X_CALLR?"callq":"jmpq";sz=0;count=1;xo_reg(&o[0], i->registers[1],8);o[0].star=1;break;
		case X_PUSH: case X_POP: mn=i->opcode==X_PUSH?"pushq":"popq";sz=0;count=1;xo_reg(&o[0], i->opcode==X_PUSH?i->registers[1]:i->registers[0],8);break;
		case X_CQO: mn=sz==8?"cqto":"cltd";sz=0;count=0;break;
		case X_MFENCE: case X_UD2: mn=i->opcode==X_MFENCE?"mfence":"ud2";sz=0;count=0;break;
		case X_BSF: case X_BSR: mn=i->opcode==X_BSF?"bsf":"bsr";break;
		case X_MULW: case X_SMULW: case X_DIV: case X_IDIV:
			mn=i->opcode==X_MULW?"mul":i->opcode==X_SMULW?"imul":i->opcode==X_DIV?"div":"idiv";count=1;break;
		case X_FADD: case X_FSUB: case X_FMUL: case X_FDIV: {
			static const char *const op[]={"add","sub","mul","div"};
			snprintf(name,sizeof name,"%s%s",op[i->opcode-X_FADD],sz==4?"ss":"sd");mn=name;sz=0;xo_reg(&o[0], i->registers[2],8);break;
		}
		case X_FXOR: mn="xorps";sz=0;xo_reg(&o[0], i->registers[2],8);break;
		default: return -1;
		}
		if (op >= 0) {
			if (as_x64_op(R->assembler, (enum X64Op)op, sz ? sz : o[1].size, o)) return -1;
		} else if(xo_emit(R,mn,sz,o,count)) return -1;
	}
	return 0;
}
