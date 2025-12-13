// file: ui_events.h
#ifndef _UI_EVENTS_H
#define _UI_EVENTS_H

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  const char *binPath;
  lv_obj_t *progressBar;
} OTAArgs;

// Funciones existentes
extern void tryLoadGameFromSPIFFS(const char *binPath, lv_obj_t *progressBar);
extern void otaTask(void *param);
extern void restart_console(void);
extern void suspend_console(void);
extern void shutdown_console(void);

void Trigger_Game1(lv_event_t *e);
void Trigger_Game2(lv_event_t *e);
void settings_console(lv_event_t *e);
void poweroff_console(lv_event_t *e);
void back_to_home(lv_event_t *e);

// Nuevas funciones para gestión de WiFi
extern void cleanup_all_wifi_tasks();

// Nuevas funciones para pop-up WiFi
extern void show_wifi_connect_popup(const char *ssid);
extern void update_wifi_popup_progress(int percent, const char *message);
extern void wifi_popup_result(bool success, const char *message);
extern void close_wifi_popup(void);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif