#include <lvgl.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <ui.h> // your LVGL UI
#include <WiFi.h>
#include <ESPSupabaseRealtime.h>
#include <ArduinoJson.h>

// ---- WiFi Config ----
const char* ssid = "JNTU2";
const char* password = "";

// ---- Supabase Config ----
const char* supabaseUrl = "uorbdplqtxmcdhbnkbmf.supabase.co";
const char* supabaseKey = "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJpc3MiOiJzdXBhYmFzZSIsInJlZiI6InVvcmJkcGxxdHhtY2RoYm5rYm1mIiwicm9sZSI6ImFub24iLCJpYXQiOjE3NDI2MjE0MTIsImV4cCI6MjA1ODE5NzQxMn0.Rl-PVPAXXoHYQVnuYl1rZG5PQjxzMCJJCz_uOUL1qiE";

// ---- Pin Config ----
#define TOUCH_CS 5
#define TOUCH_IRQ 25
#define LED_PIN 19  // D19 pin for LED

// ---- Screen Config ----
static const uint16_t screenWidth = 320;
static const uint16_t screenHeight = 240;

// Reduce buffer size to save memory
static lv_disp_draw_buf_t draw_buf;
static lv_color_t buf[screenWidth * 10]; // Reduced from screenHeight/10 to just 10 rows

TFT_eSPI tft = TFT_eSPI(screenWidth, screenHeight);
XPT2046_Touchscreen ts(TOUCH_CS, TOUCH_IRQ);

// Create Supabase Realtime instance
SupabaseRealtime supabase;

// LED status label
lv_obj_t *led_status_label = NULL;

// Function to handle incoming Supabase messages
void handleSupabaseMessage(String message) {
    
    // Use a smaller JSON document to save memory
    StaticJsonDocument<256> doc;
    DeserializationError error = deserializeJson(doc, message);
    
    if (error) {
        
        return;
    }
    
    // Check if this is a broadcast message
    if (doc["type"] == "broadcast") {
        String event = doc["event"];
        
        if (event == "ledOn") {
            // Turn on the LED
            digitalWrite(LED_PIN, HIGH);
            
            // Update UI if label exists
            if (led_status_label) {
                lv_label_set_text(led_status_label, "LED: ON");
                lv_obj_set_style_text_color(led_status_label, lv_color_make(0, 255, 0), LV_PART_MAIN);
            }
        } 
        else if (event == "ledOff") {
            // Turn off the LED
            digitalWrite(LED_PIN, LOW);
            
            // Update UI if label exists
            if (led_status_label) {
                lv_label_set_text(led_status_label, "LED: OFF");
                lv_obj_set_style_text_color(led_status_label, lv_color_make(255, 0, 0), LV_PART_MAIN);
            }
        }
    }
}

void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p) {
    uint32_t w = (area->x2 - area->x1 + 1);
    uint32_t h = (area->y2 - area->y1 + 1);

    tft.startWrite();
    tft.setAddrWindow(area->x1, area->y1, w, h);
    tft.pushColors((uint16_t *)&color_p->full, w * h, true);
    tft.endWrite();

    lv_disp_flush_ready(disp);
}

void my_touchpad_read(lv_indev_drv_t *indev_driver, lv_indev_data_t *data) {
    if (ts.touched()) {
        TS_Point p = ts.getPoint();
        data->point.x = map(p.x, 200, 3900, 0, screenWidth);
        data->point.y = map(p.y, 200, 3900, 0, screenHeight);
        data->state = LV_INDEV_STATE_PR;
    } else {
        data->state = LV_INDEV_STATE_REL;
    }
}

void setup() {
    Serial.begin(115200);
    
    // Initialize LED pin
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);
    
    // Connect to WiFi
    WiFi.begin(ssid, password);
    
    // Initialize LVGL
    lv_init();
    tft.begin();
    tft.setRotation(1);
    ts.begin();
    ts.setRotation(1);
    
    // Use a smaller buffer to save memory
    lv_disp_draw_buf_init(&draw_buf, buf, NULL, screenWidth * 10);

    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = screenWidth;
    disp_drv.ver_res = screenHeight;
    disp_drv.flush_cb = my_disp_flush;
    disp_drv.draw_buf = &draw_buf;
    lv_disp_drv_register(&disp_drv);

    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = my_touchpad_read;
    lv_indev_drv_register(&indev_drv);

    ui_init(); // your LVGL GUI init
    
    // Create LED status label
    led_status_label = lv_label_create(lv_scr_act());
    lv_label_set_text(led_status_label, "LED: OFF");
    lv_obj_set_style_text_color(led_status_label, lv_color_make(255, 0, 0), LV_PART_MAIN);
    lv_obj_align(led_status_label, LV_ALIGN_TOP_RIGHT, -10, 10);
    
    // Wait for WiFi connection
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        delay(500);
        // Serial.print(".");
        attempts++;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        
        // Initialize Supabase
        supabase.begin(supabaseUrl, supabaseKey, handleSupabaseMessage);
        supabase.addChangesListener("messages", "*", "public", "");
        supabase.listen();
        
    } else {
    }
}

void loop() {
    // Handle LVGL tasks
    lv_timer_handler();
    
    // Handle Supabase Realtime tasks
    if (WiFi.status() == WL_CONNECTED) {
        supabase.loop();
    }
    
    delay(5);
}