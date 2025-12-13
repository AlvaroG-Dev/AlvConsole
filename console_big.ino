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

// ============= BATERIA =============
Adafruit_INA219 ina219;
float consumoTotal_mAh = 0;
unsigned long ultimoTiempo = 0;
bool primeraLectura = true;
unsigned long lastBatteryUpdate = 0;
const unsigned long BATTERY_UPDATE_INTERVAL = 10000; // 10 segundos

// ============= PROTOTIPOS DE FUNCIONES =============
void initDisplay();
bool initSD();
void initTouch();
void initLVGL();
bool testSD();
bool operacionSeguraSD(std::function<bool()> operacion);
bool guardarEnSD(const char *filename, const char *data);
String leerDeSD(const char *filename);

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
    failAndReturn("Error: No hay partición OTA");
    return;
  }

  Serial.printf("🔧 Partición: %s (0x%x, %d bytes)\n", otaPart->label,
                otaPart->address, otaPart->size);

  // PASO 3: Preparar partición (opcional)
  lv_label_set_text(ui_Label6, "Preparando partición...");
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
    failAndReturn("OTA Falló");
    return;
  }

  Serial.println("🎉 Juego Cargado!");
  lv_label_set_text(ui_Label6, "Juego Cargado!");
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
    Serial.println("ERROR: Falló SD.begin() simple");

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
      Serial.println("Falló");
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
  Serial.println("ESP32-S3 ST7796 + microSD - SOLUCIÓN SIMPLE");
  Serial.println("=================================\n");

  // SECUENCIA SIMPLE:
  // 1. Pantalla primero
  initDisplay();

  Serial.println("Montando SPIFFS...");
  if (!SPIFFS.begin(true)) {
    Serial.println("❌ ERROR: No se pudo montar SPIFFS");
  } else {
    Serial.println("✔ SPIFFS montado correctamente");
  }

  // 2. SD después
  // if (initSD()) {
  //  // 3. Test SD solo si se inicializó correctamente
  //  testSD();
  //}

  // 4. Componentes que no comparten SPI
  initTouch();
  initLVGL();

  Serial.println("Memoria libre: " + String(ESP.getFreeHeap()) + " bytes");
  Serial.println("Cargando UI...");

  ui_init();

  // Configurar lógica personalizada de UI
  setup_ui_logic();

  init_focus_style();

  // Agregar objetos de la pantalla principal al grupo de navegación
  lv_group_t *g = lv_group_get_default();
  if (g && ui_Screen1) {
    lv_group_set_wrap(g, true); // Permitir dar la vuelta al final
    Serial.println("Agregando objetos de UI al grupo de navegación...");
    add_to_group_recursive(ui_Screen1, g);

    // Enfocar explícitamente el primer objeto si existe
    if (lv_group_get_obj_count(g) > 0) {
      lv_group_focus_next(g);
      Serial.println("Foco inicial establecido");
    } else {
      Serial.println(
          "ADVERTENCIA: No se encontraron objetos clickeables en ui_Screen1");
    }
  }
  Wire.begin(TOUCH_SDA, TOUCH_SCL);
  if (!ina219.begin()) {
    Serial.println("Fallo al encontrar INA219");
    while (1)
      ;
  }

  // Configuración ÓPTIMA para batería 1S
  ina219.setCalibration_16V_400mA(); // Más preciso para 1000mAh
  Serial.println("Sistema completamente inicializado");
  Serial.println("Memoria final: " + String(ESP.getFreeHeap()) + " bytes");
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

void update_battery_info() {
  // Leer valores
  float voltajeShunt_mV = ina219.getShuntVoltage_mV();
  float voltajeBus_V = ina219.getBusVoltage_V();
  float corriente_mA = ina219.getCurrent_mA();
  float potencia_mW = ina219.getPower_mW();

  // Calcular voltaje real de la batería
  float voltajeBateria_V = voltajeBus_V + (voltajeShunt_mV / 1000);

  // Calcular mAh consumidos (integración)
  unsigned long ahora = millis();
  if (!primeraLectura) {
    float horasPasadas = (ahora - ultimoTiempo) / 3600000.0;
    consumoTotal_mAh += abs(corriente_mA) * horasPasadas;
  }
  ultimoTiempo = ahora;
  primeraLectura = false;

  // Calcular porcentaje de batería (1S LiPo)
  float porcentaje = calcularPorcentajeLiPo1S(voltajeBateria_V);

  // Determinamos el color de la batería (Verde > 20%, Rojo <= 20%)
  lv_color_t batColor =
      (porcentaje > 20.0) ? lv_color_hex(0x00FF00) : lv_color_hex(0xFF0000);

  // Actualizar barras de batería
  if (ui_Bar1 && lv_obj_is_valid(ui_Bar1)) {
    lv_bar_set_value(ui_Bar1, (int)porcentaje, LV_ANIM_ON);
    // Aplicar color dinámico
    lv_obj_set_style_bg_color(ui_Bar1, batColor,
                              LV_PART_INDICATOR | LV_STATE_DEFAULT);
  }
  if (ui_Bar4 && lv_obj_is_valid(ui_Bar4)) {
    lv_bar_set_value(ui_Bar4, (int)porcentaje, LV_ANIM_OFF);
    // Aplicar color dinámico
    lv_obj_set_style_bg_color(ui_Bar4, batColor,
                              LV_PART_INDICATOR | LV_STATE_DEFAULT);
  }

  // Actualizar Debug Label en Settings
  if (ui_LabelDebug && lv_obj_is_valid(ui_LabelDebug)) {
    String debugText =
        "V: " + String(voltajeBateria_V, 2) + " V  |  " +
        String(porcentaje, 0) + "%\n" + "I: " + String(corriente_mA, 1) +
        " mA\n" + "P: " + String(potencia_mW, 1) + " mW\n" +
        "C: " + String(consumoTotal_mAh, 0) + " mAh\n" + "Estado: " +
        (corriente_mA < -20 ? "CARGANDO"
                            : (corriente_mA > 20 ? "DESCARGANDO" : "REPOSO"));
    lv_label_set_text(ui_LabelDebug, debugText.c_str());
  }
}

// Función para calcular porcentaje de batería 1S LiPo (Interpolación lineal)
float calcularPorcentajeLiPo1S(float voltaje) {
  // Puntos de referencia: {Voltaje, Porcentaje}
  // Deben estar ordenados de mayor a menor voltaje
  const struct {
    float v;
    float p;
  } points[] = {{4.20, 100.0}, {4.15, 98.0}, {4.10, 95.0}, {4.00, 85.0},
                {3.90, 75.0},  {3.80, 60.0}, {3.70, 40.0}, {3.60, 20.0},
                {3.50, 10.0},  {3.40, 5.0},  {3.30, 1.0}};

  // 1. Si está por encima del máximo
  if (voltaje >= points[0].v)
    return 100.0;

  // 2. Si está por debajo del mínimo definido
  if (voltaje <= points[10].v)
    return 0.0;

  // 3. Buscar el rango y interpolar
  for (int i = 0; i < 10; i++) {
    if (voltaje >= points[i + 1].v) {
      float vHigh = points[i].v;
      float pHigh = points[i].p;
      float vLow = points[i + 1].v;
      float pLow = points[i + 1].p;

      // Interpolación lineal
      return pLow + (voltaje - vLow) * (pHigh - pLow) / (vHigh - vLow);
    }
  }

  return 0.0; // Por si acaso
}

// Función para reiniciar contador de mAh
void reiniciarContador() {
  consumoTotal_mAh = 0;
  primeraLectura = true;
  Serial.println("Contador de mAh reiniciado");
}

void loop() {
  unsigned long now = millis();

  // LVGL (debe llamarse muy frecuentemente)
  lv_timer_handler();

  // Wakeup táctil
  checkTouchWakeup();

  // Actualizar batería cada 10 segundos
  if (now - lastBatteryUpdate >= BATTERY_UPDATE_INTERVAL) {
    lastBatteryUpdate = now;
    update_battery_info();
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