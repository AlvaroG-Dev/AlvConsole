#define LV_CONF_INCLUDE_SIMPLE

#include "PacMan/Input.h"
#include "esp_ota_ops.h"
#include "ui.h"
#include "ui_events.h"
#include <Adafruit_INA219.h>
#include <Arduino.h>
#include <SD.h>
#include <SPI.h>
#include <SPIFFS.h>
#include <TFT_eSPI.h>
#include <Update.h>
#include <WiFi.h>
#include <Wire.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <lvgl.h>

// ============= CONFIGURACIÓN DE PINES =============
#define TFT_MOSI 35
#define TFT_MISO 37
#define TFT_SCLK 36
#define TFT_CS 10
#define TFT_DC 9
#define TFT_RST 8
#define TFT_BL 7
#define SD_CS 14

#define TOUCH_SDA 4
#define TOUCH_SCL 5
#define TOUCH_RST 3
#define TOUCH_INT 2

#define SCREEN_WIDTH 480
#define SCREEN_HEIGHT 320
#define DRAW_BUF_SIZE (SCREEN_WIDTH * SCREEN_HEIGHT / 10)

TFT_eSPI tft = TFT_eSPI();
bool sdInitialized = false;

extern "C" {
extern lv_obj_t *ui_WifiList;
extern lv_obj_t *ui_WifiPass;
}

static lv_disp_draw_buf_t draw_buf;
static lv_color_t buf1[DRAW_BUF_SIZE];
static lv_color_t buf2[DRAW_BUF_SIZE];
static lv_disp_drv_t disp_drv;
static lv_indev_drv_t indev_drv;
static lv_indev_drv_t indev_keypad_drv;

Input input;

static bool suspended = false;

struct ProgressData {
  lv_obj_t *bar;
  int value;
};

// ============= TÁCTIL =============
#define FT6336_ADDR 0x38

typedef struct {
  uint16_t x;
  uint16_t y;
  bool touched;
} TouchPoint;

TouchPoint touchPoint = {0, 0, false};

// Variable global para controlar el estado
bool otaInProgress = false;
bool gameLoaded = false;
static lv_obj_t *currentProgressBar = NULL;
static int currentProgress = 0;
static uint32_t lastProgressUpdate = 0;

// ============= BATERÍA =============
Adafruit_INA219 ina219;
unsigned long ultimoTiempo = 0;
bool primeraLectura = true;
unsigned long lastBatteryUpdate = 0;
const unsigned long BATTERY_UPDATE_INTERVAL = 1000; // 2s

// Variables para detección de carga/descarga
float current_threshold_charging = -20.0f;   // mA (negativo = carga)
float current_threshold_discharging = 10.0f; // mA (positivo = descarga)
unsigned long state_change_time = 0;
#define MIN_STATE_TIME 2000 // Tiempo mínimo para cambiar de estado (ms)

#define BAT_INTERNAL_RESISTANCE 0.25f // ohmios (ajustable)
#define BAT_CAPACITY_mAh 1000.0f      // Capacidad real de tu batería
#define BATTERY_CUTOFF_VOLTAGE 3.2f   // Voltaje mínimo seguro
#define BATTERY_FULL_VOLTAGE 4.2f     // Voltaje de carga completa
#define BATTERY_NOMINAL_VOLTAGE 3.7f  // Voltaje nominal

// Estados de carga
enum BatteryState {
  BATTERY_CHARGING,    // Se está cargando
  BATTERY_DISCHARGING, // Se está descargando
  BATTERY_RESTING      // En reposo (corriente baja)
};

// Variables para Coulomb Counting
float total_current_mAh = 0;    // Corriente acumulada (positiva=descarga)
float last_soc_coulomb = 50.0f; // Último SOC calculado por coulomb
float last_soc_voltage = 50.0f; // Último SOC calculado por voltaje
unsigned long last_update_time = 0;
BatteryState battery_state = BATTERY_RESTING;

float consumoTotal_mAh = 0; // mAh descargados
float cargaTotal_mAh = 0;   // mAh cargados
float last_reported_percentage = 50.0;
float last_stable_voltage = 3.7;

// Variables para filtrado
#define VOLTAGE_SAMPLES 10
float voltage_history[VOLTAGE_SAMPLES] = {0};
int voltage_index = 0;
float smoothed_voltage = 3.7f;

// ===================== WIFI =====================
static char selected_ssid[64] = {0};
static bool wifi_scanning = false;
static SemaphoreHandle_t wifi_mutex = NULL;
static volatile bool wifi_update_pending = false;
static char wifi_status_message[128] = {0};
static const char *wifi_status_icon = NULL;
static SemaphoreHandle_t wifi_status_mutex = NULL;
static SemaphoreHandle_t scan_popup_mutex = NULL;
static volatile bool scan_popup_ready = false;

// Variables para pop-up de escaneo
static lv_obj_t *scan_popup = NULL;
static lv_obj_t *scan_popup_title = NULL;
static lv_obj_t *scan_popup_icon = NULL;
static lv_obj_t *scan_popup_message = NULL;
static lv_obj_t *scan_popup_arc = NULL;
static lv_timer_t *scan_popup_timer = NULL;

#define LVGL_MUTEX_TIMEOUT 500

static bool ui_initialized = false;
static uint32_t last_memory_check = 0;
static SemaphoreHandle_t lvgl_mutex = NULL;
static QueueHandle_t popup_event_queue = NULL;

// ============= WIFI POPUP VARIABLES =============
static lv_obj_t *wifi_popup = NULL;
static lv_obj_t *wifi_popup_title = NULL;
static lv_obj_t *wifi_popup_icon = NULL;
static lv_obj_t *wifi_popup_message = NULL;
static lv_obj_t *wifi_popup_progress = NULL;
static lv_obj_t *wifi_popup_arc = NULL;
static lv_timer_t *wifi_popup_timer = NULL;
static int wifi_popup_angle = 0;

// Estructura para redes WiFi
struct NetworkInfo {
  char ssid[33];
  int32_t rssi;
  bool secure;
};

// Gestión de tareas
typedef struct {
  TaskHandle_t handle;
  const char *name;
  bool running;
} TaskInfo;

enum PopupEventType { POPUP_SHOW_SCAN, POPUP_UPDATE_SCAN, POPUP_CLOSE_SCAN };

struct PopupEvent {
  PopupEventType type;
  int data; // Para network_count
};

static TaskInfo wifi_tasks[5] = {0};
static int wifi_task_count = 0;

// Redes WiFi pendientes de actualizar
static NetworkInfo pending_networks[10];
static int pending_count = 0;

// ============= PROTOTIPOS DE FUNCIONES =============
void initDisplay();
bool initSD();
void initTouch();
void initLVGL();
bool testSD();
bool operacionSeguraSD(std::function<bool()> operacion);
bool guardarEnSD(const char *filename, const char *data);
String leerDeSD(const char *filename);
void print_memory_info();
void safe_delete_task(TaskHandle_t task);
void cleanup_all_wifi_tasks();
bool register_wifi_task(TaskHandle_t handle, const char *name);
void unregister_wifi_task(TaskHandle_t handle);
void lvgl_safe_call(void (*func)());
void lvgl_safe_call_with_arg(void (*func)(void *), void *arg);
static void update_wifi_list_safe(NetworkInfo *networks, int count);
static void wifi_scan_task_safe(void *pvParameter);

// ============= GESTIÓN SEGURA DE MEMORIA =============
void print_memory_info() {
  static uint32_t last_print = 0;
  if (millis() - last_print < 5000)
    return;

  last_print = millis();
  Serial.printf("Memoria: Libre=%d, Min=%d, Frag=NA\n", ESP.getFreeHeap(),
                ESP.getMinFreeHeap());
  // ESP.getHeapFragmentation() no existe en ESP32, lo eliminamos
}

bool take_lvgl_mutex_with_retry(int max_retries = 3) {
  for (int i = 0; i < max_retries; i++) {
    if (xSemaphoreTake(lvgl_mutex, pdMS_TO_TICKS(LVGL_MUTEX_TIMEOUT))) {
      return true;
    }
    Serial.printf("Intento %d: mutex LVGL ocupado, esperando...\n", i + 1);

    // Pequeña pausa entre reintentos
    vTaskDelay(50 / portTICK_PERIOD_MS);

    // Forzar procesamiento de LVGL si está bloqueado
    lv_timer_handler();
  }
  return false;
}

// NUEVA FUNCIÓN: Procesador de eventos de pop-up (llamar desde loop)
void process_popup_events() {
  static PopupEvent event;

  // Procesar todos los eventos pendientes
  while (popup_event_queue &&
         xQueueReceive(popup_event_queue, &event, 0) == pdTRUE) {

    // Intentar obtener mutex LVGL con reintentos
    if (!take_lvgl_mutex_with_retry(2)) {
      Serial.printf("ERROR: No se pudo obtener mutex LVGL para evento %d\n",
                    event.type);
      continue; // Saltar este evento, procesar el siguiente
    }

    switch (event.type) {
    case POPUP_SHOW_SCAN:
      Serial.println("Procesando evento: Mostrar pop-up de escaneo");
      show_wifi_scan_popup();
      break;

    case POPUP_UPDATE_SCAN:
      Serial.printf("Procesando evento: Actualizar pop-up con %d redes\n",
                    event.data);
      // Llamar a la versión DIRECTA (sin verificación de ready, ya estamos en
      // contexto LVGL)
      if (scan_popup) {
        if (scan_popup_message && lv_obj_is_valid(scan_popup_message)) {
          char counter_text[48];
          snprintf(counter_text, sizeof(counter_text), "Redes encontradas: %d",
                   event.data);
          lv_label_set_text(scan_popup_message, counter_text);
          lv_obj_invalidate(scan_popup_message);
        }
      } else {
        Serial.println("WARNING: scan_popup es NULL al actualizar");
      }
      break;

    case POPUP_CLOSE_SCAN:
      if (event.data == -1) {
        // Caso especial: solo limpiar lista
        Serial.println("Procesando evento: Limpiar lista WiFi");
        if (ui_WifiList && lv_obj_is_valid(ui_WifiList)) {
          wifi_list_cleanup(ui_WifiList);
          lv_obj_clean(ui_WifiList);
          lv_list_add_btn(ui_WifiList, LV_SYMBOL_REFRESH, "Escaneando...");
        }
      } else {
        Serial.println("Procesando evento: Cerrar pop-up de escaneo");
        close_wifi_scan_popup();
      }
      break;
    }

    xSemaphoreGive(lvgl_mutex);

    // Pequeña pausa para que LVGL respire
    vTaskDelay(5 / portTICK_PERIOD_MS);
  }
}

static void memory_cleanup_task(void *param) {
  while (1) {
    vTaskDelay(10000 / portTICK_PERIOD_MS); // Cada 10 segundos

    // Imprimir info de memoria
    print_memory_info();

    // Limpiar tareas WiFi si llevan mucho tiempo
    for (int i = 0; i < wifi_task_count; i++) {
      if (wifi_tasks[i].running &&
          eTaskGetState(wifi_tasks[i].handle) == eInvalid) {
        Serial.printf("Limpiando tarea zombie: %s\n", wifi_tasks[i].name);
        wifi_tasks[i].running = false;
        wifi_tasks[i].handle = NULL;
      }
    }
  }
}

void safe_delete_task(TaskHandle_t task) {
  if (task == NULL)
    return;

  // Verificar si la tarea está corriendo
  eTaskState state = eTaskGetState(task);
  if (state != eDeleted && state != eInvalid) {
    vTaskDelete(task);
  }

  // Esperar a que se limpie
  vTaskDelay(50 / portTICK_PERIOD_MS);
}

void cleanup_all_wifi_tasks() {
  Serial.println("Limpiando todas las tareas WiFi...");

  for (int i = 0; i < wifi_task_count; i++) {
    if (wifi_tasks[i].running && wifi_tasks[i].handle != NULL) {
      Serial.printf("Terminando tarea: %s\n", wifi_tasks[i].name);
      safe_delete_task(wifi_tasks[i].handle);
      wifi_tasks[i].running = false;
      wifi_tasks[i].handle = NULL;
    }
  }
  wifi_task_count = 0;
  wifi_scanning = false;
}

bool register_wifi_task(TaskHandle_t handle, const char *name) {
  if (wifi_task_count >= 5) {
    Serial.println("ERROR: Demasiadas tareas WiFi");
    return false;
  }

  wifi_tasks[wifi_task_count].handle = handle;
  wifi_tasks[wifi_task_count].name = name;
  wifi_tasks[wifi_task_count].running = true;
  wifi_task_count++;

  return true;
}

void unregister_wifi_task(TaskHandle_t handle) {
  for (int i = 0; i < wifi_task_count; i++) {
    if (wifi_tasks[i].handle == handle) {
      wifi_tasks[i].running = false;
      wifi_tasks[i].handle = NULL;

      // Compactar array
      for (int j = i; j < wifi_task_count - 1; j++) {
        wifi_tasks[j] = wifi_tasks[j + 1];
      }
      wifi_task_count--;
      break;
    }
  }
}

// ============= LVGL SEGURO =============
void lvgl_safe_call(void (*func)()) {
  if (!lvgl_mutex)
    lvgl_mutex = xSemaphoreCreateMutex();
  if (!lvgl_mutex)
    return;

  if (xSemaphoreTake(lvgl_mutex, pdMS_TO_TICKS(100))) {
    if (ui_initialized) {
      func();
    }
    xSemaphoreGive(lvgl_mutex);
  }
}

void lvgl_safe_call_with_arg(void (*func)(void *), void *arg) {
  if (!lvgl_mutex)
    lvgl_mutex = xSemaphoreCreateMutex();
  if (!lvgl_mutex)
    return;

  if (xSemaphoreTake(lvgl_mutex, pdMS_TO_TICKS(100))) {
    if (ui_initialized) {
      func(arg);
    }
    xSemaphoreGive(lvgl_mutex);
  }
}

void setup_wifi_scroll_behavior() {
  // Deshabilitar scroll en contenedores de WiFi
  if (ui_PanelWifi) {
    lv_obj_clear_flag(ui_PanelWifi, LV_OBJ_FLAG_SCROLLABLE);
  }

  // Solo habilitar scroll vertical en la lista de WiFi
  if (ui_WifiList) {
    lv_obj_set_scroll_dir(ui_WifiList, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(ui_WifiList, LV_SCROLLBAR_MODE_AUTO);
  }
}

// Callback mejorado para progreso
void updateProgressCallback(void *user_data) {
  ProgressData *data = (ProgressData *)user_data;
  if (data->bar && lv_obj_is_valid(data->bar)) {
    lv_bar_set_value(data->bar, data->value, LV_ANIM_ON);
  }
  // NO liberar la memoria aquí - puede causar problemas con LVGL
}

// Timer para actualizar progreso de forma segura
static void progress_timer_cb(lv_timer_t *timer) {
  if (currentProgressBar && lv_obj_is_valid(currentProgressBar)) {
    lv_bar_set_value(currentProgressBar, currentProgress, LV_ANIM_ON);
  }
}

void otaTask(void *arg) {
  OTAArgs *args = (OTAArgs *)arg;
  const char *binPath = args->binPath;
  lv_obj_t *progressBar = args->progressBar;

  // Configurar variables globales para el progreso
  currentProgressBar = progressBar;
  currentProgress = 0;

  // Crear timer para actualizaciones seguras
  lv_timer_t *progress_timer = lv_timer_create(progress_timer_cb, 100, NULL);
  lv_timer_pause(progress_timer); // Pausar inicialmente

  Serial.printf("🎯 Iniciando Juego desde: %s\n", binPath);
  lv_label_set_text(ui_Label6, "Verificando archivo...");

  File binFile;
  const esp_partition_t *otaPart;

  // Manejo centralizado de errores
  auto failAndReturn = [&](const char *msg) {
    lv_label_set_text(ui_Label6, msg);
    Serial.println(msg);
    if (binFile)
      binFile.close();
    lv_timer_del(progress_timer);
    currentProgressBar = NULL;
    free(args);
    vTaskDelay(1000 / portTICK_PERIOD_MS); // Mostrar mensaje
    ui_Screen1_screen_init();              // Volver al menú principal
    lv_scr_load(ui_Screen1);
    vTaskDelete(NULL);
  };

  // PASO 1: VERIFICAR ARCHIVO
  if (!SPIFFS.exists(binPath)) {
    failAndReturn("Archivo no encontrado");
    return;
  }

  binFile = SPIFFS.open(binPath, FILE_READ);
  if (!binFile) {
    failAndReturn("Error abriendo archivo");
    return;
  }

  size_t binSize = binFile.size();
  Serial.printf("📦 Tamaño binario: %d bytes\n", binSize);

  // PASO 2: OBTENER PARTICION
  otaPart = esp_ota_get_next_update_partition(NULL);
  if (!otaPart) {
    failAndReturn("Error: No hay particion OTA");
    return;
  }

  Serial.printf("🔧 Particion: %s (0x%x, %d bytes)\n", otaPart->label,
                otaPart->address, otaPart->size);

  // PASO 3: Preparar partición (opcional)
  lv_label_set_text(ui_Label6, "Preparando particion...");
  vTaskDelay(100 / portTICK_PERIOD_MS);

  // PASO 4: INICIAR UPDATE
  lv_label_set_text(ui_Label6, "Cargando Juego...");
  if (!Update.begin(binSize)) {
    failAndReturn("Error iniciando OTA");
    return;
  }

  uint8_t buf[1024];
  size_t written = 0;
  int lastReportedProgress = -1;

  lv_timer_resume(progress_timer);

  // PASO 5: COPIAR DATOS
  while (binFile.available()) {
    size_t r = binFile.read(buf, sizeof(buf));
    if (r == 0)
      break;

    size_t w = Update.write(buf, r);
    if (w != r) {
      failAndReturn("Escritura fallida");
      return;
    }
    written += w;

    int perc = (written * 100) / binSize;
    currentProgress = perc;

    if (perc != lastReportedProgress) {
      Serial.printf("📊 Progreso: %d%% (%d/%d bytes)\n", perc, written,
                    binSize);
      lastReportedProgress = perc;
    }

    if (millis() - lastProgressUpdate > 50) {
      vTaskDelay(2 / portTICK_PERIOD_MS);
      lastProgressUpdate = millis();
    }
  }

  binFile.close();
  lv_timer_pause(progress_timer);

  // PASO 6: FINALIZAR
  lv_label_set_text(ui_Label6, "Finalizando...");
  currentProgress = 100;
  progress_timer_cb(NULL);

  if (!Update.end()) {
    failAndReturn("OTA Fallo");
    return;
  }

  Serial.println("🎉 Juego Cargado!");
  lv_label_set_text(ui_Label6, "Juego Cargado");
  vTaskDelay(500 / portTICK_PERIOD_MS);

  if (esp_ota_set_boot_partition(otaPart) != ESP_OK) {
    failAndReturn("Error configurando boot");
    return;
  }

  lv_label_set_text(ui_Label6, "Reiniciando...");
  vTaskDelay(2000 / portTICK_PERIOD_MS);
  esp_restart();
}

void tryLoadGameFromSPIFFS(const char *binPath, lv_obj_t *progressBar) {
  ui_LoadingScreen_screen_init();
  lv_scr_load(ui_LoadingScreen);

  // Crear estructura OTAArgs correctamente
  static OTAArgs args;
  args.binPath = binPath;
  args.progressBar = progressBar;

  if (xTaskCreate(otaTask, "OTA", 8192, &args, 1, NULL) != pdPASS) {
    Serial.println("❌ Error al crear OTA task");
  }
}

bool readTouch() {
  Point p = input.getTouch(SCREEN_WIDTH, SCREEN_HEIGHT);
  touchPoint.x = p.x;
  touchPoint.y = p.y;
  touchPoint.touched = p.touched;
  return p.touched;
}

// ============= CALLBACKS LVGL =============
void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area,
                   lv_color_t *color_p) {
  uint32_t w = (area->x2 - area->x1 + 1);
  uint32_t h = (area->y2 - area->y1 + 1);

  tft.startWrite();
  tft.setAddrWindow(area->x1, area->y1, w, h);
  tft.pushColors((uint16_t *)&color_p->full, w * h, true);
  tft.endWrite();

  lv_disp_flush_ready(disp);
}

void my_touchpad_read(lv_indev_drv_t *indev_driver, lv_indev_data_t *data) {
  if (readTouch()) {
    data->state = LV_INDEV_STATE_PR;
    data->point.x = touchPoint.x;
    data->point.y = touchPoint.y;
  } else {
    data->state = LV_INDEV_STATE_REL;
  }
}

// Estilo para el foco
static lv_style_t style_focus;

void init_focus_style() {
  lv_style_init(&style_focus);
  lv_style_set_outline_width(&style_focus, 2);
  lv_style_set_outline_color(&style_focus, lv_palette_main(LV_PALETTE_BLUE));
  lv_style_set_outline_pad(&style_focus, 2);
  lv_style_set_border_width(&style_focus, 0);
  // lv_style_set_border_color(&style_focus, lv_palette_main(LV_PALETTE_CYAN));
}

// ============= LÓGICA DE NAVEGACIÓN Y UI =============

lv_obj_t *lastFocusedGame = NULL;
static uint32_t lastSectionSwitch = 0;

// Determinar en qué sección está el objeto
// 0: Top (Perfil), 1: Middle (Juegos), 2: Bottom (Ajustes)
int get_section(lv_obj_t *obj) {
  if (!obj)
    return -1;
  lv_obj_t *parent = lv_obj_get_parent(obj);
  if (!parent)
    return -1;

  // Top: ui_Container1 o sus hijos directos
  if (parent == ui_Container1)
    return 0;

  // Middle: ui_Container2 es el abuelo de los botones de juegos (Container2 ->
  // ContainerX -> Button)
  lv_obj_t *grandparent = lv_obj_get_parent(parent);
  if (grandparent == ui_Container2)
    return 1;

  // Bottom: ui_Panel2
  if (parent == ui_Panel2)
    return 2;

  return -1;
}

bool navigate_sections(int dir) {
  // Debounce para evitar saltos rápidos
  if (millis() - lastSectionSwitch < 500)
    return false;

  lv_group_t *g = lv_group_get_default();
  lv_obj_t *focused = lv_group_get_focused(g);
  int section = get_section(focused);

  if (section == -1)
    return false;

  bool switched = false;

  if (dir == INPUT_DIR_UP) {
    if (section == 2) { // Bottom -> Middle
      if (lastFocusedGame && lv_obj_is_valid(lastFocusedGame)) {
        lv_group_focus_obj(lastFocusedGame);
      } else {
        lv_group_focus_obj(ui_ImgButton1); // Default a Pacman
      }
      switched = true;
    } else if (section == 1) {       // Middle -> Top
      lv_group_focus_obj(ui_Image2); // Perfil
      switched = true;
    }
  } else if (dir == INPUT_DIR_DOWN) {
    if (section == 0) { // Top -> Middle
      if (lastFocusedGame && lv_obj_is_valid(lastFocusedGame)) {
        lv_group_focus_obj(lastFocusedGame);
      } else {
        lv_group_focus_obj(ui_ImgButton1);
      }
      switched = true;
    } else if (section == 1) {            // Middle -> Bottom
      lv_group_focus_obj(ui_ImgButton10); // Ajustes (centro)
      switched = true;
    }
  }

  if (switched) {
    lastSectionSwitch = millis();
    return true;
  }

  return false;
}

void game_focus_cb(lv_event_t *e) {
  lv_event_code_t code = lv_event_get_code(e);
  lv_obj_t *btn = lv_event_get_target(e);

  // El label es el hermano anterior (child 0, btn es child 1)
  lv_obj_t *parent = lv_obj_get_parent(btn);
  lv_obj_t *label = lv_obj_get_child(parent, 0);

  if (code == LV_EVENT_FOCUSED) {
    if (label && lv_obj_check_type(label, &lv_label_class)) {
      lv_obj_clear_flag(label, LV_OBJ_FLAG_HIDDEN);
    }
    lastFocusedGame = btn;
    // Scroll el contenedor PADRE (ui_Container3, etc) para centrar todo el
    // bloque del juego
    lv_obj_scroll_to_view(parent, LV_ANIM_ON);
  } else if (code == LV_EVENT_DEFOCUSED) {
    if (label && lv_obj_check_type(label, &lv_label_class)) {
      lv_obj_add_flag(label, LV_OBJ_FLAG_HIDDEN);
    }
  }
}

void setup_ui_logic() {
  // 1. Configurar Container de Juegos (Scroll natural, sin snapping agresivo)
  if (ui_Container2) {
    lv_obj_clear_flag(ui_Container2,
                      LV_OBJ_FLAG_SCROLL_ONE); // Permitir scroll libre
    lv_obj_set_scroll_snap_x(
        ui_Container2,
        LV_SCROLL_SNAP_NONE); // Desactivar snap para evitar saltos
  }

  // 2. Configurar Juegos (Ocultar nombres, quitar borde default y agregar
  // callbacks)
  lv_obj_t *game_btns[] = {ui_ImgButton1, ui_ImgButton2, ui_ImgButton3,
                           ui_ImgButton4, ui_ImgButton5};
  for (lv_obj_t *btn : game_btns) {
    if (btn) {
      // Quitar borde azul por defecto (solo aparecerá al enfocar con
      // style_focus)
      lv_obj_set_style_outline_width(btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

      lv_obj_add_event_cb(btn, game_focus_cb, LV_EVENT_FOCUSED, NULL);
      lv_obj_add_event_cb(btn, game_focus_cb, LV_EVENT_DEFOCUSED, NULL);

      // Ocultar label inicialmente
      lv_obj_t *parent = lv_obj_get_parent(btn);
      lv_obj_t *label = lv_obj_get_child(parent, 0);
      if (label && lv_obj_check_type(label, &lv_label_class)) {
        lv_obj_add_flag(label, LV_OBJ_FLAG_HIDDEN);
      }
    }
  }

  // 3. Configurar Top Section (Perfil)
  if (ui_Image2) {
    lv_obj_add_flag(ui_Image2, LV_OBJ_FLAG_CLICKABLE);
  }

  // 4. Asegurar que contenedores no sean seleccionables
  if (ui_Panel2)
    lv_obj_clear_flag(ui_Panel2, LV_OBJ_FLAG_CLICKABLE);
  if (ui_Container1)
    lv_obj_clear_flag(ui_Container1, LV_OBJ_FLAG_CLICKABLE);
  if (ui_Container2)
    lv_obj_clear_flag(ui_Container2, LV_OBJ_FLAG_CLICKABLE);
}

void my_keypad_read(lv_indev_drv_t *indev_drv, lv_indev_data_t *data) {
  JoystickInput joy = input.getJoystick();
  ButtonInput btn = input.getButtons();

  data->state = LV_INDEV_STATE_REL;

  if (joy.active) {
    data->state = LV_INDEV_STATE_PR;
    switch (joy.direction) {
    // Mapear direcciones a navegación de foco (Next/Prev)
    // Grupos LVGL son listas lineales por defecto
    case INPUT_DIR_UP:
      // Navegación por secciones
      if (navigate_sections(INPUT_DIR_UP)) {
        data->state =
            LV_INDEV_STATE_REL; // Consumir evento si se navegó por secciones
      } else {
        data->key = LV_KEY_PREV; // Fallback para menús/popups
      }
      break;
    case INPUT_DIR_LEFT:
      data->key = LV_KEY_PREV;
      break;

    case INPUT_DIR_DOWN:
      // Navegación por secciones
      if (navigate_sections(INPUT_DIR_DOWN)) {
        data->state =
            LV_INDEV_STATE_REL; // Consumir evento si se navegó por secciones
      } else {
        data->key = LV_KEY_NEXT; // Fallback para menús/popups
      }
      break;
    case INPUT_DIR_RIGHT:
      data->key = LV_KEY_NEXT;
      break;

    default:
      data->state = LV_INDEV_STATE_REL;
      break;
    }
  } else if (btn.aPressed) {
    data->state = LV_INDEV_STATE_PR;
    data->key = LV_KEY_ENTER;
  } else if (btn.bPressed) {
    data->state = LV_INDEV_STATE_PR;
    data->key = LV_KEY_ESC;
  }
}

// Función recursiva para agregar objetos interactivos al grupo
void add_to_group_recursive(lv_obj_t *parent, lv_group_t *group) {
  // Verificar si es clickeable (botones, etc)
  if (lv_obj_has_flag(parent, LV_OBJ_FLAG_CLICKABLE)) {
    Serial.printf("DEBUG: Objeto añadido al grupo %p\n", parent);
    lv_group_add_obj(group, parent);

    // Forzar estilo de foco para asegurar visibilidad
    lv_obj_add_style(parent, &style_focus, LV_STATE_FOCUS_KEY);
    lv_obj_add_style(parent, &style_focus, LV_STATE_FOCUSED);
  }

  uint32_t child_cnt = lv_obj_get_child_cnt(parent);
  for (uint32_t i = 0; i < child_cnt; i++) {
    add_to_group_recursive(lv_obj_get_child(parent, i), group);
  }
}

// ============= INICIALIZACIÓN SIMPLIFICADA =============
void initDisplay() {
  Serial.println("Inicializando pantalla ST7796...");

  // Configurar pines CS
  pinMode(TFT_CS, OUTPUT);
  pinMode(SD_CS, OUTPUT);

  // Desactivar ambos dispositivos inicialmente
  digitalWrite(TFT_CS, HIGH);
  digitalWrite(SD_CS, HIGH);
  delay(100);

  // Activar solo la pantalla
  digitalWrite(TFT_CS, LOW);

  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);

  // Inicializar pantalla
  tft.begin();
  tft.setRotation(3);
  tft.fillScreen(TFT_BLACK);

  Serial.println("Pantalla OK");
}

bool initSD() {
  Serial.println("Inicializando microSD...");

  // Método SUPER SIMPLE - solo cambiar CS
  digitalWrite(TFT_CS, HIGH); // Desactivar pantalla
  digitalWrite(SD_CS, LOW);   // Activar SD
  delay(50);

  Serial.println("Intentando inicializar SD...");

  // Intentar inicialización simple
  bool sdOk = SD.begin(SD_CS);

  if (!sdOk) {
    Serial.println("ERROR: Fallo SD.begin() simple");

    // Intentar con SPI explícito
    Serial.println("Intentando con SPI explícito...");
    sdOk = SD.begin(SD_CS, SPI, 10000000);
  }

  if (!sdOk) {
    // Último intento con frecuencia más baja
    Serial.println("Intentando con frecuencia baja...");
    for (int freq = 5000000; freq >= 1000000; freq -= 1000000) {
      Serial.printf("Probando %d Hz... ", freq);
      if (SD.begin(SD_CS, SPI, freq)) {
        sdOk = true;
        Serial.println("OK");
        break;
      }
      Serial.println("Fallo");
      delay(50);
    }
  }

  if (sdOk) {
    uint8_t cardType = SD.cardType();
    if (cardType == CARD_NONE) {
      Serial.println("ERROR: No hay tarjeta insertada");
      sdOk = false;
    } else {
      Serial.print("Tipo de tarjeta: ");
      switch (cardType) {
      case CARD_MMC:
        Serial.println("MMC");
        break;
      case CARD_SD:
        Serial.println("SDSC");
        break;
      case CARD_SDHC:
        Serial.println("SDHC");
        break;
      default:
        Serial.println("Desconocido");
        break;
      }

      uint64_t cardSize = SD.cardSize() / (1024 * 1024);
      Serial.printf("Tamaño: %llu MB\n", cardSize);
      sdInitialized = true;
      Serial.println("✓ microSD inicializada correctamente");
    }
  } else {
    Serial.println("ERROR: No se pudo inicializar microSD");
  }

  // Siempre restaurar pantalla
  digitalWrite(SD_CS, HIGH);
  digitalWrite(TFT_CS, LOW);

  return sdOk;
}

bool testSD() {
  if (!sdInitialized) {
    Serial.println("SD no inicializada - omitiendo test");
    return false;
  }

  Serial.println("Probando lectura/escritura en SD...");

  // Activar SD y desactivar pantalla
  digitalWrite(TFT_CS, HIGH);
  digitalWrite(SD_CS, LOW);
  delay(10);

  bool testResult = false;

  // Escribir archivo de prueba
  File file = SD.open("/test.txt", FILE_WRITE);
  if (file) {
    file.println("Test ESP32-S3 - " + String(millis()));
    file.close();
    Serial.println("✓ Archivo escrito");

    // Leer archivo
    file = SD.open("/test.txt");
    if (file) {
      Serial.println("Contenido del test:");
      while (file.available()) {
        Serial.write(file.read());
      }
      file.close();
      testResult = true;
      Serial.println("✓ Test SD exitoso");
    } else {
      Serial.println("ERROR: No se pudo leer archivo");
    }
  } else {
    Serial.println("ERROR: No se pudo crear archivo");
  }

  // Restaurar pantalla
  digitalWrite(SD_CS, HIGH);
  digitalWrite(TFT_CS, LOW);

  return testResult;
}

void initTouch() {
  Serial.println("Inicializando Entrada (Touch + Joystick + Botones)...");
  input.begin();

  // Verificación opcional del táctil
  Wire.beginTransmission(FT6336_ADDR);
  if (Wire.endTransmission() == 0) {
    Serial.println("Táctil OK");
  } else {
    Serial.println("ERROR: Táctil no detectado");
  }
}

void initLVGL() {
  Serial.println("Inicializando LVGL...");

  lv_init();
  lv_disp_draw_buf_init(&draw_buf, buf1, buf2, DRAW_BUF_SIZE);

  lv_disp_drv_init(&disp_drv);
  disp_drv.hor_res = SCREEN_WIDTH;
  disp_drv.ver_res = SCREEN_HEIGHT;
  disp_drv.flush_cb = my_disp_flush;
  disp_drv.draw_buf = &draw_buf;
  disp_drv.full_refresh = 0;
  lv_disp_drv_register(&disp_drv);

  lv_indev_drv_init(&indev_drv);
  indev_drv.type = LV_INDEV_TYPE_POINTER;
  indev_drv.read_cb = my_touchpad_read;
  lv_indev_drv_register(&indev_drv);

  // Registrar Keypad (Joystick + Botones)
  lv_indev_drv_init(&indev_keypad_drv);
  indev_keypad_drv.type = LV_INDEV_TYPE_KEYPAD;
  indev_keypad_drv.read_cb = my_keypad_read;
  lv_indev_t *indev_keypad = lv_indev_drv_register(&indev_keypad_drv);

  // Crear grupo por defecto para navegación
  lv_group_t *g = lv_group_create();
  lv_group_set_default(g);
  lv_indev_set_group(indev_keypad, g);

  Serial.println("LVGL OK");
}

// ============= SETUP SIMPLIFICADA =============
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n=================================");
  Serial.println("ESP32-S3 ST7796 - SOLUCIÓN SEGURA");
  Serial.println("=================================\n");

  // 1. Inicializar mutexes primero
  lvgl_mutex = xSemaphoreCreateMutex();
  wifi_mutex = xSemaphoreCreateMutex();
  wifi_status_mutex = xSemaphoreCreateMutex();
  popup_event_queue = xQueueCreate(10, sizeof(PopupEvent));

  if (!lvgl_mutex || !wifi_mutex || !wifi_status_mutex || !popup_event_queue) {
    Serial.println("ERROR: No se pudieron crear recursos de sincronización");
    while (1)
      delay(1000);
  }

  // 2. Pantalla
  initDisplay();

  Serial.println("Montando SPIFFS...");
  if (!SPIFFS.begin(true)) {
    Serial.println("❌ ERROR: No se pudo montar SPIFFS");
  } else {
    Serial.println("✔ SPIFFS montado correctamente");
  }

  // 3. Componentes
  initTouch();
  initLVGL();

  Serial.println("Memoria libre: " + String(ESP.getFreeHeap()) + " bytes");
  Serial.println("Cargando UI...");

  // 4. UI con protección
  if (xSemaphoreTake(lvgl_mutex, pdMS_TO_TICKS(200))) {
    ui_init();
    setup_ui_logic();
    init_focus_style();
    setup_wifi_scroll_behavior();
    ui_initialized = true;
    xSemaphoreGive(lvgl_mutex);
  }

  // 5. Navegación
  lv_group_t *g = lv_group_get_default();
  if (g && ui_Screen1) {
    if (xSemaphoreTake(lvgl_mutex, pdMS_TO_TICKS(100))) {
      lv_group_set_wrap(g, true);
      Serial.println("Agregando objetos de UI al grupo de navegación...");
      add_to_group_recursive(ui_Screen1, g);

      if (lv_group_get_obj_count(g) > 0) {
        lv_group_focus_next(g);
        Serial.println("Foco inicial establecido");
      }
      xSemaphoreGive(lvgl_mutex);
    }
  }

  // 6. I2C y sensores
  Wire.begin(TOUCH_SDA, TOUCH_SCL);
  if (!ina219.begin()) {
    Serial.println("Fallo al encontrar INA219");
    while (1)
      delay(1000);
  }
  ina219.setCalibration_32V_2A();

  // 7. WiFi (solo modo STA)
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);

  Serial.println("Sistema completamente inicializado");
  Serial.println("Memoria final: " + String(ESP.getFreeHeap()) + " bytes");

  // 8. Tarea de limpieza periódica
  xTaskCreate(memory_cleanup_task, "MemClean", 2048, NULL, 1, NULL);
}

// ============= FUNCIONES SEGURAS PARA SD =============
bool operacionSeguraSD(std::function<bool()> operacion) {
  if (!sdInitialized)
    return false;

  // Activar SD y desactivar pantalla
  digitalWrite(TFT_CS, HIGH);
  digitalWrite(SD_CS, LOW);
  delay(5);

  bool resultado = operacion();

  // Restaurar pantalla
  digitalWrite(SD_CS, HIGH);
  digitalWrite(TFT_CS, LOW);
  delay(5);

  return resultado;
}

bool guardarEnSD(const char *filename, const char *data) {
  return operacionSeguraSD([&]() {
    File file = SD.open(filename, FILE_WRITE);
    if (!file)
      return false;
    file.print(data);
    file.close();
    return true;
  });
}

String leerDeSD(const char *filename) {
  String contenido = "";
  operacionSeguraSD([&]() {
    File file = SD.open(filename);
    if (!file)
      return false;
    while (file.available()) {
      contenido += (char)file.read();
    }
    file.close();
    return true;
  });
  return contenido;
}

bool guardarEnSPIFFS(const char *path, const char *data) {
  File file = SPIFFS.open(path, FILE_WRITE);
  if (!file)
    return false;
  file.print(data);
  file.close();
  return true;
}

String leerDeSPIFFS(const char *path) {
  File file = SPIFFS.open(path, FILE_READ);
  if (!file)
    return "";
  String contenido = "";
  while (file.available())
    contenido += (char)file.read();
  file.close();
  return contenido;
}

// Función para filtrar el voltaje (media móvil)
float smooth_voltage(float new_voltage) {
  voltage_history[voltage_index] = new_voltage;
  voltage_index = (voltage_index + 1) % VOLTAGE_SAMPLES;

  float sum = 0;
  int valid_samples = 0;
  for (int i = 0; i < VOLTAGE_SAMPLES; i++) {
    if (voltage_history[i] > 0) {
      sum += voltage_history[i];
      valid_samples++;
    }
  }

  if (valid_samples > 0) {
    smoothed_voltage = sum / valid_samples;
  }

  return smoothed_voltage;
}

// Función para determinar el estado de la batería
BatteryState determine_battery_state(float current_mA) {
  static BatteryState last_state = BATTERY_RESTING;
  static unsigned long last_change = 0;

  BatteryState new_state;

  if (current_mA < current_threshold_charging) {
    new_state = BATTERY_CHARGING;
  } else if (current_mA > current_threshold_discharging) {
    new_state = BATTERY_DISCHARGING;
  } else {
    new_state = BATTERY_RESTING;
  }

  // Evitar cambios bruscos de estado
  if (new_state != last_state) {
    if (millis() - last_change > MIN_STATE_TIME) {
      last_state = new_state;
      last_change = millis();
      state_change_time = millis();
      Serial.printf("Cambio de estado de batería: %d -> %d\n", battery_state,
                    new_state);
    }
  } else {
    last_change = millis();
  }

  return last_state;
}

// SOC basado en voltaje (solo para calibración y estados extremos)
float calculate_soc_from_voltage(float voltage) {
  // Curva de descarga típica LiPo (ajustar según tu batería)
  if (voltage >= 4.20f)
    return 100.0f;
  if (voltage >= 4.15f)
    return 95.0f;
  if (voltage >= 4.10f)
    return 90.0f;
  if (voltage >= 4.05f)
    return 80.0f;
  if (voltage >= 4.00f)
    return 70.0f;
  if (voltage >= 3.95f)
    return 60.0f;
  if (voltage >= 3.90f)
    return 50.0f;
  if (voltage >= 3.85f)
    return 40.0f;
  if (voltage >= 3.80f)
    return 30.0f;
  if (voltage >= 3.75f)
    return 20.0f;
  if (voltage >= 3.70f)
    return 15.0f;
  if (voltage >= 3.65f)
    return 10.0f;
  if (voltage >= 3.60f)
    return 5.0f;
  if (voltage >= 3.50f)
    return 2.0f;
  if (voltage >= 3.40f)
    return 0.0f;
  if (voltage >= 3.30f)
    return 0.0f;
  return 0.0f;
}

// SOC basado en Coulomb Counting (integración de corriente)
float calculate_soc_from_coulomb(float current_mA, unsigned long dt_ms,
                                 BatteryState state) {
  static float remaining_capacity_mAh =
      BAT_CAPACITY_mAh * 0.5f; // Asumir 50% inicial
  static float max_capacity = BAT_CAPACITY_mAh;

  float dt_hours = dt_ms / 3600000.0f;
  float delta_mAh = current_mA * dt_hours;

  // Actualizar capacidad restante según el estado
  switch (state) {
  case BATTERY_DISCHARGING:
    // Solo contar cuando hay carga significativa
    if (current_mA > 20.0f) {
      remaining_capacity_mAh -= delta_mAh;
      if (remaining_capacity_mAh < 0)
        remaining_capacity_mAh = 0;
    }
    break;

  case BATTERY_CHARGING:
    // Solo contar cuando hay carga significativa
    if (current_mA < -20.0f) {
      remaining_capacity_mAh += -delta_mAh; // delta es negativo al cargar
      if (remaining_capacity_mAh > max_capacity)
        remaining_capacity_mAh = max_capacity;
    }
    break;

  case BATTERY_RESTING:
    // No hacer nada, solo pequeñas correcciones por voltaje
    break;
  }

  // Calcular porcentaje
  float soc = (remaining_capacity_mAh / max_capacity) * 100.0f;

  // Limitar entre 0 y 100%
  if (soc > 100.0f)
    soc = 100.0f;
  if (soc < 0.0f)
    soc = 0.0f;

  return soc;
}

// --------------------------------------------------
// Actualización de información de batería con Coulomb Counting
void update_battery_info() {
  unsigned long now = millis();
  unsigned long dt_ms = now - last_update_time;

  if (dt_ms < 1000)
    return; // Actualizar cada segundo aproximadamente

  last_update_time = now;

  // --- Lecturas del INA219 ---
  float shunt_mV = ina219.getShuntVoltage_mV();
  float bus_V = ina219.getBusVoltage_V();
  float current_mA =
      ina219.getCurrent_mA(); // Positiva = descarga, Negativa = carga

  // Voltaje real de la batería
  float battery_voltage = bus_V + (shunt_mV / 1000.0f);

  // Filtrar voltaje
  float filtered_voltage = smooth_voltage(battery_voltage);

  // --- Determinar estado de la batería ---
  battery_state = determine_battery_state(current_mA);

  // --- Calcular SOC por voltaje (solo para referencia y calibración) ---
  float soc_voltage = calculate_soc_from_voltage(filtered_voltage);
  last_soc_voltage = soc_voltage;

  // --- Calcular SOC por Coulomb Counting ---
  float soc_coulomb =
      calculate_soc_from_coulomb(current_mA, dt_ms, battery_state);
  last_soc_coulomb = soc_coulomb;

  // --- Combinar ambos métodos inteligentemente ---
  float final_soc = soc_coulomb; // Principalmente usar coulomb counting

  // Correcciones basadas en voltaje en estados extremos
  if (battery_state == BATTERY_RESTING) {
    // En reposo, confiar más en el voltaje
    if (abs(soc_voltage - soc_coulomb) > 20.0f) {
      // Gran discrepancia, corregir suavemente
      final_soc = final_soc * 0.7f + soc_voltage * 0.3f;
    }
  }

  // Corrección en voltajes extremos
  if (filtered_voltage >= BATTERY_FULL_VOLTAGE - 0.05f) {
    // Casi llena, forzar 100%
    final_soc = 100.0f;
    // Resetear coulomb counting
    total_current_mAh = 0;
  } else if (filtered_voltage <= BATTERY_CUTOFF_VOLTAGE + 0.1f) {
    // Casi vacía, forzar 0% si estamos descargando
    if (battery_state == BATTERY_DISCHARGING) {
      final_soc = 0.0f;
    }
  }

  // Suavizar cambios bruscos
  static float last_reported_soc = 50.0f;
  float max_change_per_sec = 1.0f; // Máximo 1% por segundo
  float max_change = max_change_per_sec * (dt_ms / 1000.0f);

  if (abs(final_soc - last_reported_soc) > max_change) {
    if (final_soc > last_reported_soc) {
      final_soc = last_reported_soc + max_change;
    } else {
      final_soc = last_reported_soc - max_change;
    }
  }

  last_reported_soc = final_soc;

  // --- Determinar si está cargando (para el TP4056) ---
  bool is_charging = (current_mA < -50.0f); // Corriente negativa mayor a 50mA

  // --- Determinar color de la batería ---
  lv_color_t battery_color;
  if (final_soc > 50.0f) {
    battery_color = lv_color_hex(0x00FF00); // Verde
  } else if (final_soc > 20.0f) {
    battery_color = lv_color_hex(0xFFA500); // Naranja
  } else {
    battery_color = lv_color_hex(0xFF0000); // Rojo
  }

  if (is_charging) {
    battery_color = lv_color_hex(0x00BFFF); // Azul claro para carga
  }

  // --- Actualizar UI ---
  if (xSemaphoreTake(lvgl_mutex, pdMS_TO_TICKS(50))) {
    if (ui_Bar1 && lv_obj_is_valid(ui_Bar1)) {
      lv_bar_set_value(ui_Bar1, (int)final_soc, LV_ANIM_ON);
      lv_obj_set_style_bg_color(ui_Bar1, battery_color,
                                LV_PART_INDICATOR | LV_STATE_DEFAULT);
    }

    if (ui_Bar4 && lv_obj_is_valid(ui_Bar4)) {
      lv_bar_set_value(ui_Bar4, (int)final_soc, LV_ANIM_ON);
      lv_obj_set_style_bg_color(ui_Bar4, battery_color,
                                LV_PART_INDICATOR | LV_STATE_DEFAULT);
    }

    if (ui_LabelDebug && lv_obj_is_valid(ui_LabelDebug)) {
      String state_str;
      switch (battery_state) {
      case BATTERY_CHARGING:
        state_str = "CARGANDO";
        break;
      case BATTERY_DISCHARGING:
        state_str = "DESCARGANDO";
        break;
      case BATTERY_RESTING:
        state_str = "REPOSO";
        break;
      }

      String debugText = "Batería: " + String(final_soc, 1) + "%\n" +
                         "Voltaje: " + String(filtered_voltage, 2) + "V\n" +
                         "Corriente: " + String(current_mA, 1) + "mA\n" +
                         "Estado: " + state_str + "\n" +
                         "SOC Voltaje: " + String(soc_voltage, 1) + "%\n" +
                         "SOC Coulomb: " + String(soc_coulomb, 1) + "%\n" +
                         (is_charging ? "⚡ CARGANDO (TP4056)" : "");

      lv_label_set_text(ui_LabelDebug, debugText.c_str());
    }

    xSemaphoreGive(lvgl_mutex);
  }

  // --- Debug por serial ---
  static unsigned long last_serial_log = 0;
  if (now - last_serial_log > 3000) {
    last_serial_log = now;

    Serial.println("========== BATERÍA ==========");
    Serial.printf("Voltaje: %.2fV (Filtrado: %.2fV)\n", battery_voltage,
                  filtered_voltage);
    Serial.printf("Corriente: %.1fmA\n", current_mA);
    Serial.printf("Estado: %s\n", battery_state == BATTERY_CHARGING ? "CARGANDO"
                                  : battery_state == BATTERY_DISCHARGING
                                      ? "DESCARGANDO"
                                      : "REPOSO");
    Serial.printf("SOC Final: %.1f%%\n", final_soc);
    Serial.printf("SOC Voltaje: %.1f%%\n", soc_voltage);
    Serial.printf("SOC Coulomb: %.1f%%\n", soc_coulomb);
    Serial.printf("Cargando: %s\n", is_charging ? "SI" : "NO");
    Serial.println("============================\n");
  }
}

// Función para calibrar la batería (llamar cuando esté completamente cargada)
void calibrate_battery_full() {
  Serial.println("Calibrando batería al 100%...");
  total_current_mAh = 0;
  // Aquí podrías guardar en EEPROM que está calibrada
}

// Función para calibrar la batería (llamar cuando esté completamente
// descargada)
void calibrate_battery_empty() {
  Serial.println("Calibrando batería al 0%...");
  total_current_mAh = BAT_CAPACITY_mAh;
  // Aquí podrías guardar en EEPROM que está calibrada
}

void loop() {
  unsigned long now = millis();

  // LVGL con protección
  if (xSemaphoreTake(lvgl_mutex, pdMS_TO_TICKS(10))) {
    lv_timer_handler();
    xSemaphoreGive(lvgl_mutex);
  }

  process_popup_events();

  // Wakeup táctil
  checkTouchWakeup();

  // Actualizar batería
  if (now - lastBatteryUpdate >= BATTERY_UPDATE_INTERVAL) {
    lastBatteryUpdate = now;
    update_battery_info();
  }

  // Actualizar WiFi UI
  update_wifi_ui_safe();

  // Limpieza periódica
  if (now % 5000 < 10) { // Cada ~5 segundos
    print_memory_info();
  }
  delay(1);
}

// ============= FUNCIONES DE ENERGÍA CORREGIDAS =============

void restart_console(void) {
  Serial.println("🔄 Reiniciando consola...");

  // Mostrar mensaje final
  lv_label_set_text(ui_Label6, "Reiniciando...");
  lv_timer_handler();
  delay(500);

  ESP.restart();
}

void shutdown_console(void) {
  Serial.println("🔌 Apagando consola...");

  // 1. Mostrar mensaje de apagado
  lv_label_set_text(ui_Label6, "Apagando...");
  lv_timer_handler();
  delay(200);

  // 2. Limpiar completamente la pantalla
  lv_obj_clean(lv_scr_act());

  // 3. Crear pantalla completamente negra
  lv_obj_t *bg = lv_obj_create(lv_scr_act());
  lv_obj_set_size(bg, SCREEN_WIDTH, SCREEN_HEIGHT);
  lv_obj_set_style_bg_color(bg, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(bg, LV_OPA_COVER, 0);
  lv_obj_center(bg);

  // 4. Forzar múltiples actualizaciones
  for (int i = 0; i < 3; i++) {
    lv_timer_handler();
    delay(50);
  }

  Serial.println("Pantalla en negro...");

  // 5. Animación de apagado
  for (int i = 255; i >= 0; i -= 3) {
    analogWrite(TFT_BL, i);
    delay(30);
  }

  // 6. Resetear la pantalla (opcional)
  tft.writecommand(ST7796_DISPOFF); // Apagar display
  tft.writecommand(ST7796_SLPIN);   // Modo sleep

  // 7. Apagar backlight
  // digitalWrite(TFT_BL, LOW);

  delay(1000); // Esperar a que se complete el apagado

  Serial.println("✅ Consola apagada");

  // 8. Deep sleep
  esp_deep_sleep_start();
}

void checkTouchWakeup() {
  if (suspended && readTouch()) {
    wakeup_console();
  }
}

void suspend_console(void) {
  if (suspended)
    return; // Ya suspendida
  Serial.println("💤 Suspendiendo consola...");

  // Animación de atenuado
  for (int i = 255; i >= 50; i -= 10) {
    analogWrite(TFT_BL, i);
    delay(10);
  }

  // Marcar como suspendida
  suspended = true;

  // Opcional: pausar LVGL timers
  Serial.println("✅ Pantalla suspendida");
}

void wakeup_console(void) {
  if (!suspended)
    return; // No estaba suspendida
  Serial.println("🔛 Reanudando consola...");

  // Restaurar brillo gradualmente
  for (int i = 0; i <= 255; i += 5) {
    analogWrite(TFT_BL, i);
    delay(10);
  }

  suspended = false;
  Serial.println("✅ Consola activa");
}

// ============= WIFI LOGIC =============

static lv_obj_t *get_list_btn_label(lv_obj_t *btn) {
  if (!btn || !lv_obj_is_valid(btn))
    return NULL;

  // Los botones de lista LVGL tienen: icon (hijo 0) y label (hijo 1)
  uint32_t child_cnt = lv_obj_get_child_cnt(btn);
  if (child_cnt >= 2) {
    lv_obj_t *label = lv_obj_get_child(btn, 1);
    if (label && lv_obj_check_type(label, &lv_label_class)) {
      return label;
    }
  }
  return NULL;
}

// Función para limpiar memoria de la lista WiFi
static void wifi_list_cleanup(lv_obj_t *list) {
  if (!list || !lv_obj_is_valid(list))
    return;

  // Primero liberar toda la memoria de user_data
  uint32_t child_cnt = lv_obj_get_child_cnt(list);
  for (uint32_t i = 0; i < child_cnt; i++) {
    lv_obj_t *child = lv_obj_get_child(list, i);
    if (child && lv_obj_is_valid(child)) {
      char *user_data = (char *)lv_obj_get_user_data(child);
      if (user_data) {
        free(user_data);
        lv_obj_set_user_data(child, NULL);
      }
    }
  }

  // Esperar un poco para que LVGL procese las liberaciones
  vTaskDelay(5 / portTICK_PERIOD_MS);

  // Ahora limpiar visualmente
  lv_obj_clean(list);
}

static void update_wifi_list_safe(NetworkInfo *networks, int count) {
  if (!networks || count <= 0)
    return;

  if (xSemaphoreTake(wifi_status_mutex, pdMS_TO_TICKS(100))) {
    // Copiar datos
    pending_count = (count > 10) ? 10 : count;
    for (int i = 0; i < pending_count; i++) {
      strncpy(pending_networks[i].ssid, networks[i].ssid, 32);
      pending_networks[i].ssid[32] = '\0';
      pending_networks[i].rssi = networks[i].rssi;
      pending_networks[i].secure = networks[i].secure;
    }

    // Marcar para actualización
    wifi_update_pending = true;
    xSemaphoreGive(wifi_status_mutex);
  }
}

// Función helper para actualizar UI de forma segura (llamada desde loop)
void update_wifi_ui_safe() {
  if (!wifi_update_pending)
    return;

  static NetworkInfo networks[10];
  static int network_count = 0;

  // Copiar datos locales
  if (xSemaphoreTake(wifi_status_mutex, pdMS_TO_TICKS(100))) {
    network_count = pending_count;
    for (int i = 0; i < network_count; i++) {
      networks[i] = pending_networks[i];
    }
    wifi_update_pending = false;
    xSemaphoreGive(wifi_status_mutex);
  }

  if (network_count <= 0)
    return;

  // Actualizar UI con mutex LVGL
  if (xSemaphoreTake(lvgl_mutex, pdMS_TO_TICKS(100))) {
    if (ui_WifiList && lv_obj_is_valid(ui_WifiList)) {
      // Limpiar lista anterior de forma segura
      uint32_t child_cnt = lv_obj_get_child_cnt(ui_WifiList);
      for (uint32_t i = 0; i < child_cnt; i++) {
        lv_obj_t *child = lv_obj_get_child(ui_WifiList, i);
        if (child && lv_obj_is_valid(child)) {
          char *user_data = (char *)lv_obj_get_user_data(child);
          if (user_data) {
            free(user_data);
            lv_obj_set_user_data(child, NULL);
          }
        }
      }
      lv_obj_clean(ui_WifiList);

      // Añadir nuevas redes
      for (int i = 0; i < network_count; i++) {
        const char *signal_icon = LV_SYMBOL_WIFI;
        if (networks[i].rssi > -50)
          signal_icon = LV_SYMBOL_OK;
        else if (networks[i].rssi < -70)
          signal_icon = LV_SYMBOL_WARNING;

        char item_text[60];
        snprintf(item_text, sizeof(item_text), "%s%s\n%d dBm", networks[i].ssid,
                 networks[i].secure ? " *" : "", networks[i].rssi);

        lv_obj_t *item = lv_list_add_btn(ui_WifiList, signal_icon, item_text);
        if (item) {
          char *ssid_copy = (char *)malloc(strlen(networks[i].ssid) + 1);
          if (ssid_copy) {
            strcpy(ssid_copy, networks[i].ssid);
            lv_obj_set_user_data(item, ssid_copy);
          }
          lv_obj_add_event_cb(item, wifi_list_btn_cb, LV_EVENT_CLICKED, NULL);
        }
      }
    }
    xSemaphoreGive(lvgl_mutex);
  }

  network_count = 0;
}

// Helper para actualizar mensaje desde cualquier hilo
static void set_wifi_status(const char *icon, const char *msg) {
  if (!wifi_status_mutex) {
    wifi_status_mutex = xSemaphoreCreateMutex();
    if (!wifi_status_mutex) {
      Serial.println("ERROR: No se pudo crear wifi_status_mutex");
      return;
    }
  }

  // Intentar tomar el mutex con timeout más largo
  if (xSemaphoreTake(wifi_status_mutex, pdMS_TO_TICKS(500))) {
    wifi_status_icon = icon;

    // Limpiar el mensaje anterior
    memset(wifi_status_message, 0, sizeof(wifi_status_message));

    // Copiar nuevo mensaje (con seguridad)
    if (msg) {
      strncpy(wifi_status_message, msg, sizeof(wifi_status_message) - 1);
      wifi_status_message[sizeof(wifi_status_message) - 1] = '\0';
    } else {
      strcpy(wifi_status_message, "Unknown status");
    }

    wifi_update_pending = true;
    xSemaphoreGive(wifi_status_mutex);

    // Forzar actualización inmediata
    Serial.printf("WiFi Status Update: %s - %s\n", icon ? icon : "NULL",
                  wifi_status_message);

  } else {
    Serial.println(
        "WARNING: No se pudo actualizar estado WiFi (mutex ocupado)");
  }
}

// Callback para seleccionar red WiFi - VERSIÓN CORREGIDA
static void wifi_list_btn_cb(lv_event_t *e) {
  lv_obj_t *btn = lv_event_get_target(e);

  if (!btn || !lv_obj_is_valid(btn)) {
    Serial.println("ERROR: Invalid button in WiFi list callback");
    return;
  }

  // OBTENER SSID DESDE USER DATA (más seguro y simple)
  char *stored_ssid = (char *)lv_obj_get_user_data(btn);

  if (stored_ssid && strlen(stored_ssid) > 0) {
    // Usar el SSID almacenado directamente
    strncpy(selected_ssid, stored_ssid, sizeof(selected_ssid) - 1);
    selected_ssid[sizeof(selected_ssid) - 1] = '\0';

    Serial.printf("Selected SSID (from user_data): '%s'\n", selected_ssid);
    Serial.printf("SSID length: %d\n", strlen(selected_ssid));

    // También verificar en el label para depuración
    lv_obj_t *label = NULL;
    uint32_t child_cnt = lv_obj_get_child_cnt(btn);

    if (child_cnt >= 2) {
      label = lv_obj_get_child(btn, 1);
    } else if (child_cnt == 1) {
      label = lv_obj_get_child(btn, 0);
    }

    if (label && lv_obj_check_type(label, &lv_label_class)) {
      const char *txt = lv_label_get_text(label);
      Serial.printf("Label text for reference: %s\n", txt ? txt : "NULL");
    }
  } else {
    // Fallback: extraer del texto del label (por si acaso)
    Serial.println("WARNING: No user_data, falling back to text parsing");

    lv_obj_t *label = NULL;
    uint32_t child_cnt = lv_obj_get_child_cnt(btn);

    if (child_cnt >= 2) {
      // Ícono es hijo 0, label es hijo 1
      label = lv_obj_get_child(btn, 1);
    } else if (child_cnt == 1) {
      // Solo hay un hijo (probablemente el label)
      label = lv_obj_get_child(btn, 0);
    }

    if (!label || !lv_obj_check_type(label, &lv_label_class)) {
      Serial.println("ERROR: Label not found or invalid");
      return;
    }

    const char *txt = lv_label_get_text(label);
    if (!txt) {
      Serial.println("ERROR: Label text is NULL");
      return;
    }

    Serial.printf("Raw selection text: %s\n", txt);

    // Extraer SSID del texto
    char ssid_only[64];
    memset(ssid_only, 0, sizeof(ssid_only));
    strncpy(ssid_only, txt, sizeof(ssid_only) - 1);

    // Buscar y cortar en el primer salto de línea
    char *newline_ptr = strchr(ssid_only, '\n');
    if (newline_ptr) {
      *newline_ptr = '\0';
    }

    char *star_ptr =
        strstr(ssid_only, " *"); // Buscar " *" (espacio + asterisco)
    if (star_ptr) {
      *star_ptr = '\0';
    }

    // También buscar solo "*" al final
    int len = strlen(ssid_only);
    if (len > 0 && ssid_only[len - 1] == '*') {
      ssid_only[len - 1] = '\0';
      len--;
    }

    // Eliminar espacios al final
    while (len > 0 &&
           (ssid_only[len - 1] == ' ' || ssid_only[len - 1] == '\n' ||
            ssid_only[len - 1] == '\r')) {
      ssid_only[len - 1] = '\0';
      len--;
    }

    strncpy(selected_ssid, ssid_only, sizeof(selected_ssid) - 1);
    selected_ssid[sizeof(selected_ssid) - 1] = '\0';

    Serial.printf("Selected SSID (from text parsing): '%s'\n", selected_ssid);
  }

  // Verificar que el SSID no esté vacío
  if (strlen(selected_ssid) == 0) {
    Serial.println("ERROR: SSID is empty after extraction");
    return;
  }

  // Verificar caracteres inválidos
  for (int i = 0; selected_ssid[i] != '\0'; i++) {
    if (selected_ssid[i] == '\n' || selected_ssid[i] == '\r') {
      Serial.println("ERROR: SSID contains newline characters");
      selected_ssid[i] = '\0'; // Truncar aquí
      break;
    }
  }

  // Actualizar placeholder del campo de contraseña
  if (ui_WifiPass && lv_obj_is_valid(ui_WifiPass)) {
    char ph[80];
    snprintf(ph, sizeof(ph), "Password for: %s", selected_ssid);
    lv_textarea_set_placeholder_text(ui_WifiPass, ph);

    // Limpiar campo de contraseña anterior
    lv_textarea_set_text(ui_WifiPass, "");

    // Resaltar visualmente que está seleccionado
    lv_obj_set_style_border_color(ui_WifiPass, lv_color_hex(0x3498DB), 0);
    lv_obj_set_style_border_width(ui_WifiPass, 2, 0);

    // Mover foco al campo de contraseña
    lv_group_t *g = lv_group_get_default();
    if (g && lv_obj_is_valid(ui_WifiPass)) {
      lv_group_focus_obj(ui_WifiPass);
    }

    // Forzar actualización de la UI
    lv_task_handler();
  }

  Serial.printf("✅ SSID final para conexión: '%s' (length: %d)\n",
                selected_ssid, strlen(selected_ssid));
}

// En la función wifi_scan_task_safe - AGREGAR LLAMADA PARA ACTUALIZAR POP-UP
static void wifi_scan_task_safe(void *pvParameter) {
  TaskHandle_t this_task = xTaskGetCurrentTaskHandle();
  register_wifi_task(this_task, "WiFiScan");

  Serial.println("WiFi scan task SAFE - STARTED");
  wifi_scanning = true;

  // Limpiar estado previo
  memset(selected_ssid, 0, sizeof(selected_ssid));

  // 1. Verificar memoria
  if (ESP.getFreeHeap() < 20000) {
    Serial.println("ERROR: Memoria insuficiente para escaneo");
    wifi_scanning = false;

    // Enviar evento de cierre
    PopupEvent event = {POPUP_CLOSE_SCAN, 0};
    xQueueSend(popup_event_queue, &event, pdMS_TO_TICKS(50));

    unregister_wifi_task(this_task);
    vTaskDelete(NULL);
    return;
  }

  // 2. Tomar mutex WiFi con timeout más largo
  if (!xSemaphoreTake(wifi_mutex, pdMS_TO_TICKS(5000))) {
    Serial.println("ERROR: No se pudo obtener mutex WiFi");
    wifi_scanning = false;

    PopupEvent event = {POPUP_CLOSE_SCAN, 0};
    xQueueSend(popup_event_queue, &event, pdMS_TO_TICKS(50));

    unregister_wifi_task(this_task);
    vTaskDelete(NULL);
    return;
  }

  // 3. Dar tiempo para que el pop-up se muestre completamente
  vTaskDelay(400 / portTICK_PERIOD_MS);

  // 4. Configurar WiFi
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  vTaskDelay(pdMS_TO_TICKS(200));

  // 5. Escanear redes
  int16_t n = 0;
  try {
    n = WiFi.scanNetworks(false, true, false, 200);
  } catch (...) {
    Serial.println("EXCEPTION en WiFi.scanNetworks()");
    n = 0;
  }

  Serial.printf("Encontradas %d redes\n", n);

  // 6. Enviar evento para actualizar pop-up (ASINCRONO)
  PopupEvent update_event = {POPUP_UPDATE_SCAN, n};
  xQueueSend(popup_event_queue, &update_event, pdMS_TO_TICKS(100));

  // 7. Liberar mutex WiFi
  xSemaphoreGive(wifi_mutex);

  // 8. Procesar resultados
  if (n > 0) {
    // Crear array temporal
    int max_networks = (n > 10) ? 10 : n;
    NetworkInfo *networks =
        (NetworkInfo *)malloc(max_networks * sizeof(NetworkInfo));

    if (networks) {
      int valid_count = 0;
      for (int i = 0; i < max_networks; i++) {
        String ssid = WiFi.SSID(i);
        if (ssid.length() == 0 || ssid.length() > 32)
          continue;

        strncpy(networks[valid_count].ssid, ssid.c_str(), 32);
        networks[valid_count].ssid[32] = '\0';
        networks[valid_count].rssi = WiFi.RSSI(i);
        networks[valid_count].secure =
            (WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
        valid_count++;
      }

      // Actualizar UI de lista
      if (valid_count > 0) {
        update_wifi_list_safe(networks, valid_count);
      }

      free(networks);
    }
  } else if (n == 0) {
    // Mostrar mensaje "no networks"
    if (xSemaphoreTake(wifi_status_mutex, pdMS_TO_TICKS(100))) {
      set_wifi_status(LV_SYMBOL_WARNING, "No networks found");
      xSemaphoreGive(wifi_status_mutex);
    }
  }

  // 9. Esperar 2 segundos para mostrar el resultado
  Serial.println("Esperando 2 segundos antes de cerrar pop-up...");
  vTaskDelay(2000 / portTICK_PERIOD_MS);

  // 10. Enviar evento para cerrar pop-up
  PopupEvent close_event = {POPUP_CLOSE_SCAN, 0};
  xQueueSend(popup_event_queue, &close_event, pdMS_TO_TICKS(100));

  // 11. Limpiar escaneo
  WiFi.scanDelete();

  // 12. Finalizar
  wifi_scanning = false;
  Serial.println("WiFi scan task SAFE - FINISHED");

  // Esperar un poco antes de eliminar la tarea
  vTaskDelay(100 / portTICK_PERIOD_MS);
  unregister_wifi_task(this_task);
  vTaskDelete(NULL);
}

// En la función wifi_scan_click - ARREGLAR LLAMADA AL POP-UP
extern "C" void wifi_scan_click(lv_event_t *e) {
  Serial.println("=== Botón SCAN clickeado ===");

  // Limpiar tareas previas primero
  cleanup_all_wifi_tasks();

  vTaskDelay(100 / portTICK_PERIOD_MS);

  // Verificar memoria
  if (ESP.getFreeHeap() < 25000) {
    Serial.println("ERROR: Memoria insuficiente para escaneo");
    return;
  }

  // ENVIAR EVENTO en lugar de llamar directamente
  PopupEvent event = {POPUP_SHOW_SCAN, 0};
  if (xQueueSend(popup_event_queue, &event, pdMS_TO_TICKS(100)) != pdTRUE) {
    Serial.println("ERROR: No se pudo encolar evento de pop-up");
    return;
  }

  Serial.println("Evento de pop-up encolado exitosamente");

  // Limpiar UI de lista de forma ASINCRONA
  PopupEvent clear_event = {POPUP_CLOSE_SCAN, -1}; // -1 indica limpiar lista
  xQueueSend(popup_event_queue, &clear_event, pdMS_TO_TICKS(50));

  // Crear tarea de escaneo con retraso
  Serial.println("Creando tarea de escaneo WiFi...");
  vTaskDelay(200 / portTICK_PERIOD_MS); // Dar tiempo a que se muestre el pop-up

  if (xTaskCreate(wifi_scan_task_safe, "WiFiScanSafe", 16384, NULL, 2, NULL) !=
      pdPASS) {
    Serial.println("ERROR: No se pudo crear tarea WiFi");
    wifi_scanning = false;

    // Enviar evento para cerrar pop-up
    PopupEvent close_event = {POPUP_CLOSE_SCAN, 0};
    xQueueSend(popup_event_queue, &close_event, pdMS_TO_TICKS(50));
  } else {
    Serial.println("Tarea WiFi creada exitosamente");
  }
}

// Tarea para conexión WiFi en segundo plano - VERSIÓN MEJORADA
static void wifi_connect_task(void *pvParameter) {
  Serial.println("=== WiFi connect task STARTING ===");

  TaskHandle_t this_task = xTaskGetCurrentTaskHandle();
  if (!register_wifi_task(this_task, "WiFiConnect")) {
    Serial.println("ERROR: No se pudo registrar tarea WiFi");
    vTaskDelete(NULL);
    return;
  }

  // Copiar los parámetros locales primero
  char local_ssid[64];
  char local_password[64];

  memset(local_ssid, 0, sizeof(local_ssid));
  memset(local_password, 0, sizeof(local_password));

  // Verificar parámetros
  if (!pvParameter) {
    Serial.println("ERROR: Null parameters in WiFi task");
    set_wifi_status(LV_SYMBOL_CLOSE, "Error: Invalid parameters");
    unregister_wifi_task(this_task);
    vTaskDelete(NULL);
    return;
  }

  struct ConnectParams {
    char ssid[64];
    char password[64];
  };

  ConnectParams *params = (ConnectParams *)pvParameter;

  // Copiar a variables locales
  strncpy(local_ssid, params->ssid, sizeof(local_ssid) - 1);
  strncpy(local_password, params->password, sizeof(local_password) - 1);
  local_ssid[sizeof(local_ssid) - 1] = '\0';
  local_password[sizeof(local_password) - 1] = '\0';

  // Liberar memoria de parámetros
  free(params);
  params = NULL;

  Serial.printf("WiFi task starting for SSID: '%s'\n", local_ssid);
  Serial.printf("Password length: %d\n", strlen(local_password));

  // 1. MOSTRAR POP-UP DE CONEXIÓN
  lvgl_safe_call_with_arg(
      [](void *arg) { show_wifi_connect_popup((const char *)arg); },
      local_ssid);

  vTaskDelay(pdMS_TO_TICKS(100)); // Pequeña pausa para mostrar el pop-up

  // Crear mutex si no existe
  if (!wifi_mutex) {
    wifi_mutex = xSemaphoreCreateMutex();
  }

  bool mutex_acquired = false;

  // Intentar tomar el mutex con timeout
  if (xSemaphoreTake(wifi_mutex, pdMS_TO_TICKS(3000))) {
    mutex_acquired = true;
  } else {
    Serial.println("ERROR: Failed to acquire WiFi mutex (timeout)");

    // Mostrar error en pop-up
    lvgl_safe_call([]() {
      wifi_popup_result(false, "Error: WiFi ocupado\nIntenta de nuevo");
    });

    set_wifi_status(LV_SYMBOL_CLOSE, "WiFi busy - try again");
    unregister_wifi_task(this_task);
    vTaskDelay(pdMS_TO_TICKS(5000)); // Esperar antes de cerrar
    vTaskDelete(NULL);
    return;
  }

  // Actualizar pop-up - Fase 1
  lvgl_safe_call(
      []() { update_wifi_popup_progress(10, "Configurando WiFi..."); });

  Serial.println("Configuring WiFi mode...");

  // Configurar WiFi - REINICIAR COMPLETAMENTE
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true);          // true = olvidar redes anteriores
  vTaskDelay(pdMS_TO_TICKS(500)); // Más tiempo para desconectar

  // Actualizar pop-up - Fase 2
  lvgl_safe_call(
      []() { update_wifi_popup_progress(25, "Conectando a la red..."); });

  Serial.println("Calling WiFi.begin()...");

  // Intentar conexión con configuración explícita
  WiFi.begin(local_ssid, local_password);

  // Configurar tiempo de espera y reintentos
  WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN);
  WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL);

  // Esperar conexión con timeout
  unsigned long startTime = millis();
  wl_status_t status = WiFi.status();
  int attempt = 0;
  bool connected = false;
  int progress_percent = 25;

  Serial.println("Waiting for WiFi connection...");

  while (millis() - startTime < 30000) { // 30 segundos timeout (aumentado)
    status = WiFi.status();

    // Calcular progreso basado en tiempo
    progress_percent = 25 + ((millis() - startTime) * 50 / 30000);
    if (progress_percent > 75)
      progress_percent = 75;

    // DEBUG: Mostrar estado actual
    if (attempt % 4 == 0) { // Cada ~2 segundos
      const char *status_str = "Unknown";
      switch (status) {
      case WL_NO_SHIELD:
        status_str = "No shield";
        break;
      case WL_IDLE_STATUS:
        status_str = "Idle";
        break;
      case WL_NO_SSID_AVAIL:
        status_str = "No SSID";
        break;
      case WL_SCAN_COMPLETED:
        status_str = "Scan done";
        break;
      case WL_CONNECTED:
        status_str = "Connected!";
        break;
      case WL_CONNECT_FAILED:
        status_str = "Failed";
        break;
      case WL_CONNECTION_LOST:
        status_str = "Lost";
        break;
      case WL_DISCONNECTED:
        status_str = "Disconnected";
        break;
      }
      Serial.printf("WiFi status [%d]: %s\n", attempt, status_str);

      // Actualizar pop-up con estado detallado
      char status_msg[64];
      snprintf(status_msg, sizeof(status_msg), "Estado: %s", status_str);

      lvgl_safe_call_with_arg(
          [](void *arg) {
            update_wifi_popup_progress(*(int *)arg,
                                       (const char *)arg + sizeof(int));
          },
          [&]() {
            static struct {
              int percent;
              char msg[64];
            } data;
            data.percent = progress_percent;
            snprintf(data.msg, sizeof(data.msg), "Conectando...\n%s",
                     status_str);
            return &data;
          }());
    }

    if (status == WL_CONNECTED) {
      connected = true;
      Serial.println("=== WiFi CONNECTED! ===");
      break;
    }

    // Mostrar progreso en UI cada 2 segundos
    if (millis() - startTime > attempt * 1000) {
      attempt++;

      char progress_msg[80];
      const char *progress_text = "";

      switch (status) {
      case WL_IDLE_STATUS:
        progress_text = "Inicializando...";
        break;
      case WL_NO_SSID_AVAIL:
        progress_text = "SSID no encontrada";
        break;
      case WL_SCAN_COMPLETED:
        progress_text = "Escaneando...";
        break;
      case WL_CONNECT_FAILED:
        progress_text = "Fallo de autenticación";
        break;
      case WL_CONNECTION_LOST:
        progress_text = "Conexión perdida";
        break;
      case WL_DISCONNECTED:
        progress_text = "Conectando...";
        break;
      default:
        progress_text = "Esperando...";
        break;
      }

      snprintf(progress_msg, sizeof(progress_msg), "Conectando...\n%s",
               progress_text);
      set_wifi_status(LV_SYMBOL_REFRESH, progress_msg);
    }

    // Pequeña pausa para no saturar
    vTaskDelay(pdMS_TO_TICKS(500));
  }

  // Mostrar resultado final
  if (connected) {
    // Actualizar pop-up - Fase final
    lvgl_safe_call(
        []() { update_wifi_popup_progress(90, "Obteniendo dirección IP..."); });

    vTaskDelay(pdMS_TO_TICKS(1000));

    // Obtener información de conexión
    String ip = WiFi.localIP().toString();
    String gateway = WiFi.gatewayIP().toString();
    String dns = WiFi.dnsIP().toString();
    int32_t rssi = WiFi.RSSI();

    // Crear mensaje detallado de conexión para pop-up
    char connected_msg[128];
    snprintf(connected_msg, sizeof(connected_msg),
             "¡Conectado exitosamente!\n\n"
             "Red: %s\n"
             "IP: %s\n"
             "Señal: %d dBm",
             local_ssid, ip.c_str(), rssi);

    // Mostrar resultado en pop-up
    lvgl_safe_call_with_arg(
        [](void *arg) { wifi_popup_result(true, (const char *)arg); },
        connected_msg);

    // También actualizar estado general
    char status_msg[128];
    snprintf(status_msg, sizeof(status_msg),
             "✓ Connected to:\n%s\n\n"
             "IP: %s\n"
             "Signal: %d dBm\n"
             "Gateway: %s",
             local_ssid, ip.c_str(), rssi, gateway.c_str());

    set_wifi_status(LV_SYMBOL_OK, status_msg);
    Serial.printf("Connected successfully! IP: %s, RSSI: %d dBm\n", ip.c_str(),
                  rssi);
  } else {
    // Mostrar error detallado en pop-up
    char error_msg[96];
    const char *status_text = "Error desconocido";

    switch (status) {
    case WL_NO_SSID_AVAIL:
      status_text = "Red no encontrada";
      break;
    case WL_CONNECT_FAILED:
      status_text = "Contraseña incorrecta";
      break;
    case WL_CONNECTION_LOST:
      status_text = "Conexión perdida";
      break;
    case WL_DISCONNECTED:
      status_text = "Tiempo de espera (30s)";
      break;
    default:
      snprintf(error_msg, sizeof(error_msg), "Error: Código %d", status);
      status_text = error_msg;
      break;
    }

    char final_error[128];
    snprintf(final_error, sizeof(final_error),
             "✗ Conexión fallida\n\nRazón: %s\n\nIntenta de nuevo",
             status_text);

    // Mostrar error en pop-up
    lvgl_safe_call_with_arg(
        [](void *arg) { wifi_popup_result(false, (const char *)arg); },
        final_error);

    // También actualizar estado general
    set_wifi_status(LV_SYMBOL_CLOSE, final_error);
    Serial.printf("Connection failed after %d ms. Reason: %s\n",
                  millis() - startTime, status_text);

    // Desconectar completamente
    WiFi.disconnect(true);
    vTaskDelay(pdMS_TO_TICKS(100));
  }

  // Liberar mutex
  if (mutex_acquired) {
    xSemaphoreGive(wifi_mutex);
  }

  // Limpiar variables locales
  memset(local_ssid, 0, sizeof(local_ssid));
  memset(local_password, 0, sizeof(local_password));

  // Pequeña pausa antes de terminar (para que se vea el pop-up)
  vTaskDelay(pdMS_TO_TICKS(5000));

  Serial.println("=== WiFi connect task FINISHED ===");
  unregister_wifi_task(this_task);
  vTaskDelete(NULL);
}

// Función de conexión WiFi (llamada desde UI)
extern "C" void wifi_connect_click(lv_event_t *e) {
  if (strlen(selected_ssid) == 0) {
    Serial.println("No SSID selected");
    if (ui_WifiList) {
      lv_obj_clean(ui_WifiList);
      lv_list_add_btn(ui_WifiList, LV_SYMBOL_CLOSE, "Select a network first");
      lv_task_handler();
    }
    return;
  }

  // Verificar que el SSID no contenga "dBm" o números al final
  if (strstr(selected_ssid, "dBm") != NULL) {
    Serial.println("ERROR: SSID contains 'dBm' - extraction failed");
    if (ui_WifiList) {
      lv_obj_clean(ui_WifiList);
      lv_list_add_btn(ui_WifiList, LV_SYMBOL_CLOSE, "Invalid SSID selection");
      lv_task_handler();
    }
    return;
  }

  const char *pass = lv_textarea_get_text(ui_WifiPass);
  if (!pass || strlen(pass) < 8) {
    Serial.println("Password too short (min 8 chars)");
    if (ui_WifiList) {
      lv_obj_clean(ui_WifiList);
      lv_list_add_btn(ui_WifiList, LV_SYMBOL_CLOSE, "Password too short");
      lv_task_handler();
    }
    return;
  }

  Serial.printf("Attempting to connect to: '%s'\n", selected_ssid);
  Serial.printf("Password length: %d\n", strlen(pass));

  // Crear parámetros para la tarea
  struct ConnectParams {
    char ssid[64];
    char password[64];
  };

  ConnectParams *params = (ConnectParams *)malloc(sizeof(ConnectParams));
  if (!params) {
    Serial.println("Memory allocation failed");
    return;
  }

  // Limpiar la memoria
  memset(params, 0, sizeof(ConnectParams));

  // Copiar datos asegurándonos de no sobrepasar los límites
  strncpy(params->ssid, selected_ssid, sizeof(params->ssid) - 1);
  strncpy(params->password, pass, sizeof(params->password) - 1);
  params->ssid[sizeof(params->ssid) - 1] = '\0';
  params->password[sizeof(params->password) - 1] = '\0';

  Serial.printf("Params prepared - SSID: '%s', Pass: '%s'\n", params->ssid,
                "***");

  lv_textarea_set_text(ui_WifiPass, ""); // Limpiar campo de contraseña

  // Crear tarea de conexión
  if (xTaskCreate(wifi_connect_task, "WiFi Connect", 6144, params, 1, NULL) !=
      pdPASS) {
    Serial.println("Failed to create WiFi task");
    free(params);
    return;
  }

  Serial.println("WiFi connection task created");
}

// ============= WIFI CONNECTION POPUP =============

// Función para mostrar el pop-up de conexión WiFi - VERSIÓN SIMPLIFICADA
void show_wifi_connect_popup(const char *ssid) {
  // Si ya existe un pop-up, cerrarlo primero
  if (wifi_popup) {
    close_wifi_popup();
  }

  // Crear pop-up principal con altura ajustada
  wifi_popup = lv_obj_create(lv_scr_act());
  lv_obj_set_size(wifi_popup, 400, 280); // Más alto para evitar solapamientos
  lv_obj_center(wifi_popup);
  lv_obj_set_style_bg_color(wifi_popup, lv_color_hex(0x1a1a2e), 0);
  lv_obj_set_style_bg_opa(wifi_popup, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(wifi_popup, 20, 0);
  lv_obj_set_style_border_width(wifi_popup, 3, 0);
  lv_obj_set_style_border_color(wifi_popup, lv_color_hex(0x3498DB), 0);
  lv_obj_set_style_pad_all(wifi_popup, 15, 0); // Padding interno

  // IMPORTANTE: Deshabilitar scroll
  lv_obj_clear_flag(wifi_popup, LV_OBJ_FLAG_SCROLLABLE);

  // Título del pop-up
  wifi_popup_title = lv_label_create(wifi_popup);
  lv_label_set_text(wifi_popup_title, "CONECTANDO A WIFI");
  lv_obj_set_style_text_font(wifi_popup_title, &lv_font_montserrat_18, 0);
  lv_obj_set_style_text_color(wifi_popup_title, lv_color_white(), 0);
  lv_obj_align(wifi_popup_title, LV_ALIGN_TOP_MID, 0, 10);

  // SSID - Posicionado claramente debajo del título
  lv_obj_t *ssid_container = lv_obj_create(wifi_popup);
  lv_obj_set_size(ssid_container, 360, 50); // Más alto para mejor visibilidad
  lv_obj_align(ssid_container, LV_ALIGN_TOP_MID, 0, 45);
  lv_obj_set_style_bg_color(ssid_container, lv_color_hex(0x16213e), 0);
  lv_obj_set_style_bg_opa(ssid_container, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(ssid_container, 1, 0);
  lv_obj_set_style_border_color(ssid_container, lv_color_hex(0x3498DB), 0);
  lv_obj_set_style_radius(ssid_container, 10, 0);
  lv_obj_set_style_pad_all(ssid_container, 10, 0);
  lv_obj_clear_flag(ssid_container, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *ssid_label = lv_label_create(ssid_container);
  lv_label_set_text(ssid_label, "Red:");
  lv_obj_set_style_text_font(ssid_label, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(ssid_label, lv_color_hex(0xAAAAAA), 0);
  lv_obj_align(ssid_label, LV_ALIGN_LEFT_MID, 5, 0);

  lv_obj_t *ssid_value = lv_label_create(ssid_container);
  lv_label_set_text(ssid_value, ssid);
  lv_obj_set_style_text_font(ssid_value, &lv_font_montserrat_16, 0);
  lv_obj_set_style_text_color(ssid_value, lv_color_hex(0x4BC2FF), 0);
  lv_obj_align_to(ssid_value, ssid_label, LV_ALIGN_OUT_RIGHT_MID, 10, 0);
  lv_label_set_long_mode(ssid_value, LV_LABEL_LONG_SCROLL_CIRCULAR);
  lv_obj_set_width(ssid_value, 260); // Ancho ajustado

  // Icono animado (arco giratorio) - Centrado en el espacio disponible
  wifi_popup_arc = lv_arc_create(wifi_popup);
  lv_obj_set_size(wifi_popup_arc, 70, 70);
  lv_obj_align(wifi_popup_arc, LV_ALIGN_CENTER, 0,
               10); // Centrado verticalmente
  lv_arc_set_bg_angles(wifi_popup_arc, 0, 360);
  lv_arc_set_angles(wifi_popup_arc, 0, 270);
  lv_obj_set_style_arc_color(wifi_popup_arc, lv_color_hex(0x3498DB),
                             LV_PART_INDICATOR);
  lv_obj_set_style_arc_width(wifi_popup_arc, 6, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(wifi_popup_arc, lv_color_hex(0x2C3E50),
                             LV_PART_MAIN);
  lv_obj_set_style_arc_width(wifi_popup_arc, 6, LV_PART_MAIN);
  lv_obj_remove_style(wifi_popup_arc, NULL, LV_PART_KNOB);

  // Icono WiFi dentro del arco
  wifi_popup_icon = lv_label_create(wifi_popup_arc);
  lv_label_set_text(wifi_popup_icon, LV_SYMBOL_WIFI);
  lv_obj_set_style_text_font(wifi_popup_icon, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(wifi_popup_icon, lv_color_white(), 0);
  lv_obj_center(wifi_popup_icon);

  // Mensaje de estado - Debajo del arco con espacio suficiente
  wifi_popup_message = lv_label_create(wifi_popup);
  lv_label_set_text(wifi_popup_message, "Conectando...");
  lv_obj_set_style_text_font(wifi_popup_message, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(wifi_popup_message, lv_color_hex(0xAAAAAA), 0);
  lv_obj_set_style_text_align(wifi_popup_message, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_width(wifi_popup_message, 360);
  lv_label_set_long_mode(wifi_popup_message, LV_LABEL_LONG_WRAP);
  lv_obj_align(wifi_popup_message, LV_ALIGN_BOTTOM_MID, 0, -15);

  // Timer para animación del arco
  wifi_popup_timer = lv_timer_create(
      [](lv_timer_t *timer) {
        static int angle = 0;
        if (wifi_popup_arc && lv_obj_is_valid(wifi_popup_arc)) {
          angle += 15;
          if (angle > 360)
            angle = 0;
          lv_arc_set_angles(wifi_popup_arc, angle, angle + 270);
        }
      },
      50, NULL);

  // Animación de entrada
  lv_obj_set_style_opa(wifi_popup, 0, 0);
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, wifi_popup);
  lv_anim_set_values(&a, 0, 255);
  lv_anim_set_time(&a, 300);
  lv_anim_set_exec_cb(&a, [](void *obj, int32_t v) {
    lv_obj_set_style_opa((lv_obj_t *)obj, v, 0);
  });
  lv_anim_start(&a);
}

// Función para actualizar el progreso del pop-up
void update_wifi_popup_progress(int percent, const char *message) {
  if (!wifi_popup || !lv_obj_is_valid(wifi_popup))
    return;

  // Actualizar mensaje
  if (wifi_popup_message && lv_obj_is_valid(wifi_popup_message)) {
    lv_label_set_text(wifi_popup_message, message);
  }
}

// Función para mostrar resultado de conexión - VERSIÓN MEJORADA
void wifi_popup_result(bool success, const char *message) {
  if (!wifi_popup || !lv_obj_is_valid(wifi_popup))
    return;

  // Detener animación del arco
  if (wifi_popup_timer) {
    lv_timer_del(wifi_popup_timer);
    wifi_popup_timer = NULL;
  }

  // Ajustar tamaño del popup para el resultado
  lv_obj_set_height(wifi_popup, 320); // Más alto para mostrar toda la info

  // Cambiar título según resultado
  if (wifi_popup_title && lv_obj_is_valid(wifi_popup_title)) {
    lv_label_set_text(wifi_popup_title,
                      success ? "CONEXION EXITOSA" : "ERROR DE CONEXION");
    lv_obj_set_style_text_color(
        wifi_popup_title,
        success ? lv_color_hex(0x27AE60) : lv_color_hex(0xE74C3C), 0);
  }

  // Cambiar icono y arco
  if (wifi_popup_icon && lv_obj_is_valid(wifi_popup_icon) && wifi_popup_arc &&
      lv_obj_is_valid(wifi_popup_arc)) {

    // Mover arco más arriba para dar espacio al mensaje
    lv_obj_align(wifi_popup_arc, LV_ALIGN_TOP_MID, 0, 110);

    if (success) {
      lv_label_set_text(wifi_popup_icon, LV_SYMBOL_OK);
      lv_obj_set_style_text_color(wifi_popup_icon, lv_color_hex(0x27AE60), 0);
      lv_obj_set_style_arc_color(wifi_popup_arc, lv_color_hex(0x27AE60),
                                 LV_PART_INDICATOR);
      lv_arc_set_angles(wifi_popup_arc, 0, 360);
    } else {
      lv_label_set_text(wifi_popup_icon, LV_SYMBOL_CLOSE);
      lv_obj_set_style_text_color(wifi_popup_icon, lv_color_hex(0xE74C3C), 0);
      lv_obj_set_style_arc_color(wifi_popup_arc, lv_color_hex(0xE74C3C),
                                 LV_PART_INDICATOR);
      lv_arc_set_angles(wifi_popup_arc, 0, 360);
    }
  }

  // Actualizar mensaje - FORMATEADO CORRECTAMENTE
  if (wifi_popup_message && lv_obj_is_valid(wifi_popup_message)) {
    // Reposicionar el mensaje debajo del arco con suficiente espacio
    lv_obj_align(wifi_popup_message, LV_ALIGN_BOTTOM_MID, 0, -60);
    lv_obj_set_width(wifi_popup_message, 360);
    lv_label_set_long_mode(wifi_popup_message, LV_LABEL_LONG_WRAP);

    if (success) {
      // Extraer información relevante del mensaje
      char simplified_msg[256] = "";

      // Buscar IP en el mensaje
      const char *ip_start = strstr(message, "IP:");
      if (ip_start) {
        char ip[32] = "";
        const char *ip_end = strchr(ip_start + 4, '\n');
        int ip_len = ip_end ? (ip_end - (ip_start + 4)) : strlen(ip_start + 4);
        if (ip_len > 0 && ip_len < 32) {
          strncpy(ip, ip_start + 4, ip_len);
          ip[ip_len] = '\0';

          // Eliminar espacios
          char *p = ip;
          while (*p && *p == ' ')
            p++;

          snprintf(simplified_msg, sizeof(simplified_msg),
                   "Conexion establecida\n\n"
                   "IP: %s",
                   p);
        }
      } else {
        strcpy(simplified_msg, "Conexion establecida");
      }

      lv_label_set_text(wifi_popup_message, simplified_msg);
      lv_obj_set_style_text_color(wifi_popup_message, lv_color_hex(0x27AE60),
                                  0);

    } else {
      // Para errores, mensaje conciso
      char error_msg[128];

      // Extraer razón del error si existe
      const char *reason_start = strstr(message, "Razon:");
      if (reason_start) {
        const char *reason_end = strchr(reason_start, '\n');
        int reason_len = reason_end ? (reason_end - (reason_start + 7)) : 30;
        if (reason_len > 60)
          reason_len = 60;

        char reason[64] = "";
        strncpy(reason, reason_start + 7, reason_len);
        reason[reason_len] = '\0';

        snprintf(error_msg, sizeof(error_msg),
                 "No se pudo conectar\n\n%s\n\nVerify the password", reason);
      } else {
        strcpy(error_msg,
               "No se pudo conectar\n\nVerify:\n• Password\n• WiFi Signal");
      }

      lv_label_set_text(wifi_popup_message, error_msg);
      lv_obj_set_style_text_color(wifi_popup_message, lv_color_hex(0xE74C3C),
                                  0);
    }

    lv_obj_set_style_text_font(wifi_popup_message, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_align(wifi_popup_message, LV_TEXT_ALIGN_CENTER, 0);
  }

  // Botón de cerrar - Posicionado claramente en la parte inferior
  lv_obj_t *close_btn = lv_btn_create(wifi_popup);
  lv_obj_set_size(close_btn, 140, 40);
  lv_obj_align(close_btn, LV_ALIGN_BOTTOM_MID, 0, -10);
  lv_obj_set_style_bg_color(
      close_btn, success ? lv_color_hex(0x27AE60) : lv_color_hex(0xE74C3C), 0);
  lv_obj_set_style_radius(close_btn, 10, 0);

  lv_obj_add_event_cb(
      close_btn,
      [](lv_event_t *e) {
        if (e->code == LV_EVENT_CLICKED) {
          close_wifi_popup();
        }
      },
      LV_EVENT_CLICKED, NULL);

  lv_obj_t *close_label = lv_label_create(close_btn);
  lv_label_set_text(close_label, success ? "CONTINUAR" : "CERRAR");
  lv_obj_center(close_label);
  lv_obj_set_style_text_color(close_label, lv_color_white(), 0);
  lv_obj_set_style_text_font(close_label, &lv_font_montserrat_14, 0);

  // Auto-cerrar después de 6 segundos si es éxito
  if (success) {
    lv_timer_t *auto_close_timer = lv_timer_create(
        [](lv_timer_t *timer) {
          close_wifi_popup();
          lv_timer_del(timer);
        },
        6000, NULL);
    lv_timer_set_repeat_count(auto_close_timer, 1);
  }
}

// ============= WIFI SCAN POPUP =============

// Función para mostrar pop-up de escaneo WiFi - VERSIÓN MEJORADA
void show_wifi_scan_popup(void) {
  Serial.println("Mostrando pop-up de escaneo WiFi...");

  // Crear mutex si no existe
  if (!scan_popup_mutex) {
    scan_popup_mutex = xSemaphoreCreateMutex();
  }

  // Tomar mutex antes de crear
  if (xSemaphoreTake(scan_popup_mutex, pdMS_TO_TICKS(100))) {
    // Si ya existe un pop-up, cerrarlo primero
    if (scan_popup) {
      close_wifi_scan_popup();
      vTaskDelay(50 / portTICK_PERIOD_MS);
    }

    // Marcar como no listo
    scan_popup_ready = false;

    // Crear pop-up de escaneo con dimensiones ajustadas
    scan_popup = lv_obj_create(lv_scr_act());
    lv_obj_set_size(scan_popup, 380, 240); // Más alto para evitar solapamiento
    lv_obj_center(scan_popup);
    lv_obj_set_style_bg_color(scan_popup, lv_color_hex(0x1a1a2e), 0);
    lv_obj_set_style_bg_opa(scan_popup, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(scan_popup, 20, 0);
    lv_obj_set_style_border_width(scan_popup, 3, 0);
    lv_obj_set_style_border_color(scan_popup, lv_color_hex(0xF39C12), 0);
    lv_obj_set_style_pad_all(scan_popup, 15, 0);

    // IMPORTANTE: Deshabilitar scroll
    lv_obj_clear_flag(scan_popup, LV_OBJ_FLAG_SCROLLABLE);

    // Título - Bien posicionado arriba
    scan_popup_title = lv_label_create(scan_popup);
    lv_label_set_text(scan_popup_title, "BUSCANDO REDES");
    lv_obj_set_style_text_font(scan_popup_title, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(scan_popup_title, lv_color_hex(0xF39C12), 0);
    lv_obj_align(scan_popup_title, LV_ALIGN_TOP_MID, 0, 10);

    // Subtítulo - Debajo del título con espacio
    lv_obj_t *subtitle = lv_label_create(scan_popup);
    lv_label_set_text(subtitle, "Escaneando redes WiFi...");
    lv_obj_set_style_text_font(subtitle, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(subtitle, lv_color_hex(0xAAAAAA), 0);
    lv_obj_align(subtitle, LV_ALIGN_TOP_MID, 0, 40);

    // Icono WiFi animado - Centrado en el espacio medio
    scan_popup_arc = lv_arc_create(scan_popup);
    lv_obj_set_size(scan_popup_arc, 90, 90);
    lv_obj_align(scan_popup_arc, LV_ALIGN_CENTER, 0,
                 10); // Centrado verticalmente
    lv_arc_set_bg_angles(scan_popup_arc, 0, 360);
    lv_arc_set_angles(scan_popup_arc, 0, 180);
    lv_obj_set_style_arc_color(scan_popup_arc, lv_color_hex(0xF39C12),
                               LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(scan_popup_arc, 8, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(scan_popup_arc, lv_color_hex(0x7D6608),
                               LV_PART_MAIN);
    lv_obj_set_style_arc_width(scan_popup_arc, 8, LV_PART_MAIN);
    lv_obj_remove_style(scan_popup_arc, NULL, LV_PART_KNOB);

    // Icono WiFi dentro del arco
    scan_popup_icon = lv_label_create(scan_popup_arc);
    lv_label_set_text(scan_popup_icon, LV_SYMBOL_WIFI);
    lv_obj_set_style_text_font(scan_popup_icon, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(scan_popup_icon, lv_color_hex(0xF39C12), 0);
    lv_obj_center(scan_popup_icon);

    // Contador de redes - Debajo del arco con espacio suficiente
    scan_popup_message = lv_label_create(scan_popup);
    lv_label_set_text(scan_popup_message, "Redes encontradas: 0");
    lv_obj_set_style_text_font(scan_popup_message, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(scan_popup_message, lv_color_hex(0x4BC2FF), 0);
    lv_obj_set_style_text_align(scan_popup_message, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(scan_popup_message, LV_ALIGN_BOTTOM_MID, 0,
                 -15); // Abajo del todo

    // Timer para animación del escaneo (radar)
    scan_popup_timer = lv_timer_create(
        [](lv_timer_t *timer) {
          static int angle = 0;
          static int direction = 1;

          if (scan_popup_arc && lv_obj_is_valid(scan_popup_arc)) {
            angle += direction * 15;

            if (angle >= 270) {
              angle = 270;
              direction = -1;
            } else if (angle <= 0) {
              angle = 0;
              direction = 1;
            }

            lv_arc_set_angles(scan_popup_arc, angle, angle + 90);
          }
        },
        80, NULL);

    // Animación de entrada
    lv_obj_set_style_opa(scan_popup, 0, 0);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, scan_popup);
    lv_anim_set_values(&a, 0, 255);
    lv_anim_set_time(&a, 300);
    lv_anim_set_exec_cb(&a, [](void *obj, int32_t v) {
      lv_obj_set_style_opa((lv_obj_t *)obj, v, 0);
    });

    // Cuando termine la animación, marcar como listo
    lv_anim_set_ready_cb(&a, [](lv_anim_t *a) {
      scan_popup_ready = true;
      Serial.println("Pop-up de escaneo listo y visible");
    });

    lv_anim_start(&a);

    // Liberar mutex
    xSemaphoreGive(scan_popup_mutex);
  } else {
    Serial.println("ERROR: No se pudo obtener mutex para crear pop-up");
  }

  Serial.println("Pop-up de escaneo creado");
}

// Función para actualizar el pop-up de escaneo - SIN CAMBIOS (ya está bien)
void update_scan_popup(int network_count) {
  // Verificar si el pop-up está listo
  if (!scan_popup_ready) {
    Serial.printf("DEBUG: Pop-up no está listo aún, esperando...\n");

    unsigned long start = millis();
    while (!scan_popup_ready && (millis() - start) < 500) {
      vTaskDelay(10 / portTICK_PERIOD_MS);
    }

    if (!scan_popup_ready) {
      Serial.printf("ERROR: Timeout esperando pop-up. Count=%d\n",
                    network_count);
      return;
    }
  }

  // Tomar mutex para actualización segura
  if (scan_popup_mutex &&
      xSemaphoreTake(scan_popup_mutex, pdMS_TO_TICKS(100))) {
    if (!scan_popup || !lv_obj_is_valid(scan_popup)) {
      Serial.println("ERROR: scan_popup es NULL o inválido");
      xSemaphoreGive(scan_popup_mutex);
      return;
    }

    Serial.printf("Actualizando pop-up de escaneo: %d redes\n", network_count);

    // Actualizar contador
    if (scan_popup_message && lv_obj_is_valid(scan_popup_message)) {
      char counter_text[48];
      snprintf(counter_text, sizeof(counter_text), "Redes encontradas: %d",
               network_count);
      lv_label_set_text(scan_popup_message, counter_text);
      lv_obj_invalidate(scan_popup_message);
    }

    xSemaphoreGive(scan_popup_mutex);
  } else {
    Serial.println("WARNING: No se pudo obtener mutex para actualizar pop-up");
  }
}

// Función para cerrar el pop-up de escaneo
void close_wifi_scan_popup(void) {
  if (!scan_popup)
    return;

  // Marcar como no listo
  scan_popup_ready = false;

  // Tomar mutex
  if (scan_popup_mutex &&
      !xSemaphoreTake(scan_popup_mutex, pdMS_TO_TICKS(100))) {
    Serial.println("WARNING: No se pudo obtener mutex para cerrar pop-up");
    return;
  }

  // Detener timer de animación
  if (scan_popup_timer) {
    lv_timer_del(scan_popup_timer);
    scan_popup_timer = NULL;
  }

  // Animación de salida
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, scan_popup);
  lv_anim_set_values(&a, 255, 0);
  lv_anim_set_time(&a, 200);
  lv_anim_set_exec_cb(&a, [](void *obj, int32_t v) {
    lv_obj_set_style_opa((lv_obj_t *)obj, v, 0);
  });

  // Callback para limpiar
  lv_anim_set_ready_cb(&a, [](lv_anim_t *a) {
    if (scan_popup) {
      lv_obj_del(scan_popup);
      scan_popup = NULL;
      scan_popup_title = NULL;
      scan_popup_icon = NULL;
      scan_popup_message = NULL;
      scan_popup_arc = NULL;
    }

    if (scan_popup_mutex) {
      xSemaphoreGive(scan_popup_mutex);
    }
  });

  lv_anim_start(&a);

  Serial.println("Pop-up de escaneo cerrado");
}

// Función para cerrar el pop-up de conexión
void close_wifi_popup(void) {
  if (!wifi_popup)
    return;

  // Detener timer de animación
  if (wifi_popup_timer) {
    lv_timer_del(wifi_popup_timer);
    wifi_popup_timer = NULL;
  }

  // Animación de salida
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, wifi_popup);
  lv_anim_set_values(&a, 255, 0);
  lv_anim_set_time(&a, 200);
  lv_anim_set_exec_cb(&a, [](void *obj, int32_t v) {
    lv_obj_set_style_opa((lv_obj_t *)obj, v, 0);
  });

  // Callback para limpiar
  lv_anim_set_ready_cb(&a, [](lv_anim_t *a) {
    lv_obj_del(wifi_popup);
    wifi_popup = NULL;
    wifi_popup_title = NULL;
    wifi_popup_icon = NULL;
    wifi_popup_message = NULL;
    wifi_popup_progress = NULL;
    wifi_popup_arc = NULL;
  });

  lv_anim_start(&a);
}
