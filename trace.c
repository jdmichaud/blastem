/*
 Frame trace recording for blastdbg.
 One record per VBlank: taken when the level 6 interrupt is accepted, or at the start of
 VBlank (flagged no_irq) when the game did not take the interrupt before the next VBlank.
 The format is described in debug-megadrive/TRACE_FORMAT.md.
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "trace.h"
#include "genesis.h"
#include "util.h"
#include "paths.h"
#ifndef DISABLE_ZLIB
#include "png.h"
#endif

#define TRACE_VERSION 1
#define HEADER_SIZE 128
#define RECORD_HEADER_SIZE 112
#define WRITE_ENTRY_SIZE 24
#define VINT_LEVEL 6

typedef struct {
	uint64_t cycle;
	uint32_t pc;
	uint32_t address;
	uint32_t seq;
	uint8_t  kind;
	uint8_t  source;
	uint8_t  flags;
	uint8_t  value;
} trace_write;

struct trace_context {
	FILE        *out;
	FILE        *input;
	char        *screenshot_dir;
	char        *translated_path;
	char        *translated_z80_path;
	void        (*on_finish)(genesis_context *gen);
	uint32_t    first_frame;
	uint32_t    last_frame;
	uint32_t    screenshot_every;
	uint32_t    record_flags;
	uint32_t    record_size;
	uint32_t    record_count;
	uint32_t    input_line;
	uint32_t    seen_vint;
	uint32_t    seen_frame;
	uint32_t    shot_through;
	uint64_t    cycle_base;
	uint16_t    pads[2];
	uint8_t     slot_open;
	uint8_t     prov_valid;
	uint8_t     done;
	uint8_t     *record;
	uint8_t     *prov;
	uint64_t    prov_cycle;
	trace_write *log;
	uint32_t    log_len;
	uint32_t    log_storage;
	uint32_t    log_seq;
};

static void put_u16(uint8_t *dst, uint16_t val)
{
	dst[0] = val;
	dst[1] = val >> 8;
}

static void put_u32(uint8_t *dst, uint32_t val)
{
	put_u16(dst, val);
	put_u16(dst + 2, val >> 16);
}

static void put_u64(uint8_t *dst, uint64_t val)
{
	put_u32(dst, val);
	put_u32(dst + 4, val >> 32);
}

static uint32_t section_size(genesis_context *gen, uint32_t section)
{
	switch (section)
	{
	case TRACE_REC_VRAM: return VRAM_SIZE;
	case TRACE_REC_CRAM: return CRAM_SIZE * 2;
	case TRACE_REC_VSRAM: return gen->vdp->vsram_size * 2;
	case TRACE_REC_VDPREGS: return VDP_REGS;
	case TRACE_REC_Z80RAM: return Z80_RAM_BYTES;
	}
	return 0;
}

static uint64_t absolute_cycle(trace_context *trace, uint32_t cycle)
{
	return trace->cycle_base + cycle;
}

static void write_header(trace_context *trace, genesis_context *gen, trace_options *opts, uint8_t complete)
{
	uint8_t header[HEADER_SIZE] = {0};
	memcpy(header, "BDTRACE", 8);
	put_u32(header + 8, TRACE_VERSION);
	put_u32(header + 12, HEADER_SIZE);
	if (opts) {
		memcpy(header + 16, opts->rom_sha256, 32);
		header[69] = opts->pad_type;
	} else {
		//keep the digest and pad type written at the start
		fseek(trace->out, 16, SEEK_SET);
		if (fread(header + 16, 1, 32, trace->out) != 32) {
			fatal_error("Failed to read back trace header\n");
		}
		fseek(trace->out, 69, SEEK_SET);
		header[69] = fgetc(trace->out);
	}
	put_u32(header + 48, trace->record_flags);
	put_u32(header + 52, trace->first_frame);
	put_u32(header + 56, trace->last_frame);
	put_u32(header + 60, trace->record_count);
	put_u32(header + 64, gen->normal_clock);
	header[68] = (gen->version_reg & 0x40) != 0;
	header[70] = gen->version_reg;
	header[71] = complete;
	put_u32(header + 72, RECORD_HEADER_SIZE);
	put_u32(header + 76, WRITE_ENTRY_SIZE);
	put_u32(header + 80, RAM_WORDS * 2);
	put_u32(header + 84, section_size(gen, TRACE_REC_VRAM));
	put_u32(header + 88, section_size(gen, TRACE_REC_CRAM));
	put_u32(header + 92, section_size(gen, TRACE_REC_VSRAM));
	put_u32(header + 96, section_size(gen, TRACE_REC_VDPREGS));
	put_u32(header + 100, section_size(gen, TRACE_REC_Z80RAM));
	put_u32(header + 104, trace->screenshot_every);
	fseek(trace->out, 0, SEEK_SET);
	if (fwrite(header, 1, HEADER_SIZE, trace->out) != HEADER_SIZE) {
		fatal_error("Failed to write trace header\n");
	}
	fseek(trace->out, 0, SEEK_END);
}

//fills a record (without its write count) with the current machine state
static void capture_state(trace_context *trace, genesis_context *gen, uint8_t *dst, uint32_t frame, uint8_t irq, uint32_t pc)
{
	m68k_context *m68k = gen->m68k;
	vdp_context *vdp = gen->vdp;
	memset(dst, 0, RECORD_HEADER_SIZE);
	memcpy(dst, "REC", 4);
	put_u32(dst + 4, frame);
	put_u32(dst + 8, irq);
	put_u64(dst + 16, absolute_cycle(trace, m68k->current_cycle));
	put_u16(dst + 24, trace->pads[0]);
	put_u16(dst + 26, trace->pads[1]);
	put_u32(dst + 28, pc);
	uint16_t sr = m68k->status << 8;
	for (int flag = 0; flag < 5; flag++)
	{
		sr |= m68k->flags[flag] << (4-flag);
	}
	put_u32(dst + 32, sr);
	for (int i = 0; i < 8; i++)
	{
		put_u32(dst + 36 + i * 4, m68k->dregs[i]);
		put_u32(dst + 68 + i * 4, m68k->aregs[i]);
	}
	put_u32(dst + 100, m68k->aregs[8]);
	put_u32(dst + 104, vdp->frame);
	uint8_t *cur = dst + RECORD_HEADER_SIZE;
	//work RAM is stored as native endian words, write it as the 68K sees it
	for (int i = 0; i < RAM_WORDS; i++)
	{
		*(cur++) = gen->work_ram[i] >> 8;
		*(cur++) = gen->work_ram[i];
	}
	if (trace->record_flags & TRACE_REC_VRAM) {
		memcpy(cur, vdp->vdpmem, VRAM_SIZE);
		cur += VRAM_SIZE;
	}
	if (trace->record_flags & TRACE_REC_CRAM) {
		for (int i = 0; i < CRAM_SIZE; i++)
		{
			*(cur++) = vdp->cram[i] >> 8;
			*(cur++) = vdp->cram[i];
		}
	}
	if (trace->record_flags & TRACE_REC_VSRAM) {
		for (int i = 0; i < vdp->vsram_size; i++)
		{
			*(cur++) = vdp->vsram[i] >> 8;
			*(cur++) = vdp->vsram[i];
		}
	}
	if (trace->record_flags & TRACE_REC_VDPREGS) {
		memcpy(cur, vdp->regs, VDP_REGS);
		cur += VDP_REGS;
	}
	if (trace->record_flags & TRACE_REC_Z80RAM) {
		memcpy(cur, gen->zram, Z80_RAM_BYTES);
		cur += Z80_RAM_BYTES;
	}
}

static int compare_writes(const void *a, const void *b)
{
	const trace_write *wa = a, *wb = b;
	if (wa->cycle != wb->cycle) {
		return wa->cycle < wb->cycle ? -1 : 1;
	}
	return wa->seq < wb->seq ? -1 : wa->seq > wb->seq;
}

static void write_translated(char *path, uint32_t (*dump)(void *, FILE *), void *opts)
{
	FILE *f = fopen(path, "w");
	if (!f) {
		fatal_error("Could not open %s for writing\n", path);
	}
	dump(opts, f);
	fclose(f);
}

static void finish(trace_context *trace, genesis_context *gen)
{
	if (trace->on_finish) {
		trace->on_finish(gen);
	}
	if (trace->translated_path) {
		write_translated(trace->translated_path, (uint32_t (*)(void *, FILE *))m68k_dump_translated, gen->m68k->options);
	}
#ifndef NO_Z80
	if (trace->translated_z80_path) {
		write_translated(trace->translated_z80_path, (uint32_t (*)(void *, FILE *))z80_dump_translated, gen->z80->options);
	}
#endif
	write_header(trace, gen, NULL, 1);
	fclose(trace->out);
	if (trace->input) {
		fclose(trace->input);
	}
	printf("Wrote %d trace records\n", trace->record_count);
	fflush(stdout);
	exit(0);
}

//writes a record followed by the logged writes up to its capture cycle, which are then removed from the log
//the Z80 can run a few cycles past the sync point, so later writes stay in the log for the next record
static void emit_record(trace_context *trace, genesis_context *gen, uint8_t *record, uint32_t frame, uint64_t capture_cycle)
{
	qsort(trace->log, trace->log_len, sizeof(trace_write), compare_writes);
	uint32_t num_writes = 0;
	while (num_writes < trace->log_len && trace->log[num_writes].cycle <= capture_cycle)
	{
		num_writes++;
	}
	if (frame >= trace->first_frame && frame <= trace->last_frame) {
		put_u32(record + 12, num_writes);
		if (fwrite(record, 1, trace->record_size, trace->out) != trace->record_size) {
			fatal_error("Failed to write trace record\n");
		}
		for (uint32_t i = 0; i < num_writes; i++)
		{
			uint8_t entry[WRITE_ENTRY_SIZE] = {0};
			trace_write *w = trace->log + i;
			put_u64(entry, w->cycle);
			put_u32(entry + 8, w->pc);
			put_u32(entry + 12, w->address);
			entry[16] = w->kind;
			entry[17] = w->source;
			entry[18] = w->flags;
			entry[19] = w->value;
			if (fwrite(entry, 1, WRITE_ENTRY_SIZE, trace->out) != WRITE_ENTRY_SIZE) {
				fatal_error("Failed to write trace record\n");
			}
		}
		trace->record_count++;
		if (frame == trace->last_frame) {
			trace->done = 1;
		}
	}
	memmove(trace->log, trace->log + num_writes, (trace->log_len - num_writes) * sizeof(trace_write));
	trace->log_len -= num_writes;
}

static uint8_t frame_in_range(trace_context *trace, uint32_t frame)
{
	return frame >= trace->first_frame && frame <= trace->last_frame;
}

static void read_input(trace_context *trace, uint16_t *pads)
{
	pads[0] = pads[1] = 0;
	if (!trace->input) {
		return;
	}
	char line[256];
	while (fgets(line, sizeof(line), trace->input))
	{
		trace->input_line++;
		char *cur = line;
		while (*cur == ' ' || *cur == '\t')
		{
			cur++;
		}
		if (!*cur || *cur == '\n' || *cur == '\r' || *cur == '#') {
			continue;
		}
		char *end;
		for (int i = 0; i < 2; i++)
		{
			unsigned long mask = strtoul(cur, &end, 16);
			if (end == cur || mask > 0xFFF) {
				fatal_error("Invalid pad %d mask on input line %d: %s", i + 1, trace->input_line, line);
			}
			pads[i] = mask;
			cur = end;
		}
		return;
	}
	//input file exhausted, nothing is pressed from here on
}

static void apply_input(trace_context *trace, genesis_context *gen)
{
	uint16_t pads[2];
	read_input(trace, pads);
	for (int pad = 0; pad < 2; pad++)
	{
		uint16_t changed = pads[pad] ^ trace->pads[pad];
		for (int bit = 0; bit < 12; bit++)
		{
			if (!(changed & (1 << bit))) {
				continue;
			}
			//bit order matches the button enum: up, down, left, right, a, b, c, start, x, y, z, mode
			if (pads[pad] & (1 << bit)) {
				gen->header.gamepad_down(&gen->header, pad + 1, bit + DPAD_UP);
			} else {
				gen->header.gamepad_up(&gen->header, pad + 1, bit + DPAD_UP);
			}
		}
		trace->pads[pad] = pads[pad];
	}
}

static uint8_t screenshot_due(trace_context *trace, uint32_t frame)
{
	return trace->screenshot_every && frame_in_range(trace, frame) && !((frame - trace->first_frame) % trace->screenshot_every);
}

static void save_frame_screenshot(trace_context *trace, genesis_context *gen, uint32_t frame)
{
#ifndef DISABLE_ZLIB
	uint32_t width, height, pitch;
	uint32_t *pixels = vdp_get_last_frame(gen->vdp, &width, &height, &pitch);
	if (!pixels) {
		return;
	}
	char name[32];
	sprintf(name, "%06u.png", frame);
	char *path = path_append(trace->screenshot_dir, name);
	FILE *f = fopen(path, "wb");
	if (!f) {
		fatal_error("Could not open %s for writing\n", path);
	}
	save_png(f, pixels, width, height, pitch);
	fclose(f);
	free(path);
#endif
}

trace_context *trace_start(genesis_context *gen, trace_options *opts)
{
	trace_context *trace = calloc(1, sizeof(trace_context));
	trace->out = fopen(opts->out_path, "w+b");
	if (!trace->out) {
		fatal_error("Could not open %s for writing\n", opts->out_path);
	}
	if (opts->input_path) {
		trace->input = fopen(opts->input_path, "r");
		if (!trace->input) {
			fatal_error("Could not open input file %s\n", opts->input_path);
		}
	}
	trace->screenshot_dir = opts->screenshot_dir;
	trace->translated_path = opts->translated_path;
	trace->translated_z80_path = opts->translated_z80_path;
	trace->on_finish = opts->on_finish;
	if (trace->screenshot_dir && !ensure_dir_exists(trace->screenshot_dir)) {
		fatal_error("Could not create screenshot directory %s\n", trace->screenshot_dir);
	}
	trace->screenshot_every = opts->screenshot_dir ? opts->screenshot_every : 0;
	trace->first_frame = opts->first_frame;
	trace->last_frame = opts->last_frame;
	trace->record_flags = opts->record_flags;
	trace->record_size = RECORD_HEADER_SIZE + RAM_WORDS * 2;
	for (uint32_t section = 1; section <= TRACE_REC_Z80RAM; section <<= 1)
	{
		if (trace->record_flags & section) {
			trace->record_size += section_size(gen, section);
		}
	}
	trace->record = malloc(trace->record_size);
	trace->prov = malloc(trace->record_size);
	trace->log_storage = 1024;
	trace->log = malloc(trace->log_storage * sizeof(trace_write));
	trace->shot_through = UINT32_MAX;
	write_header(trace, gen, opts, 0);
	return trace;
}

void trace_adjust_cycles(trace_context *trace, uint32_t deduction)
{
	trace->cycle_base += deduction;
}

void trace_log_write(trace_context *trace, uint8_t source, uint8_t kind, uint32_t address, uint8_t value, uint32_t cycle, uint32_t pc, uint8_t pc_valid)
{
	if (trace->log_len == trace->log_storage) {
		trace->log_storage *= 2;
		trace->log = realloc(trace->log, trace->log_storage * sizeof(trace_write));
	}
	trace_write *w = trace->log + trace->log_len++;
	w->cycle = absolute_cycle(trace, cycle);
	w->pc = pc;
	w->address = address;
	w->seq = trace->log_seq++;
	w->kind = kind;
	w->source = source;
	w->flags = pc_valid;
	w->value = value;
}

void trace_sync(genesis_context *gen)
{
	trace_context *trace = gen->trace;
	m68k_context *m68k = gen->m68k;
	vdp_context *vdp = gen->vdp;
	while (trace->seen_vint != vdp->vint_count)
	{
		//a new VBlank started, the previous one never had its interrupt taken
		if (trace->slot_open) {
			uint32_t frame = trace->seen_vint - 1;
			emit_record(trace, gen, trace->prov, frame, trace->prov_cycle);
		}
		uint32_t frame = trace->seen_vint++;
		apply_input(trace, gen);
		//keep the state at VBlank start in case the interrupt is not taken during this VBlank
		trace->prov_valid = frame_in_range(trace, frame);
		if (trace->prov_valid) {
			capture_state(trace, gen, trace->prov, frame, 0, m68k->last_prefetch_address);
		}
		trace->prov_cycle = absolute_cycle(trace, m68k->current_cycle);
		trace->slot_open = 1;
	}
	if (m68k->int_ack == VINT_LEVEL && trace->slot_open) {
		uint32_t frame = trace->seen_vint - 1;
		if (frame_in_range(trace, frame)) {
			//the exception frame has been pushed and the vector read, the handler has not run yet
			uint32_t vector = VINT_LEVEL * 4 + 0x60;
			void **mem_pointers = (void **)m68k->mem_pointers;
			uint32_t handler = read_word(vector, mem_pointers, &m68k->options->gen, m68k) << 16
				| read_word(vector + 2, mem_pointers, &m68k->options->gen, m68k);
			capture_state(trace, gen, trace->record, frame, 1, handler);
		}
		emit_record(trace, gen, trace->record, frame, absolute_cycle(trace, m68k->current_cycle));
		trace->slot_open = 0;
	}
	if (vdp->frame != trace->seen_frame) {
		trace->seen_frame = vdp->frame;
		if (vdp->done_fb_vint_count) {
			//the completed frame was displayed right before this VBlank
			uint32_t frame = vdp->done_fb_vint_count - 1;
			if (screenshot_due(trace, frame)) {
				save_frame_screenshot(trace, gen, frame);
			}
			trace->shot_through = frame;
		}
	}
	if (trace->done && (!screenshot_due(trace, trace->last_frame) || (trace->shot_through != UINT32_MAX && trace->shot_through >= trace->last_frame))) {
		finish(trace, gen);
	}
}
