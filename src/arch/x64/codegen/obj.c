/* x86 records -> structured assembler operands. Encoding, relocations and
 * branch relaxation belong to the existing assembler, shared with -S input. */
#include "x64impl.h"
#include "../as/operand.h"

VPRIV bool x64_obj_record(codegen_context *e, const record *i, record *out)
{
	switch (i->op) {
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
		if (e->E->o->xcf & 4) return false;
		break;
	default: return false;
	}
	*out = *i;
	if (i->op == X_LD || i->op == X_LDX || i->op == X_ST || i->op == X_STX || i->op == X_LEA) {
		if (i->r[1] == XFRAME) {
			int64_t off = i->off + e->x64->framesz - 8 - e->x64->spdelta;
			/* Preserve the textual path for a displacement wider than rec.off. */
			if (off < INT32_MIN || off > INT32_MAX) return false;
			out->r[1] = RSP; out->off = (int32_t)off;
		} else if (i->r[1] == RBP && i->off < 0 && e->wk->falign > 16)
			out->r[1] = R12;
	}
	return true;
}

static XOp xo_reg(int r, int size)
{
	return (XOp){ .kind = XK_REG, .cls = IS_XMM(r) ? XR_XMM : XR_GPR,
		.reg = (uint8_t)(IS_XMM(r) ? r - XMM0 : r), .size = (uint8_t)(IS_XMM(r) ? 16 : size),
		.needrex = !IS_XMM(r) && size == 1 && r >= 4 };
}

static XOp xo_imm(int64_t v)
{
	return (XOp){ .kind = XK_IMM, .v = { .c = v }, .hasv = 1 };
}

static uint32_t xo_label(codegen_commit_context *R, uint32_t fid, uint32_t label)
{
	R->nb.n = 0;
	x64_lbl(&R->nb, fid, label);
	return as_symbol_by_name(R->as, R->nb.p, R->nb.n);
}

static XOp xo_mem(codegen_commit_context *R, const record *i)
{
	XOp m = { .kind = XK_MEM, .base = (int8_t)(i->r[1] == RIP ? 16 : i->r[1]), .index = -1, .scale = 1,
		.v = { .c = i->off }, .hasv = 1 };
	if (i->r[1] == RIP)
		m.v = as_symbol_value(R->as, codegen_commit_symbol_find(R, i->ref), i->off);
	if (i->op == X_LDX || i->op == X_STX) {
		m.index = (int8_t)i->r[3]; m.scale = (uint8_t)(1u << i->sh);
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
	return as_x64_insn(R->as, mn, o, n);
}

VPRIV int x64_replay(codegen_commit_context *R, const char *p, size_t n)
{
	uint32_t fid = *(const uint32_t *)p;
	const record *in = (const record *)(p + 8);
	static const char *const ccname[] = { "o", "no", "b", "ae", "e", "ne", "be", "a", "s", "ns", "p", "np", "l", "ge", "le", "g" };
	static const char *const alu[] = {
		[X_ADD]="add", [X_SUB]="sub", [X_AND]="and", [X_OR]="or", [X_XOR]="xor",
		[X_IMUL]="imul", [X_ADC]="adc", [X_SBB]="sbb",
		[X_ADDI]="add", [X_SUBI]="sub", [X_ANDI]="and", [X_ORI]="or", [X_XORI]="xor",
		[X_SHLI]="shl", [X_SHRI]="shr", [X_SARI]="sar", [X_SHL]="shl", [X_SHR]="shr", [X_SAR]="sar",
	};
	for (size_t k = 0; k < (n - 8) / sizeof(record); k++) {
		const record *i = &in[k];
		int sz = i->sz, count = 2;
		const char *mn = NULL;
		char name[24];
		XOp o[3] = { xo_reg(i->r[1], sz), xo_reg(i->r[0], sz), {0} };
		switch (i->op) {
		case X_LABEL:
			if (as_label_define_symbol(R->as, xo_label(R, fid, i->label))) return -1;
			continue;
		case X_MOV:
			mn = "mov";
			if (IS_XMM(i->r[0]) && IS_XMM(i->r[1])) { mn="movaps"; sz=0; }
			else if (IS_XMM(i->r[0]) || IS_XMM(i->r[1])) { mn=sz==4?"movd":"movq"; sz=0; }
			break;
		case X_MOVI:
			mn="mov";
			if (sz==4) o[0]=xo_imm((int32_t)i->imm);
			else if (i->imm >= INT32_MIN && i->imm <= INT32_MAX) { sz=8; o[0]=xo_imm(i->imm); }
			else if ((uint64_t)i->imm <= UINT32_MAX) { sz=4; o[0]=xo_imm(i->imm); }
			else { mn="movabs"; sz=8; o[0]=xo_imm(i->imm); }
			o[1]=xo_reg(i->r[0],sz); break;
		case X_LD: case X_LDX: case X_EXT:
			o[0]=i->op==X_EXT?xo_reg(i->r[1],sz):xo_mem(R,i);
			if (IS_XMM(i->r[0])) mn=sz==4?"movss":sz==8?"movsd":"movups";
			else {
				static const char *const sx[] = { [1]="movsbq",[2]="movswq",[4]="movslq",[8]="movq" };
				static const char *const zx[] = { [1]="movzbl",[2]="movzwl",[4]="movl",[8]="movq" };
				mn=i->cond?sx[sz]:zx[sz]; o[1]=xo_reg(i->r[0],i->cond||sz==8?8:4);
			}
			sz=0; break;
		case X_ST: case X_STX:
			mn="mov"; o[0]=xo_reg(i->r[2],sz); o[1]=xo_mem(R,i);
			if (IS_XMM(i->r[2])) { mn=sz==4?"movss":sz==8?"movsd":"movups"; sz=0; } break;
		case X_LEA: mn="lea"; o[0]=xo_mem(R,i); break;
		case X_LEASCALE:
			mn="lea"; o[0]=(XOp){ .kind=XK_MEM,.base=(int8_t)i->r[1],.index=(int8_t)i->r[2],.scale=(uint8_t)(1u<<i->sh) }; break;
		case X_ADD: case X_SUB: case X_AND: case X_OR: case X_XOR: case X_IMUL: case X_ADC: case X_SBB:
			mn=alu[i->op]; o[0]=xo_reg(i->r[2],sz); break;
		case X_ADDI: case X_SUBI: case X_ANDI: case X_ORI: case X_XORI: case X_SHLI: case X_SHRI: case X_SARI:
			mn=alu[i->op]; o[0]=xo_imm(i->imm); break;
		case X_SHL: case X_SHR: case X_SAR: mn=alu[i->op]; o[0]=xo_reg(RCX,1); break;
		case X_IMULI: mn="imul"; o[2]=o[1]; o[1]=o[0]; o[0]=xo_imm(i->imm); count=3; break;
		case X_NEG: case X_NOT: mn=i->op==X_NEG?"neg":"not"; o[0]=o[1]; count=1; break;
		case X_CMP: mn="cmp"; o[1]=o[0]; o[0]=xo_reg(i->r[2],sz); break;
		case X_CMPI: mn="cmp"; o[1]=o[0]; o[0]=xo_imm(i->imm); break;
		case X_TEST: mn="test"; o[1]=o[0]; break;
		case X_CMOV: snprintf(name,sizeof name,"cmov%s",ccname[i->cond]); mn=name; o[0]=xo_reg(i->r[2],sz); break;
		case X_SETCC:
			snprintf(name,sizeof name,"set%s",ccname[i->cond]); o[0]=xo_reg(i->r[0],1);
			if (xo_emit(R,name,0,o,1)) return -1;
			mn="movzbl"; sz=0; o[1]=xo_reg(i->r[0],4); break;
		case X_JMP: case X_JCC: case X_LEAL:
			o[0]=(XOp){ .kind=XK_EXPR,.v=as_symbol_value(R->as,xo_label(R,fid,i->label),0),.hasv=1 };
			if (i->op==X_LEAL) { mn="lea";sz=8;o[0].kind=XK_MEM;o[0].base=16;o[0].index=-1;o[0].scale=1; }
			else { snprintf(name,sizeof name,"j%s",i->op==X_JMP?"mp":ccname[i->cond]);mn=name;sz=0;count=1; } break;
		case X_CALL:
			mn="callq";sz=0;count=1;o[0]=(XOp){ .kind=XK_EXPR,.v=as_symbol_value(R->as,codegen_commit_symbol_find(R,i->ref),0),.hasv=1 };
			if(R->c->o->pic&&(CODEGEN_REFERENCE_KIND(i->ref)==CODEGEN_REFERENCE_NAME||codegen_symbol_needs_got(R->c,i->ref)))o[0].mod=XM_PLT;
			break;
		case X_CALLR: case X_JMPR: mn=i->op==X_CALLR?"callq":"jmpq";sz=0;count=1;o[0]=xo_reg(i->r[1],8);o[0].star=1;break;
		case X_PUSH: case X_POP: mn=i->op==X_PUSH?"pushq":"popq";sz=0;count=1;o[0]=xo_reg(i->op==X_PUSH?i->r[1]:i->r[0],8);break;
		case X_CQO: mn=sz==8?"cqto":"cltd";sz=0;count=0;break;
		case X_MFENCE: case X_UD2: mn=i->op==X_MFENCE?"mfence":"ud2";sz=0;count=0;break;
		case X_BSF: case X_BSR: mn=i->op==X_BSF?"bsf":"bsr";break;
		case X_MULW: case X_SMULW: case X_DIV: case X_IDIV:
			mn=i->op==X_MULW?"mul":i->op==X_SMULW?"imul":i->op==X_DIV?"div":"idiv";count=1;break;
		case X_FADD: case X_FSUB: case X_FMUL: case X_FDIV: {
			static const char *const op[]={"add","sub","mul","div"};
			snprintf(name,sizeof name,"%s%s",op[i->op-X_FADD],sz==4?"ss":"sd");mn=name;sz=0;o[0]=xo_reg(i->r[2],8);break;
		}
		case X_FXOR: mn="xorps";sz=0;o[0]=xo_reg(i->r[2],8);break;
		default: return -1;
		}
		if(xo_emit(R,mn,sz,o,count)) return -1;
	}
	return 0;
}
