#ifndef TRACE_H_
#define TRACE_H_

#include <stdint.h>

//Frame trace recording for blastdbg, see debug-megadrive/TRACE_FORMAT.md

//optional record sections, stored in the file header
#define TRACE_REC_VRAM    0x01
#define TRACE_REC_CRAM    0x02
#define TRACE_REC_VSRAM   0x04
#define TRACE_REC_VDPREGS 0x08
#define TRACE_REC_Z80RAM  0x10
#define TRACE_REC_ALL     0x1F

enum {
	TRACE_WRITE_YM = 1,
	TRACE_WRITE_PSG,
	TRACE_WRITE_Z80_RAM,
	TRACE_WRITE_Z80_BUSREQ,
	TRACE_WRITE_Z80_RESET,
	TRACE_WRITE_Z80_BANK
};

enum {
	TRACE_SRC_68K,
	TRACE_SRC_Z80
};

typedef struct genesis_context genesis_context;

typedef struct {
	char     *out_path;
	char     *input_path;
	char     *screenshot_dir;
	char     *translated_path;
	char     *translated_z80_path;
	uint32_t first_frame;
	uint32_t last_frame;
	uint32_t screenshot_every;
	uint32_t record_flags;
	uint8_t  rom_sha256[32];
	uint8_t  pad_type;
	//called when the last record has been written, before the process exits
	void     (*on_finish)(genesis_context *gen);
} trace_options;

typedef struct trace_context trace_context;

trace_context *trace_start(genesis_context *gen, trace_options *opts);
//called from sync_components after the VDP has caught up and before a pending interrupt is acknowledged
void trace_sync(genesis_context *gen);
void trace_adjust_cycles(trace_context *trace, uint32_t deduction);
void trace_log_write(trace_context *trace, uint8_t source, uint8_t kind, uint32_t address, uint8_t value, uint32_t cycle, uint32_t pc, uint8_t pc_valid);

#endif //TRACE_H_
