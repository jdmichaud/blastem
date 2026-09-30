/*
 Headless render backend for BlastEm debugger.
 Provides stub implementations of all render and audio functions
 so the emulator core can run without SDL or any display/audio.
*/
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <pthread.h>
#include "render.h"
#include "render_audio.h"
#include "blastem.h"
#include "config.h"
#include <math.h>

static uint32_t framebuffers[2][512 * 512];
static int fb_pitch = 512 * sizeof(uint32_t);
static uint32_t start_time;

/* --- Video stubs --- */

uint32_t render_map_color(uint8_t r, uint8_t g, uint8_t b)
{
	return (r << 16) | (g << 8) | b;
}

uint32_t *render_get_framebuffer(uint8_t which, int *pitch)
{
	*pitch = fb_pitch;
	return framebuffers[which & 1];
}

void render_framebuffer_updated(uint8_t which, int width) {}
uint8_t render_get_active_framebuffer(void) { return 0; }

void render_init(int width, int height, char *title, uint8_t fullscreen) {}
void render_set_video_standard(vid_std std) {}
void render_toggle_fullscreen() {}
void render_update_caption(char *title) {}

void render_wait_quit(void)
{
	exit(0);
}

void process_events() {}

int render_width() { return 320; }
int render_height() { return 224; }
int render_fullscreen() { return 0; }
void render_set_drag_drop_handler(drop_handler handler) {}

int32_t render_translate_input_name(int32_t controller, char *name, uint8_t is_axis)
{
	return RENDER_NOT_MAPPED;
}

int32_t render_dpad_part(int32_t input) { return input; }
int32_t render_axis_part(int32_t input) { return input; }
uint8_t render_direction_part(int32_t input) { return 0; }
char *render_joystick_type_id(int index) { return ""; }

void render_errorbox(char *title, char *message)
{
	fprintf(stderr, "Error: %s - %s\n", title, message);
}

void render_warnbox(char *title, char *message)
{
	fprintf(stderr, "Warning: %s - %s\n", title, message);
}

void render_infobox(char *title, char *message)
{
	fprintf(stderr, "Info: %s - %s\n", title, message);
}

uint32_t render_emulated_width() { return 320; }
uint32_t render_emulated_height() { return 224; }
uint32_t render_overscan_top() { return 0; }
uint32_t render_overscan_bot() { return 0; }
uint32_t render_overscan_left() { return 0; }

uint32_t render_elapsed_ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000) - start_time;
}

void render_sleep_ms(uint32_t delay)
{
	struct timespec ts = { .tv_sec = delay / 1000, .tv_nsec = (delay % 1000) * 1000000 };
	nanosleep(&ts, NULL);
}

uint8_t render_has_gl(void) { return 0; }
void render_config_updated(void) {}
void render_set_gl_context_handlers(ui_render_fun destroy, ui_render_fun create) {}
void render_set_ui_render_fun(ui_render_fun fun) {}
void render_set_ui_fb_resize_handler(ui_render_fun resize) {}
void render_video_loop(void) {}
uint8_t render_should_release_on_exit(void) { return 0; }
void render_set_external_sync(uint8_t ext_sync_on) {}
void render_reset_mappings(void) {}
void render_save_screenshot(char *path) {}
uint8_t render_create_window(char *caption, uint32_t width, uint32_t height, window_close_handler close_handler) { return 0; }
void render_destroy_window(uint8_t which) {}

/* --- Audio stubs --- */

/*
 Audio capture. When enabled with headless_audio_capture(), every source's output is low-pass
 filtered and resampled to the capture rate with the same code as render_audio.c, and appended
 to a buffer that grows for the whole run. Sample i of every source is the same instant, so the
 buffers can be mixed afterwards exactly like the real-time mixer does.
 In the audio_source, back holds the samples, buffer_pos their count and mask the capacity.
*/
#define BUFFER_INC_RES 0x40000000UL

static uint32_t capture_rate;

void headless_audio_capture(uint32_t rate)
{
	capture_rate = rate;
}

audio_source *render_audio_source(uint64_t master_clock, uint64_t sample_divider, uint8_t channels)
{
	audio_source *src = calloc(1, sizeof(audio_source));
	src->num_channels = channels;
	src->gain_mult = 1.0f;
	if (capture_rate) {
		render_audio_adjust_clock(src, master_clock, sample_divider);
		double lowpass_cutoff = get_lowpass_cutoff(config);
		double rc = (1.0 / lowpass_cutoff) / (2.0 * M_PI);
		src->dt = 1.0 / ((double)master_clock / (double)(sample_divider));
		double alpha = src->dt / (src->dt + rc);
		src->lowpass_alpha = (int32_t)(((double)0x10000) * alpha);
	}
	return src;
}

void render_audio_source_gaindb(audio_source *src, float gain)
{
	src->gain_mult = powf(10.0f, gain/20.0f);
}

void render_audio_adjust_clock(audio_source *src, uint64_t master_clock, uint64_t sample_divider)
{
	if (capture_rate) {
		src->buffer_inc = ((BUFFER_INC_RES * (uint64_t)capture_rate) / master_clock) * sample_divider;
	}
}

static int16_t lowpass_sample(audio_source *src, int16_t last, int16_t current)
{
	int32_t tmp = current * src->lowpass_alpha + last * (0x10000 - src->lowpass_alpha);
	current = tmp >> 16;
	return current;
}

static void interp_sample(audio_source *src, int16_t last, int16_t current)
{
	if (src->buffer_pos == src->mask) {
		src->mask = src->mask ? src->mask * 2 : 1 << 20;
		src->back = realloc(src->back, src->mask * sizeof(int16_t));
	}
	int64_t tmp = last * ((src->buffer_fraction << 16) / src->buffer_inc);
	tmp += current * (0x10000 - ((src->buffer_fraction << 16) / src->buffer_inc));
	src->back[src->buffer_pos++] = tmp >> 16;
}

void render_put_mono_sample(audio_source *src, int16_t value)
{
	if (!capture_rate) {
		return;
	}
	value = lowpass_sample(src, src->last_left, value);
	src->buffer_fraction += src->buffer_inc;
	while (src->buffer_fraction > BUFFER_INC_RES)
	{
		src->buffer_fraction -= BUFFER_INC_RES;
		interp_sample(src, src->last_left, value);
	}
	src->last_left = value;
}

void render_put_stereo_sample(audio_source *src, int16_t left, int16_t right)
{
	if (!capture_rate) {
		return;
	}
	left = lowpass_sample(src, src->last_left, left);
	right = lowpass_sample(src, src->last_right, right);
	src->buffer_fraction += src->buffer_inc;
	while (src->buffer_fraction > BUFFER_INC_RES)
	{
		src->buffer_fraction -= BUFFER_INC_RES;
		interp_sample(src, src->last_left, left);
		interp_sample(src, src->last_right, right);
	}
	src->last_left = left;
	src->last_right = right;
}

//number of captured sample frames and the interleaved samples of a source
uint32_t headless_audio_samples(audio_source *src, int16_t **samples)
{
	*samples = src->back;
	return src->buffer_pos / src->num_channels;
}

//master volume from the config, as applied by the real-time mixer
float headless_audio_overall_gain(void)
{
	char *gain_str = tern_find_path(config, "audio\0gain\0", TVAL_PTR).ptrval;
	return powf(10.0f, (gain_str ? atof(gain_str) : 0.0f)/20.0f);
}
void render_pause_source(audio_source *src) {}
void render_resume_source(audio_source *src) {}

void render_free_source(audio_source *src)
{
	free(src->front);
	free(src->back);
	free(src);
}

void render_audio_initialized(render_audio_format format, uint32_t rate, uint8_t channels, uint32_t buffer_size, int sample_size) {}
int mix_and_convert(unsigned char *byte_stream, int len, int *min_remaining_out) { return 0; }
uint8_t all_sources_ready(void) { return 0; }
void render_audio_adjust_speed(float adjust_ratio) {}
uint8_t render_is_audio_sync(void) { return 0; }
void render_buffer_consumed(audio_source *src) {}
void *render_new_audio_opaque(void) { return NULL; }
void render_free_audio_opaque(void *opaque) {}
void render_lock_audio(void) {}
void render_unlock_audio(void) {}
uint32_t render_min_buffered(void) { return 0; }
uint32_t render_audio_syncs_per_sec(void) { return 0; }
void render_audio_created(audio_source *src) {}
void render_do_audio_ready(audio_source *src) {}
void render_source_paused(audio_source *src, uint8_t remaining_sources) {}
void render_source_resumed(audio_source *src) {}

/* --- Threading stub --- */

typedef pthread_t render_thread;
typedef int (*render_thread_fun)(void*);

uint8_t render_create_thread(render_thread *thread, const char *name, render_thread_fun fun, void *data)
{
	return pthread_create(thread, NULL, (void *(*)(void *))fun, data) == 0;
}

/* --- Bindings stubs (genesis.c calls these) --- */

void bindings_release_capture(void) {}
void bindings_reacquire_capture(void) {}
void set_bindings(void) {}
void bindings_set_mouse_mode(uint8_t mode) {}
void set_content_binding_state(uint8_t enabled) {}
void handle_keydown(int keycode, uint8_t scancode) {}
void handle_keyup(int keycode, uint8_t scancode) {}
void handle_joydown(int joystick, int button) {}
void handle_joyup(int joystick, int button) {}
void handle_joy_dpad(int joystick, int dpad, uint8_t state) {}
void handle_joy_axis(int joystick, int axis, int16_t value) {}
void handle_joy_added(int joystick) {}
void handle_mouse_moved(int mouse, uint16_t x, uint16_t y, int16_t deltax, int16_t deltay) {}
void handle_mousedown(int mouse, int button) {}
void handle_mouseup(int mouse, int button) {}
