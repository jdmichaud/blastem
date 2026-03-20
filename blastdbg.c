/*
 BlastDbg - Headless Mega Drive / Genesis ROM debugger
 Based on BlastEm by Michael Pavone
 Stripped-down build requiring no SDL, OpenGL, or audio output.
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "system.h"
#include "68kinst.h"
#include "m68k_core.h"
#ifndef NEW_CORE
#include "z80_to_x86.h"
#endif
#include "vdp.h"
#include "render.h"
#include "render_audio.h"
#include "genesis.h"
#include "gst.h"
#include "util.h"
#include "romdb.h"
#include "terminal.h"
#include "arena.h"
#include "config.h"
#include "hash.h"
#include "zip.h"
#include "saves.h"

#ifndef DISABLE_ZLIB
#include "zlib/zlib.h"
#define ROMFILE gzFile
#define romopen gzopen
#define romread gzfread
#define romseek gzseek
#define romgetc gzgetc
#define romclose gzclose
#else
#define ROMFILE FILE*
#define romopen fopen
#define romread fread
#define romseek fseek
#define romgetc fgetc
#define romclose fclose
#endif

#define BLASTEM_VERSION "0.6.3-pre"

int headless = 1;
int exit_after = 0;
int z80_enabled = 1;
int frame_limit = 0;
uint8_t use_native_states = 1;
int break_on_sync = 0;

tern_node *config;

system_header *current_system;
system_header *menu_system;
system_header *game_system;

char *save_state_path;
char *save_filename;

/* --- ROM loading (from blastem.c) --- */

#define SMD_HEADER_SIZE 512
#define SMD_MAGIC1 0x03
#define SMD_MAGIC2 0xAA
#define SMD_MAGIC3 0xBB
#define SMD_BLOCK_SIZE 0x4000

static uint16_t *process_smd_block(uint16_t *dst, uint8_t *src, size_t bytes)
{
	for (uint8_t *low = src, *high = (src+bytes/2), *end = src+bytes; high < end; high++, low++) {
		*(dst++) = *low << 8 | *high;
	}
	return dst;
}

static int load_smd_rom(ROMFILE f, void **buffer)
{
	uint8_t block[SMD_BLOCK_SIZE];
	romseek(f, SMD_HEADER_SIZE, SEEK_SET);
	size_t filesize = 512 * 1024;
	size_t readsize = 0;
	uint16_t *dst, *buf;
	dst = buf = malloc(filesize);
	size_t read;
	do {
		if ((readsize + SMD_BLOCK_SIZE > filesize)) {
			filesize *= 2;
			buf = realloc(buf, filesize);
			dst = buf + readsize/sizeof(uint16_t);
		}
		read = romread(block, 1, SMD_BLOCK_SIZE, f);
		if (read > 0) {
			dst = process_smd_block(dst, block, read);
			readsize += read;
		}
	} while(read > 0);
	romclose(f);
	*buffer = buf;
	return readsize;
}

static uint8_t is_smd_format(const char *filename, uint8_t *header)
{
	if (header[1] == SMD_MAGIC1 && header[8] == SMD_MAGIC2 && header[9] == SMD_MAGIC3) {
		int i;
		for (i = 3; i < 8; i++) {
			if (header[i] != 0) {
				return 0;
			}
		}
		if (i == 8) {
			if (header[2]) {
				fatal_error("%s is a split SMD ROM which is not currently supported", filename);
			}
			return 1;
		}
	}
	return 0;
}

static uint32_t load_rom_zip(const char *filename, void **dst)
{
	static const char *valid_exts[] = {"bin", "md", "gen", "sms", "rom", "smd"};
	const uint32_t num_exts = sizeof(valid_exts)/sizeof(*valid_exts);
	zip_file *z = zip_open(filename);
	if (!z) {
		return 0;
	}
	for (uint32_t i = 0; i < z->num_entries; i++)
	{
		char *ext = path_extension(z->entries[i].name);
		if (!ext) {
			continue;
		}
		for (uint32_t j = 0; j < num_exts; j++)
		{
			if (!strcasecmp(ext, valid_exts[j])) {
				size_t out_size = nearest_pow2(z->entries[i].size);
				*dst = zip_read(z, i, &out_size);
				if (*dst) {
					if (is_smd_format(z->entries[i].name, *dst)) {
						size_t offset;
						for (offset = 0; offset + SMD_BLOCK_SIZE + SMD_HEADER_SIZE <= out_size; offset += SMD_BLOCK_SIZE)
						{
							uint8_t tmp[SMD_BLOCK_SIZE];
							uint8_t *u8dst = *dst;
							memcpy(tmp, u8dst + offset + SMD_HEADER_SIZE, SMD_BLOCK_SIZE);
							process_smd_block((void *)(u8dst + offset), tmp, SMD_BLOCK_SIZE);
						}
						out_size = offset;
					}
					free(ext);
					zip_close(z);
					return out_size;
				}
			}
		}
		free(ext);
	}
	zip_close(z);
	return 0;
}

uint32_t load_rom(const char *filename, void **dst, system_type *stype)
{
	uint8_t header[10];
	char *ext = path_extension(filename);
	if (ext && !strcasecmp(ext, "zip")) {
		free(ext);
		return load_rom_zip(filename, dst);
	}
	free(ext);
	ROMFILE f = romopen(filename, "rb");
	if (!f) {
		return 0;
	}
	if (sizeof(header) != romread(header, 1, sizeof(header), f)) {
		fatal_error("Error reading from %s\n", filename);
	}
	if (is_smd_format(filename, header)) {
		if (stype) {
			*stype = SYSTEM_GENESIS;
		}
		return load_smd_rom(f, dst);
	}
	size_t filesize = 512 * 1024;
	size_t readsize = sizeof(header);
	char *buf = malloc(filesize);
	memcpy(buf, header, readsize);
	size_t read;
	do {
		read = romread(buf + readsize, 1, filesize - readsize, f);
		if (read > 0) {
			readsize += read;
			if (readsize == filesize) {
				int one_more = romgetc(f);
				if (one_more >= 0) {
					filesize *= 2;
					buf = realloc(buf, filesize);
					buf[readsize++] = one_more;
				} else {
					read = 0;
				}
			}
		}
	} while (read > 0);
	*dst = buf;
	romclose(f);
	return readsize;
}

/* --- Save management (from blastem.c) --- */

static void persist_save(void)
{
	if (!game_system) {
		return;
	}
	game_system->persist_save(game_system);
}

static char *get_save_dir(system_media *media)
{
	char *savedir_template = tern_find_path(config, "ui\0save_path\0", TVAL_PTR).ptrval;
	if (!savedir_template) {
		savedir_template = "$USERDATA/blastem/$ROMNAME";
	}
	tern_node *vars = tern_insert_ptr(NULL, "ROMNAME", media->name);
	vars = tern_insert_ptr(vars, "ROMDIR", media->dir);
	vars = tern_insert_ptr(vars, "HOME", get_home_dir());
	vars = tern_insert_ptr(vars, "EXEDIR", get_exe_dir());
	vars = tern_insert_ptr(vars, "USERDATA", (char *)get_userdata_dir());
	char *save_dir = replace_vars(savedir_template, vars, 1);
	tern_free(vars);
	if (!ensure_dir_exists(save_dir)) {
		warning("Failed to create save directory %s\n", save_dir);
	}
	return save_dir;
}

static const char *get_save_fname(uint8_t save_type)
{
	switch(save_type)
	{
	case SAVE_I2C: return "save.eeprom";
	case SAVE_NOR: return "save.nor";
	case SAVE_HBPT: return "save.hbpt";
	default: return "save.sram";
	}
}

void setup_saves(system_media *media, system_header *context)
{
	static uint8_t persist_save_registered;
	rom_info *info = &context->info;
	char *save_dir = get_save_dir(info->is_save_lock_on ? media->chain : media);
	char const *parts[] = {save_dir, PATH_SEP, get_save_fname(info->save_type)};
	free(save_filename);
	save_filename = alloc_concat_m(3, parts);
	if (info->is_save_lock_on) {
		free(save_dir);
		parts[0] = save_dir = get_save_dir(media);
	}
	if (use_native_states || context->type != SYSTEM_GENESIS) {
		parts[2] = "quicksave.state";
	} else {
		parts[2] = "quicksave.gst";
	}
	free(save_state_path);
	save_state_path = alloc_concat_m(3, parts);
	context->save_dir = save_dir;
	if (info->save_type != SAVE_NONE) {
		context->load_save(context);
		if (!persist_save_registered) {
			atexit(persist_save);
			persist_save_registered = 1;
		}
	}
}

/* --- Stubs for functions referenced by other modules --- */

void update_title(char *rom_name) {}
void reload_media(void) {}
void lockon_media(char *lock_on_path) {}
void apply_updated_config(void) {}
const system_media *current_media(void) { return NULL; }
void init_system_with_media(const char *path, system_type force_stype) {}

/* --- Main --- */

static void print_usage(void)
{
	fprintf(stderr,
		"blastdbg %s - Headless Mega Drive / Genesis ROM debugger\n"
		"Usage: blastdbg [OPTIONS] ROMFILE\n"
		"Options:\n"
		"  -r (J|U|E)  Force region\n"
		"  -m MACHINE  Force machine type (sms, gen)\n"
		"  -n          Disable Z80\n"
		"  -h          Print this help\n",
		BLASTEM_VERSION
	);
}

int main(int argc, char **argv)
{
	set_exe_str(argv[0]);
	config = load_config();

	int loaded = 0;
	system_type stype = SYSTEM_UNKNOWN, force_stype = SYSTEM_UNKNOWN;
	char *romfname = NULL;
	uint8_t force_region = 0;
	uint32_t opts = 0;
	system_media cart = {0};

	for (int i = 1; i < argc; i++) {
		if (argv[i][0] == '-') {
			switch (argv[i][1]) {
			case 'r':
				i++;
				if (i >= argc) {
					fatal_error("-r must be followed by a region (J, U or E)\n");
				}
				force_region = translate_region_char(argv[i][0]);
				if (!force_region) {
					fatal_error("'%c' is not a valid region character\n", argv[i][0]);
				}
				break;
			case 'm':
				i++;
				if (i >= argc) {
					fatal_error("-m must be followed by a machine type\n");
				}
				if (!strcmp("sms", argv[i])) {
					stype = force_stype = SYSTEM_SMS;
				} else if (!strcmp("gen", argv[i])) {
					stype = force_stype = SYSTEM_GENESIS;
				} else {
					fatal_error("Unrecognized machine type %s\n", argv[i]);
				}
				break;
			case 'n':
				z80_enabled = 0;
				break;
			case 'h':
				print_usage();
				return 0;
			default:
				fatal_error("Unrecognized switch %s\n", argv[i]);
			}
		} else if (!loaded) {
			if (!(cart.size = load_rom(argv[i], &cart.buffer, stype == SYSTEM_UNKNOWN ? &stype : NULL))) {
				fatal_error("Failed to open %s for reading\n", argv[i]);
			}
			cart.dir = path_dirname(argv[i]);
			cart.name = basename_no_extension(argv[i]);
			cart.extension = path_extension(argv[i]);
			romfname = argv[i];
			loaded = 1;
		}
	}

	if (!loaded) {
		print_usage();
		return 1;
	}

	if (stype == SYSTEM_UNKNOWN) {
		stype = detect_system_type(&cart);
	}
	if (stype == SYSTEM_UNKNOWN) {
		fatal_error("Failed to detect system type for %s\n", romfname);
	}

	current_system = alloc_config_system(stype, &cart, opts, force_region);
	if (!current_system) {
		fatal_error("Failed to configure emulated machine for %s\n", romfname);
	}
	game_system = current_system;

	setup_saves(&cart, current_system);

	current_system->debugger_type = DEBUGGER_NATIVE;
	current_system->enter_debugger = 1;

	force_no_terminal();
	current_system->start_context(current_system, NULL);

	return 0;
}
