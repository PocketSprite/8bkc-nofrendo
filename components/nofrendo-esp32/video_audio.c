// Copyright 2015-2016 Espressif Systems (Shanghai) PTE LTD
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at

//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <freertos/FreeRTOS.h>
#include <freertos/timers.h>
#include <freertos/task.h>
#include <freertos/queue.h>
//Nes stuff wants to define this as well...
#undef false
#undef true
#undef bool


#include <math.h>
#include <string.h>
#include <noftypes.h>
#include <bitmap.h>
#include <nofconfig.h>
#include <event.h>
#include <gui.h>
#include <log.h>
#include <nes.h>
#include <nes_pal.h>
#include <nesinput.h>
#include <osd.h>
#include <stdint.h>
#include "driver/i2s.h"
#include "sdkconfig.h"
#include <8bkc-hal.h>
#include "powerbtn_menu.h"
#include "esp_timer.h"
#include "menu.h"

#define  DEFAULT_SAMPLERATE   22050
#define  DEFAULT_FRAGSIZE     128

#define  DEFAULT_WIDTH        256
#define  DEFAULT_HEIGHT       NES_VISIBLE_HEIGHT


esp_timer_handle_t timer=NULL;
int timerfreq;
uint16_t *oledfb;

//Seemingly, this will be called only once. Should call func with a freq of frequency,
int osd_installtimer(int frequency, void *func, int funcsize, void *counter, int countersize)
{
	if (timer) {
		esp_timer_stop(timer);
		esp_timer_delete(timer);
	}
	printf("Timer install, freq=%d\n", frequency);
	esp_timer_create_args_t args={
		.callback=func,
		.dispatch_method=ESP_TIMER_TASK,
		.name="refresh"
	};
	esp_timer_create(&args, &timer);
	esp_timer_start_periodic(timer, 1000000/frequency);
	timerfreq=frequency;
	return 0;
}


/*
** Audio
*/
static void (*audio_callback)(void *buffer, int length) = NULL;
static uint16_t *audio_frame;

static void do_audio_frame() {
	int left=(DEFAULT_SAMPLERATE/NES_REFRESH_RATE);
	while(left) {
		int n=DEFAULT_FRAGSIZE;
		if (n>left) n=left;
		audio_callback(audio_frame, n); //get more data
		kchal_sound_push(audio_frame, n);
		left-=n;
	}
}

void osd_setsound(void (*playfunc)(void *buffer, int length)) {
	//Indicates we should call playfunc() to get more data.
	audio_callback = playfunc;
}




void osd_getsoundinfo(sndinfo_t *info) {
	info->sample_rate = DEFAULT_SAMPLERATE;
	info->bps = 8;
}

/*
** Video
*/

static int init(int width, int height);
static void shutdown(void);
static int set_mode(int width, int height);
static void set_palette(rgb_t *pal);
static void clear(uint8 color);
static bitmap_t *lock_write(void);
static void free_write(int num_dirties, rect_t *dirty_rects);
static void custom_blit(bitmap_t *bmp, int num_dirties, rect_t *dirty_rects);
static char fb[1]; //dummy

QueueHandle_t vidQueue;

viddriver_t pkspDriver =
{
   "PocketSprite video",         /* name */
   init,          /* init */
   shutdown,      /* shutdown */
   set_mode,      /* set_mode */
   set_palette,   /* set_palette */
   clear,         /* clear */
   lock_write,    /* lock_write */
   free_write,    /* free_write */
   custom_blit,   /* custom_blit */
   false          /* invalidate flag */
};


bitmap_t *myBitmap;

void osd_getvideoinfo(vidinfo_t *info) {
   info->default_width = DEFAULT_WIDTH;
   info->default_height = DEFAULT_HEIGHT;
   info->driver = &pkspDriver;
}

/* flip between full screen and windowed */
void osd_togglefullscreen(int code) {
}

/* initialise video */
static int init(int width, int height) {
	return 0;
}

static void shutdown(void) {
}

/* set a video mode */
static int set_mode(int width, int height) {
	return 0;
}

uint8 myPalette[256*3];

/* copy nes palette over to hardware */
static void set_palette(rgb_t *pal) {
	uint16 c;
	int i;
	for (i = 0; i < 256; i++) {
		myPalette[i*3+0]=pal[i].r;
		myPalette[i*3+1]=pal[i].g;
		myPalette[i*3+2]=pal[i].b;
	}
}

/* clear all frames to a particular color */
static void clear(uint8 color) {
}

/* acquire the directbuffer for writing */
static bitmap_t *lock_write(void) {
   myBitmap = bmp_createhw((uint8*)fb, DEFAULT_WIDTH, DEFAULT_HEIGHT, DEFAULT_WIDTH*2);
   return myBitmap;
}

/* release the resource */
static void free_write(int num_dirties, rect_t *dirty_rects) {
   bmp_destroy(&myBitmap);
}


static void custom_blit(bitmap_t *bmp, int num_dirties, rect_t *dirty_rects) {
	xQueueSend(vidQueue, &bmp, 0);
	do_audio_frame();
}

//This runs on core 1.

const uint8_t kernel[6][3]={
	{12, 2, 2}, {2, 12, 2}, {2, 2, 12},
	{6, 1, 1}, {1, 6, 1}, {1, 1, 6}
};

static uint16_t get_pix_around(char **pic, int py, int px, int halfIsUpper) {
	int r=0, g=0, b=0;
	for (int y=0; y<4; y++) {
		int o=0;
		if (halfIsUpper && y==0) o=3;
		if (!halfIsUpper && y==3) o=3;
		for (int x=0; x<3; x++) {
			int pale=pic[y+py][x+px]*3;
			r+=kernel[x+o][0]*myPalette[pale+0];
			g+=kernel[x+o][1]*myPalette[pale+1];
			b+=kernel[x+o][2]*myPalette[pale+2];
		}
	}
	r=r/((12+2+2)*3+(6+1+1));
	g=g/((12+2+2)*3+(6+1+1));
	b=b/((12+2+2)*3+(6+1+1));
	uint16_t rgb16=((r>>(3))<<11)+((g>>(2))<<5)+((b>>(3))<<0);
	return (rgb16<<8)|(rgb16>>8);
}

//Scale: hor div by 3, ver div by 3.5.
static void videoTask(void *arg) {
	int x, y;
	oledfb=malloc(KC_SCREEN_W*KC_SCREEN_H*2);
	bitmap_t *bmp=NULL;
	while(1) {
		xQueueReceive(vidQueue, &bmp, portMAX_DELAY);
		const uint8_t **pic=(const uint8_t **)bmp->line;
		uint16_t *fbp=oledfb;
		for (int y=0; y<KC_SCREEN_H; y++) {
			int ny=(y*7)/2;
			int nx=0;
			for (int x=0; x<KC_SCREEN_W; x++) {
				*fbp++=get_pix_around(pic, ny, nx, y&1);
				nx+=3;
			}
		}
		kchal_send_fb(oledfb);
	}
}


/*
** Input
*/

static int quit_reason=EMU_RUN_CONT;

int osd_quitreason() {
	return quit_reason;
}

static int do_loadstate=0;

void osd_queue_loadstate() {
	do_loadstate=5;
}

void osd_getinput(void) {
	const int ev[8]={
		event_joypad1_right, event_joypad1_left, event_joypad1_up, event_joypad1_down, 
		event_joypad1_start, event_joypad1_select, event_joypad1_a, event_joypad1_b
	};
	static int oldb=0;
	int b=kchal_get_keys();
	event_t evh;
	
	//do_loadstate is set to a certain number on bootup, because for some reason loading the state
	//doesn't work directly after boot-up. This causes it to wait for a few frames.
	if (do_loadstate>0) do_loadstate--;
	if (do_loadstate==1) {
		evh=event_get(event_state_slot_0);
		if (evh) evh(INP_STATE_MAKE);
		evh=event_get(event_state_load);
		if (evh) evh(INP_STATE_MAKE);
	}

	if (b & KC_BTN_POWER) {
		//Show powerbutton menu. 
		kchal_sound_mute(1);
		if (timer) esp_timer_stop(timer);
		vTaskDelay(20); //hack: make sure video task is done with framebuffer
		int r=nofrendoShowMenu(oledfb);
		while(kchal_get_keys()&KC_BTN_POWER) vTaskDelay(10); //wait till powerbutton is released
		quit_reason=r;
		if (r==EMU_RUN_RESET) {
			evh=event_get(event_hard_reset);
			if (evh) evh(INP_STATE_MAKE);
		} else if (r==EMU_RUN_NEWROM || r==EMU_RUN_POWERDOWN || r==EMU_RUN_EXIT) {
			evh=event_get(event_state_slot_0);
			if (evh) evh(INP_STATE_MAKE);
			evh=event_get(event_state_save);
 			if (evh) evh(INP_STATE_MAKE);
			evh=event_get(event_quit);
			if (evh) evh(INP_STATE_MAKE);
		} else {
			kchal_sound_mute(0);
		}
		if (timer) esp_timer_start_periodic(timer, 1000000/timerfreq);
	}
	
	int chg=b^oldb;
	int x;
	oldb=b;
	for (x=0; x<8; x++) {
		if (chg&1) {
			evh=event_get(ev[x]);
			if (evh) evh((b&1)?INP_STATE_MAKE:INP_STATE_BREAK);
		}
		chg>>=1;
		b>>=1;
	}
}

void osd_getmouse(int *x, int *y, int *button) {
}

/* this is at the bottom, to eliminate warnings */
void osd_shutdown() {
	audio_callback = NULL;
}

static int logprint(const char *string) {
   return printf("%s", string);
}

/*
** Startup
*/

//We actually should shut down video in osd_shutdown, but that's a PITA... we use this to just init it once.
static int osd_inited=0;

int osd_init()
{
	audio_callback = NULL;

	if (osd_inited) {
		//Make sound is not muted and return
		kchal_sound_mute(0);
		return 0;
	}
	osd_inited=1;

	kchal_sound_start(DEFAULT_SAMPLERATE, 1024);
	audio_frame=malloc(4*DEFAULT_FRAGSIZE);

	log_chain_logfunc(logprint);

	vidQueue=xQueueCreate(1, sizeof(bitmap_t *));
	xTaskCreatePinnedToCore(&videoTask, "videoTask", 2048, NULL, 5, NULL, 1);
	return 0;
}
