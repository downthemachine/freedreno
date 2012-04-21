/*
 * Copyright (c) 2012 Rob Clark <robdclark@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>

#include "redump.h"


/* ************************************************************************* */
/* based on kernel recovery dump code: */
#include "a2xx_reg.h"
#include "adreno_pm4types.h"

typedef enum {
	true = 1, false = 0,
} bool;

static void dump_commands(uint32_t *dwords, uint32_t sizedwords, int level);

struct buffer {
	void *hostptr;
	unsigned int gpuaddr, len;
};

static struct buffer buffers[32];
static int nbuffers;

static int buffer_contains_gpuaddr(struct buffer *buf, uint32_t gpuaddr, uint32_t len)
{
	return (buf->gpuaddr <= gpuaddr) && (gpuaddr < (buf->gpuaddr + buf->len));
}

static int buffer_contains_hostptr(struct buffer *buf, void *hostptr)
{
	return (buf->hostptr <= hostptr) && (hostptr < (buf->hostptr + buf->len));
}

#define GET_PM4_TYPE3_OPCODE(x) ((*(x) >> 8) & 0xFF)
#define GET_PM4_TYPE0_REGIDX(x) ((*(x)) & 0x7FFF)

static uint32_t gpuaddr(void *hostptr)
{
	int i;
	for (i = 0; i < nbuffers; i++)
		if (buffer_contains_hostptr(&buffers[i], hostptr))
			return buffers[i].gpuaddr + (hostptr - buffers[i].hostptr);
	return 0;
}

static void *hostptr(uint32_t gpuaddr)
{
	int i;
	for (i = 0; i < nbuffers; i++)
		if (buffer_contains_gpuaddr(&buffers[i], gpuaddr, 0))
			return buffers[i].hostptr + (gpuaddr - buffers[i].gpuaddr);
	return 0;
}

static const char *levels[] = {
		"\t",
		"\t\t",
		"\t\t\t",
		"\t\t\t\t",
		"\t\t\t\t\t",
		"\t\t\t\t\t\t",
		"\t\t\t\t\t\t\t",
		"\t\t\t\t\t\t\t\t",
		"\t\t\t\t\t\t\t\t\t",
		"x",
		"x",
		"x",
		"x",
		"x",
		"x",
};

#define INVALID_RB_CMD 0xaaaaaaaa

/* CP timestamp register */
#define	REG_CP_TIMESTAMP		 REG_SCRATCH_REG0

#define NAME(x) [REG_ ## x] = #x
static const const char *type0_regname[0x7fff] = {
		NAME(CP_CSQ_IB1_STAT),
		NAME(CP_CSQ_IB2_STAT),
		NAME(CP_CSQ_RB_STAT),
		NAME(CP_DEBUG),
		NAME(CP_IB1_BASE),
		NAME(CP_IB1_BUFSZ),
		NAME(CP_IB2_BASE),
		NAME(CP_IB2_BUFSZ),
		NAME(CP_INT_ACK),
		NAME(CP_INT_CNTL),
		NAME(CP_INT_STATUS),
		NAME(CP_ME_CNTL),
		NAME(CP_ME_RAM_DATA),
		NAME(CP_ME_RAM_WADDR),
		NAME(CP_ME_RAM_RADDR),
		NAME(CP_ME_STATUS),
		NAME(CP_PFP_UCODE_ADDR),
		NAME(CP_PFP_UCODE_DATA),
		NAME(CP_QUEUE_THRESHOLDS),
		NAME(CP_RB_BASE),
		NAME(CP_RB_CNTL),
		NAME(CP_RB_RPTR),
		NAME(CP_RB_RPTR_ADDR),
		NAME(CP_RB_RPTR_WR),
		NAME(CP_RB_WPTR),
		NAME(CP_RB_WPTR_BASE),
		NAME(CP_RB_WPTR_DELAY),
		NAME(CP_STAT),
		NAME(CP_STATE_DEBUG_DATA),
		NAME(CP_STATE_DEBUG_INDEX),
		NAME(CP_ST_BASE),
		NAME(CP_ST_BUFSZ),

		NAME(CP_PERFMON_CNTL),
		NAME(CP_PERFCOUNTER_SELECT),
		NAME(CP_PERFCOUNTER_LO),
		NAME(CP_PERFCOUNTER_HI),

		NAME(RBBM_PERFCOUNTER1_SELECT),
		NAME(RBBM_PERFCOUNTER1_HI),
		NAME(RBBM_PERFCOUNTER1_LO),

		NAME(MASTER_INT_SIGNAL),

		NAME(PA_CL_VPORT_XSCALE),
		NAME(PA_CL_VPORT_ZOFFSET),
		NAME(PA_CL_VPORT_ZSCALE),
		NAME(PA_CL_CLIP_CNTL),
		NAME(PA_CL_VTE_CNTL),
		NAME(PA_SC_AA_MASK),
		NAME(PA_SC_LINE_CNTL),
		NAME(PA_SC_SCREEN_SCISSOR_BR),
		NAME(PA_SC_SCREEN_SCISSOR_TL),
		NAME(PA_SC_VIZ_QUERY),
		NAME(PA_SC_VIZ_QUERY_STATUS),
		NAME(PA_SC_WINDOW_OFFSET),
		NAME(PA_SC_WINDOW_SCISSOR_BR),
		NAME(PA_SC_WINDOW_SCISSOR_TL),
		NAME(PA_SU_FACE_DATA),
		NAME(PA_SU_POINT_SIZE),
		NAME(PA_SU_LINE_CNTL),
		NAME(PA_SU_POLY_OFFSET_BACK_OFFSET),
		NAME(PA_SU_POLY_OFFSET_FRONT_SCALE),
		NAME(PA_SU_SC_MODE_CNTL),

		NAME(PC_INDEX_OFFSET),

		NAME(RBBM_CNTL),
		NAME(RBBM_INT_ACK),
		NAME(RBBM_INT_CNTL),
		NAME(RBBM_INT_STATUS),
		NAME(RBBM_PATCH_RELEASE),
		NAME(RBBM_PERIPHID1),
		NAME(RBBM_PERIPHID2),
		NAME(RBBM_DEBUG),
		NAME(RBBM_DEBUG_OUT),
		NAME(RBBM_DEBUG_CNTL),
		NAME(RBBM_PM_OVERRIDE1),
		NAME(RBBM_PM_OVERRIDE2),
		NAME(RBBM_READ_ERROR),
		NAME(RBBM_SOFT_RESET),
		NAME(RBBM_STATUS),

		NAME(RB_COLORCONTROL),
		NAME(RB_COLOR_DEST_MASK),
		NAME(RB_COLOR_MASK),
		NAME(RB_COPY_CONTROL),
		NAME(RB_DEPTHCONTROL),
		NAME(RB_EDRAM_INFO),
		NAME(RB_MODECONTROL),
		NAME(RB_SURFACE_INFO),
		NAME(RB_SAMPLE_POS),

		NAME(SCRATCH_ADDR),
		NAME(SCRATCH_REG0),
		NAME(SCRATCH_REG2),
		NAME(SCRATCH_UMSK),

		NAME(SQ_CF_BOOLEANS),
		NAME(SQ_CF_LOOP),
		NAME(SQ_GPR_MANAGEMENT),
		NAME(SQ_FLOW_CONTROL),
		NAME(SQ_INST_STORE_MANAGMENT),
		NAME(SQ_INT_ACK),
		NAME(SQ_INT_CNTL),
		NAME(SQ_INT_STATUS),
		NAME(SQ_PROGRAM_CNTL),
		NAME(SQ_PS_PROGRAM),
		NAME(SQ_VS_PROGRAM),
		NAME(SQ_WRAPPING_0),
		NAME(SQ_WRAPPING_1),

		NAME(VGT_ENHANCE),
		NAME(VGT_INDX_OFFSET),
		NAME(VGT_MAX_VTX_INDX),
		NAME(VGT_MIN_VTX_INDX),

		NAME(TP0_CHICKEN),
		NAME(TC_CNTL_STATUS),
		NAME(PA_SC_AA_CONFIG),
		NAME(VGT_VERTEX_REUSE_BLOCK_CNTL),
		NAME(SQ_INTERPOLATOR_CNTL),
		NAME(RB_DEPTH_INFO),
		NAME(COHER_DEST_BASE_0),
		NAME(RB_FOG_COLOR),
		NAME(RB_STENCILREFMASK_BF),
		NAME(PA_SC_LINE_STIPPLE),
		NAME(SQ_PS_CONST),
		NAME(RB_DEPTH_CLEAR),
		NAME(RB_SAMPLE_COUNT_CTL),
		NAME(SQ_CONSTANT_0),
		NAME(SQ_FETCH_0),

		NAME(COHER_BASE_PM4),
		NAME(COHER_STATUS_PM4),
		NAME(COHER_SIZE_PM4),

		/*registers added in adreno220*/
		NAME(A220_PC_INDX_OFFSET),
		NAME(A220_PC_VERTEX_REUSE_BLOCK_CNTL),
		NAME(A220_PC_MAX_VTX_INDX),
		NAME(A220_RB_LRZ_VSC_CONTROL),
		NAME(A220_GRAS_CONTROL),
		NAME(A220_VSC_BIN_SIZE),
		NAME(A220_VSC_PIPE_DATA_LENGTH_7),

		/*registers added in adreno225*/
		NAME(A225_RB_COLOR_INFO3),
		NAME(A225_PC_MULTI_PRIM_IB_RESET_INDX),
		NAME(A225_GRAS_UCP0X),
		NAME(A225_GRAS_UCP5W),
		NAME(A225_GRAS_UCP_ENABLED),

		/* Debug registers used by snapshot */
		NAME(PA_SU_DEBUG_CNTL),
		NAME(PA_SU_DEBUG_DATA),
		NAME(RB_DEBUG_CNTL),
		NAME(RB_DEBUG_DATA),
		NAME(PC_DEBUG_CNTL),
		NAME(PC_DEBUG_DATA),
		NAME(GRAS_DEBUG_CNTL),
		NAME(GRAS_DEBUG_DATA),
		NAME(SQ_DEBUG_MISC),
		NAME(SQ_DEBUG_INPUT_FSM),
		NAME(SQ_DEBUG_CONST_MGR_FSM),
		NAME(SQ_DEBUG_EXP_ALLOC),
		NAME(SQ_DEBUG_FSM_ALU_0),
		NAME(SQ_DEBUG_FSM_ALU_1),
		NAME(SQ_DEBUG_PTR_BUFF),
		NAME(SQ_DEBUG_GPR_VTX),
		NAME(SQ_DEBUG_GPR_PIX),
		NAME(SQ_DEBUG_TB_STATUS_SEL),
		NAME(SQ_DEBUG_VTX_TB_0),
		NAME(SQ_DEBUG_VTX_TB_1),
		NAME(SQ_DEBUG_VTX_TB_STATE_MEM),
		NAME(SQ_DEBUG_TP_FSM),
		NAME(SQ_DEBUG_VTX_TB_STATUS_REG),
		NAME(SQ_DEBUG_PIX_TB_0),
		NAME(SQ_DEBUG_PIX_TB_STATUS_REG_0),
		NAME(SQ_DEBUG_PIX_TB_STATUS_REG_1),
		NAME(SQ_DEBUG_PIX_TB_STATUS_REG_2),
		NAME(SQ_DEBUG_PIX_TB_STATUS_REG_3),
		NAME(SQ_DEBUG_PIX_TB_STATE_MEM),
		NAME(SQ_DEBUG_MISC_0),
		NAME(SQ_DEBUG_MISC_1),
};
#undef NAME

#define NAME(x)   [CP_ ## x] = #x
static const char *type3_opname[0xff] = {
		NAME(ME_INIT),
		NAME(NOP),
		NAME(INDIRECT_BUFFER),
		NAME(INDIRECT_BUFFER_PFD),
		NAME(WAIT_FOR_IDLE),
		NAME(WAIT_REG_MEM),
		NAME(WAIT_REG_EQ),
		NAME(WAT_REG_GTE),
		NAME(WAIT_UNTIL_READ),
		NAME(WAIT_IB_PFD_COMPLETE),
		NAME(REG_RMW),
		NAME(REG_TO_MEM),
		NAME(MEM_WRITE),
		NAME(MEM_WRITE_CNTR),
		NAME(COND_EXEC),
		NAME(COND_WRITE),
		NAME(EVENT_WRITE),
		NAME(EVENT_WRITE_SHD),
		NAME(EVENT_WRITE_CFL),
		NAME(EVENT_WRITE_ZPD),
		NAME(DRAW_INDX),
		NAME(DRAW_INDX_2),
		NAME(DRAW_INDX_BIN),
		NAME(DRAW_INDX_2_BIN),
		NAME(VIZ_QUERY),
		NAME(SET_STATE),
		NAME(SET_CONSTANT),
		NAME(IM_LOAD),
		NAME(IM_LOAD_IMMEDIATE),
		NAME(LOAD_CONSTANT_CONTEXT),
		NAME(INVALIDATE_STATE),
		NAME(SET_SHADER_BASES),
		NAME(SET_BIN_MASK),
		NAME(SET_BIN_SELECT),
		NAME(CONTEXT_UPDATE),
		NAME(INTERRUPT),
		NAME(IM_STORE),
		NAME(SET_PROTECTED_MODE),

		/* for a20x */
		//NAME(SET_BIN_BASE_OFFSET),

		/* for a22x */
		NAME(SET_DRAW_INIT_FLAGS),

		/* note also some different values for a3xx it seems.. probably
		 * need to have different tables for different cores, but we
		 * can worry about that later
		 */
};
#undef NAME

#define NAME(x)   [x] = #x
static const char *event_name[] = {
		NAME(VS_DEALLOC),
		NAME(PS_DEALLOC),
		NAME(VS_DONE_TS),
		NAME(PS_DONE_TS),
		NAME(CACHE_FLUSH_TS),
		NAME(CONTEXT_DONE),
		NAME(CACHE_FLUSH),
		NAME(VIZQUERY_START),
		NAME(VIZQUERY_END),
		NAME(SC_WAIT_WC),
		NAME(RST_PIX_CNT),
		NAME(RST_VTX_CNT),
		NAME(TILE_FLUSH),
		NAME(CACHE_FLUSH_AND_INV_TS_EVENT),
		NAME(ZPASS_DONE),
		NAME(CACHE_FLUSH_AND_INV_EVENT),
		NAME(PERFCOUNTER_START),
		NAME(PERFCOUNTER_STOP),
		NAME(VS_FETCH_DONE),
		NAME(FACENESS_FLUSH),
};
#undef NAME

#define NAME(x)   [x] = #x
static const char *format_name[] = {
		NAME(COLORX_4_4_4_4),
		NAME(COLORX_1_5_5_5),
		NAME(COLORX_5_6_5),
		NAME(COLORX_8),
		NAME(COLORX_8_8),
		NAME(COLORX_8_8_8_8),
		NAME(COLORX_S8_8_8_8),
		NAME(COLORX_16_FLOAT),
		NAME(COLORX_16_16_FLOAT),
		NAME(COLORX_16_16_16_16_FLOAT),
		NAME(COLORX_32_FLOAT),
		NAME(COLORX_32_32_FLOAT),
		NAME(COLORX_32_32_32_32_FLOAT),
		NAME(COLORX_2_3_3),
		NAME(COLORX_8_8_8),
};
#undef NAME


static void dump_hex(uint32_t *dwords, uint32_t sizedwords, int level)
{
	int i;
	for (i = 0; i < sizedwords; i++) {
		if ((i % 8) == 0)
			printf("%08x:%s", gpuaddr(dwords), levels[level]);
		else
			printf(" ");
		printf("%08x", *(dwords++));
		if ((i % 8) == 7)
			printf("\n");
	}
	if (i % 8)
		printf("\n");
}

static void dump_im_loadi(uint32_t *dwords, uint32_t sizedwords, int level)
{
	uint32_t start = dwords[2] >> 16;
	uint32_t size  = dwords[2] & 0xffff;
	const char *type;
	switch (dwords[1]) {
	case 0:   type = "vertex";   break;
	case 1:   type = "fragment"; break;
	default: type = "<unknown>"; break;
	}
	printf("%s%s shader, start=%04x, size=%04x\n", levels[level], type, start, size);
}

/* I believe the surface format is low bits:
#define RB_COLOR_INFO__COLOR_FORMAT_MASK                   0x0000000fL
comments in sys2gmem_tex_const indicate that address is [31:12], but
looks like at least some of the bits above the format have different meaning..
*/
static void parse_dword_addr(uint32_t dword, uint32_t *gpuaddr, uint32_t *flags)
{
	*gpuaddr = dword & ~0xfff;
	*flags   = dword & 0xfff;
}

static void dump_tex_const(uint32_t *dwords, uint32_t sizedwords, uint32_t val, int level)
{
	uint32_t w, h, p;
	uint32_t gpuaddr, flags, mip_gpuaddr, mip_flags;

	/* see sys2gmem_tex_const[] in adreno_a2xxx.c */

	/* Texture, FormatXYZW=Unsigned, ClampXYZ=Wrap/Repeat,
	 * RFMode=ZeroClamp-1, Dim=1:2d, pitch
	 */
	p = (dwords[0] >> 22) << 5;

	/* Format=6:8888_WZYX, EndianSwap=0:None, ReqSize=0:256bit, DimHi=0,
	 * NearestClamp=1:OGL Mode
	 */
	parse_dword_addr(dwords[1], &gpuaddr, &flags);

	/* Width, Height, EndianSwap=0:None */
	w = (dwords[2] & 0x1fff) + 1;
	h = ((dwords[2] >> 13) & 0x1fff) + 1;

	/* NumFormat=0:RF, DstSelXYZW=XYZW, ExpAdj=0, MagFilt=MinFilt=0:Point,
	 * Mip=2:BaseMap
	 */
	// XXX

	/* VolMag=VolMin=0:Point, MinMipLvl=0, MaxMipLvl=1, LodBiasH=V=0,
	 * Dim3d=0
	 */
	// XXX

	/* BorderColor=0:ABGRBlack, ForceBC=0:diable, TriJuice=0, Aniso=0,
	 * Dim=1:2d, MipPacking=0
	 */
	parse_dword_addr(dwords[5], &mip_gpuaddr, &mip_flags);

	printf("%sset texture const %04x\n", levels[level], val);
	printf("%saddr=%08x (flags=%03x), size=%dx%d, pitch=%d, format=%s\n",
			levels[level+1], gpuaddr, flags, w, h, p,
			format_name[flags & 0xf]);
	printf("%smipaddr=%08x (flags=%03x)\n", levels[level+1],
			mip_gpuaddr, mip_flags);
}

static void dump_shader_const(uint32_t *dwords, uint32_t sizedwords, uint32_t val, int level)
{
	int i;
	printf("%sset shader const %04x\n", levels[level], val);
	for (i = 0; i < sizedwords; ) {
		uint32_t gpuaddr, flags;
		parse_dword_addr(dwords[i++], &gpuaddr, &flags);
		void *addr = hostptr(gpuaddr);
		if (addr) {
			uint32_t size = dwords[i++];
			printf("%saddr=%08x, size=%d, format=%s\n", levels[level+1],
					gpuaddr, size, format_name[flags & 0xf]);
			// TODO maybe dump these as bytes instead of dwords?
			size = (size + 3) / 4; // for now convert to dwords
			dump_hex(addr, size, level + 1);
		}
	}
}

static void dump_set_const(uint32_t *dwords, uint32_t sizedwords, int level)
{
	uint32_t val = dwords[1] & 0xffff;
	switch(dwords[1] >> 16) {
	case 0x1:
		dwords += 2;
		sizedwords -= 2;
		if (val == 0x000) {
			dump_tex_const(dwords, sizedwords, val, level);
		} else {
			dump_shader_const(dwords, sizedwords, val, level);
		}
		break;
	case 0x2:
		printf("%sset bool const %04x\n", levels[level], val);
		break;
	case 0x3:
		printf("%sset loop const %04x\n", levels[level], val);
		break;
	case 0x4:
		printf("%sset register %s\n", levels[level],
				type0_regname[val + 0x2000]);
		break;
	}
}

static void dump_event_write(uint32_t *dwords, uint32_t sizedwords, int level)
{
	printf("%sevent %s\n", levels[level], event_name[dwords[1]]);
}

static void (*type3_dump_op[0xff])(uint32_t *, uint32_t, int) = {
		[CP_IM_LOAD_IMMEDIATE] = dump_im_loadi,
		[CP_SET_CONSTANT] = dump_set_const,
		[CP_EVENT_WRITE] = dump_event_write,
};

static void dump_type3(uint32_t *dwords, uint32_t sizedwords, int level)
{
	switch (GET_PM4_TYPE3_OPCODE(dwords)) {
	case CP_INDIRECT_BUFFER_PFD:
	case CP_INDIRECT_BUFFER: {
		/* traverse indirect buffers */
		int i;
		uint32_t ibaddr = dwords[1];
		uint32_t ibsize = dwords[2];
		uint32_t *ptr = NULL;

		printf("%sibaddr:%08x\n", levels[level], ibaddr);
		printf("%sibsize:%08x\n", levels[level], ibsize);

		// XXX stripped out checking for loops.. but maybe we need that..
		// see kgsl_cffdump_handle_type3()

		/* map gpuaddr back to hostptr: */
		for (i = 0; i < nbuffers; i++) {
			if (buffer_contains_gpuaddr(&buffers[i], ibaddr, ibsize)) {
				ptr = buffers[i].hostptr + (ibaddr - buffers[i].gpuaddr);
				break;
			}
		}

		if (ptr) {
			dump_commands(ptr, ibsize, level);
		} else {
			fprintf(stderr, "could not find: %08x (%d)\n", ibaddr, ibsize);
		}

		break;
	}
	}

}

static void dump_commands(uint32_t *dwords, uint32_t sizedwords, int level)
{
	int dwords_left = sizedwords;
	uint32_t count = 0; /* dword count including packet header */
	uint32_t val;

	while (dwords_left > 0) {
		switch (*dwords >> 30) {
		case 0x0: /* type-0 */
			count = (*dwords >> 16)+2;
			val = GET_PM4_TYPE0_REGIDX(dwords);
			printf("\t%swrite: %s (%04x) (%d dwords)\n", levels[level],
					type0_regname[val], val, count);
			dump_hex(dwords, count, level+1);
			break;
		case 0x1: /* type-1 */
			count = 2;
			dump_hex(dwords, count, level+1);
			break;
		case 0x3: /* type-3 */
			count = ((*dwords >> 16) & 0x3fff) + 2;
			val = GET_PM4_TYPE3_OPCODE(dwords);
			printf("\t%sopcode: %s (%02x) (%d dwords)\n", levels[level],
					type3_opname[val], val, count);
			if (type3_dump_op[val])
				type3_dump_op[val](dwords, count, level+1);
			dump_hex(dwords, count, level+1);
			dump_type3(dwords, count, level+1);
			break;
		default:
			fprintf(stderr, "bad type!\n");
			return;
		}

		dwords += count;
		dwords_left -= count;

		//printf("*** dwords_left=%d, count=%d\n", dwords_left, count);
	}

	if (dwords_left < 0)
		printf("**** this ain't right!! dwords_left=%d\n", dwords_left);
}

int main(int argc, char **argv)
{
	enum rd_sect_type type = RD_NONE;
	void *buf = NULL;
	int fd, sz, i;

	if (argc != 2)
		fprintf(stderr, "usage: %s testlog.rd\n", argv[0]);

	fd = open(argv[1], O_RDONLY);
	if (fd < 0)
		fprintf(stderr, "could not open: %s\n", argv[1]);

	while ((read(fd, &type, sizeof(type)) > 0) && (read(fd, &sz, 4) > 0)) {
		free(buf);

		buf = malloc(sz + 1);
		((char *)buf)[sz] = '\0';
		read(fd, buf, sz);

		switch(type) {
		case RD_TEST:
			printf("test: %s\n", (char *)buf);
			break;
		case RD_CMD:
			printf("cmd: %s\n", (char *)buf);
			break;
		case RD_VERT_SHADER:
			printf("vertex shader:\n%s\n", (char *)buf);
			break;
		case RD_FRAG_SHADER:
			printf("fragment shader:\n%s\n", (char *)buf);
			break;
		case RD_PROGRAM:
			/* TODO */
			break;
		case RD_GPUADDR:
			buffers[nbuffers].gpuaddr = ((uint32_t *)buf)[0];
			buffers[nbuffers].len = ((uint32_t *)buf)[1];
			break;
		case RD_BUFFER_CONTENTS:
			buffers[nbuffers].hostptr = buf;
			nbuffers++;
			buf = NULL;
			break;
		case RD_CMDSTREAM_ADDR:
			printf("cmdstream: %d dwords\n", ((uint32_t *)buf)[1]);
			dump_commands(hostptr(((uint32_t *)buf)[0]),
					((uint32_t *)buf)[1], 0);
			for (i = 0; i < nbuffers; i++) {
				free(buffers[i].hostptr);
				buffers[i].hostptr = NULL;
			}
			nbuffers = 0;
			break;
		}
	}

	return 0;
}

