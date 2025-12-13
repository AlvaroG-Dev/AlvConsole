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

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif