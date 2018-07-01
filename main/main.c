#include "freertos/FreeRTOS.h"
#include "esp_wifi.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_event_loop.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "nofrendo.h"
#include "esp_partition.h"
#include "8bkc-hal.h"
#include "appfs.h"
#include "powerbtn_menu.h"
#include "8bkc-ugui.h"
#include "ugui.h"
#include "8bkcgui-widgets.h"
#include "menu.h"
#include <string.h>

spi_flash_mmap_handle_t hrom=NULL;

char *osd_getromdata(const char *name) {
	int sz;
	char *romdata;
	if (!appfsExists(name)) return NULL;
	int rom_fd=appfsOpen(name);
	appfsEntryInfo(rom_fd, NULL, &sz);
	esp_err_t err=appfsMmap(rom_fd, 0, sz, (const void**)&romdata, SPI_FLASH_MMAP_DATA, &hrom);
	if (err!=ESP_OK) {
		printf("Error: mmap for rom failed\n");
	}
	return (char*)romdata;
}

void osd_unloadromdata() {
	if (!hrom) return;
	appfsMunmap(hrom);
	hrom=NULL;
}

static void debug_screen() {
	kcugui_cls();
	UG_FontSelect(&FONT_6X8);
	UG_SetForecolor(C_WHITE);
	UG_PutString(0, 0, "INFO");
	UG_SetForecolor(C_YELLOW);
	UG_PutString(0, 16, "Nofrendo");
	UG_PutString(0, 24, "Gitrev");
	UG_SetForecolor(C_WHITE);
	UG_PutString(0, 32, GITREV);
	UG_SetForecolor(C_YELLOW);
	UG_PutString(0, 40, "Compiled");
	UG_SetForecolor(C_WHITE);
	UG_PutString(0, 48, COMPILEDATE);
	kcugui_flush();

	while (kchal_get_keys()&KC_BTN_SELECT) vTaskDelay(100/portTICK_RATE_MS);
	while (!(kchal_get_keys()&KC_BTN_SELECT)) vTaskDelay(100/portTICK_RATE_MS);
}


static int fccallback(int button, void **glob, char **desc, void *usrptr) {
	if (button & KC_BTN_POWER) {
		int r=powerbtn_menu_show(kcugui_get_fb());
		//No need to save state or anything as we're not in a game.
		if (r==POWERBTN_MENU_EXIT) kchal_exit_to_chooser();
		if (r==POWERBTN_MENU_POWERDOWN) kchal_power_down();
	}
	if (button & KC_BTN_SELECT) debug_screen();
	return 0;
}

void monTask() {
	while(1) {
		vTaskDelay(1000/portTICK_PERIOD_MS);
		printf("Free mem: dram %d\n", xPortGetFreeHeapSize());
	}
}

extern int osd_quitreason();
extern void osd_queue_loadstate();

int app_main(void) {
	char rom[128]="";
	kchal_init();
	nvs_handle nvsh=kchal_get_app_nvsh();;

	xTaskCreatePinnedToCore(&monTask, "monTask", 1024*2, NULL, 7, NULL, 0);


	while(1) {
		int rom_fd=-1;
		//Either continue from old rom or ask for new one
		unsigned int size=sizeof(rom);
		nvs_get_str(nvsh, "rom", rom, &size);
		if (strlen(rom)!=0 && appfsExists(rom)) {
			rom_fd=appfsOpen(rom);
		} else {
			kcugui_init();
			rom_fd=kcugui_filechooser("*.nes,*.bin", "Select ROM", fccallback, NULL, 0);
			kcugui_deinit();
		}

		nvs_set_str(nvsh, "rom", "");

		//Create commandline args
		const char *args[3];
		args[0]="nofrendo";
		appfsEntryInfo(rom_fd, &args[1], NULL);
		args[2]=NULL;
		osd_queue_loadstate();
		nofrendo_main(2, args);

		int r=osd_quitreason();
		if (r==EMU_RUN_POWERDOWN || r==EMU_RUN_EXIT) {
			//Make sure we continue with this ROM the next time.
			nvs_set_str(nvsh, "rom", args[1]);
		}
		if (r==EMU_RUN_POWERDOWN) kchal_power_down();
		if (r==EMU_RUN_EXIT) kchal_exit_to_chooser();
	}
	return 0;
}

