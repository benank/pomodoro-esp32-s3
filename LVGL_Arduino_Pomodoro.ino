#include <lvgl.h>
#include <TFT_eSPI.h>
#include "lv_conf.h"
#include <demos/lv_demos.h>
#include "CST816S.h"

#define EXAMPLE_LVGL_TICK_PERIOD_MS    2

static const uint16_t screenWidth  = 240;
static const uint16_t screenHeight = 240;

static lv_disp_draw_buf_t draw_buf;
static lv_color_t buf[ screenWidth * screenHeight / 10 ];

TFT_eSPI tft = TFT_eSPI(screenWidth, screenHeight); /* TFT instance */
CST816S touch(6, 7, 13, 5);	// sda, scl, rst, irq

// Pomodoro Timer States
typedef enum {
    POMODORO_IDLE,
    POMODORO_FOCUS,
    POMODORO_BREAK,
    POMODORO_TRANSITION
} pomodoro_state_t;

// Pomodoro Timer Configuration
// Set DEBUG_MODE to 1 for faster testing (10 seconds focus, 5 seconds break)
#define DEBUG_MODE            0

#if DEBUG_MODE
#define FOCUS_TIME_SECONDS    10  // 10 seconds for testing
#define BREAK_TIME_SECONDS    5   // 5 seconds for testing
#else
#define FOCUS_TIME_SECONDS    (25 * 60)  // 25 minutes
#define BREAK_TIME_SECONDS    (5 * 60)   // 5 minutes
#endif

// Global Pomodoro Variables
static pomodoro_state_t current_state = POMODORO_IDLE;
static pomodoro_state_t next_state = POMODORO_FOCUS;
static int remaining_seconds = 0;
static lv_timer_t *countdown_timer = NULL;
static lv_obj_t *current_screen = NULL;
static lv_obj_t *progress_arc = NULL;
static lv_obj_t *time_label = NULL;
static lv_obj_t *state_label = NULL;
static bool is_focus_session = true; // Track if next session is focus or break

// Forward declarations
void create_focus_screen();
void create_break_screen();
void create_transition_screen();
void create_idle_screen();

// Function to get random exercise based on probabilities
const char* get_random_exercise() {
    int random_num = random(100); // Generate random number 0-99
    
    if (random_num < 10) {
        return "Do some situps!";
    } else if (random_num < 50) { // 10 + 40 = 50
        return "Do some pushups!";
    } else if (random_num < 60) { // 50 + 10 = 60
        return "Hold a plank!";
    } else if (random_num < 80) { // 60 + 20 = 80
        return "Do jumping jacks!";
    } else {
        return "Do some squats!";
    }
}

#if LV_USE_LOG != 0
/* Serial debugging */
void my_print(const char * buf)
{
    Serial.printf(buf);
    Serial.flush();
}
#endif

/* Display flushing */
void my_disp_flush( lv_disp_drv_t *disp_drv, const lv_area_t *area, lv_color_t *color_p )
{
    uint32_t w = ( area->x2 - area->x1 + 1 );
    uint32_t h = ( area->y2 - area->y1 + 1 );

    tft.startWrite();
    tft.setAddrWindow( area->x1, area->y1, w, h );
    tft.pushColors( ( uint16_t * )&color_p->full, w * h, true );
    tft.endWrite();

    lv_disp_flush_ready( disp_drv );
}

void example_increase_lvgl_tick(void *arg)
{
    /* Tell LVGL how many milliseconds has elapsed */
    lv_tick_inc(EXAMPLE_LVGL_TICK_PERIOD_MS);
}



/*Read the touchpad*/
void my_touchpad_read( lv_indev_drv_t * indev_drv, lv_indev_data_t * data )
{
    bool touched = touch.available();
    if( !touched )
    {
        data->state = LV_INDEV_STATE_REL;
    }
    else
    {
        data->state = LV_INDEV_STATE_PR;
        data->point.x = touch.data.x;
        data->point.y = touch.data.y;
    }
}

// Format seconds to MM:SS string
void format_time(int seconds, char* buffer) {
    if (seconds < 0) seconds = 0; // Prevent negative time display
    int minutes = seconds / 60;
    int secs = seconds % 60;
    sprintf(buffer, "%02d:%02d", minutes, secs);
}

// Countdown timer callback - called every second
void countdown_timer_cb(lv_timer_t * timer) {
    if (remaining_seconds > 0) {
        remaining_seconds--;
        
        // Update time display
        char time_str[6];
        format_time(remaining_seconds, time_str);
        lv_label_set_text(time_label, time_str);
        
        // Update progress arc (decreases from left to right, 270° arc)
        int total_time = (current_state == POMODORO_FOCUS) ? FOCUS_TIME_SECONDS : BREAK_TIME_SECONDS;
        float progress_ratio = (float)remaining_seconds / total_time; // remaining/total for decreasing effect
        int start_angle = 405 - (int)(progress_ratio * 270); // Start moves from left (135°) toward right (405°)
        int end_angle = 405; // Always end at right side (405°)
        lv_arc_set_angles(progress_arc, start_angle, end_angle);
        
        Serial.printf("Time remaining: %s\n", time_str);
    } else {
        // Timer finished - go to transition state
        Serial.println("Timer finished!");
        lv_timer_pause(countdown_timer);
        
        // Determine next state
        if (current_state == POMODORO_FOCUS) {
            next_state = POMODORO_BREAK;
            is_focus_session = false;
        } else if (current_state == POMODORO_BREAK) {
            next_state = POMODORO_FOCUS;
            is_focus_session = true;
        }
        
        current_state = POMODORO_TRANSITION;
        create_transition_screen();
    }
}

// Touch event handler for screens
void screen_touch_event_cb(lv_event_t * e) {
    lv_event_code_t code = lv_event_get_code(e);
    
    if (code == LV_EVENT_CLICKED) {
        Serial.printf("Screen touched in state: %d\n", current_state);
        
        if (current_state == POMODORO_FOCUS || current_state == POMODORO_BREAK) {
            // Skip current timer and go to transition
            lv_timer_pause(countdown_timer);
            
            if (current_state == POMODORO_FOCUS) {
                next_state = POMODORO_BREAK;
                is_focus_session = false;
            } else {
                next_state = POMODORO_FOCUS;
                is_focus_session = true;
            }
            
            current_state = POMODORO_TRANSITION;
            create_transition_screen();
        } else if (current_state == POMODORO_TRANSITION) {
            // Start next timer session
            current_state = next_state;
            if (current_state == POMODORO_FOCUS) {
                create_focus_screen();
            } else {
                create_break_screen();
            }
        } else if (current_state == POMODORO_IDLE) {
            // Start first focus session
            current_state = POMODORO_FOCUS;
            is_focus_session = true;
            create_focus_screen();
        }
    }
}

// Clean up current screen
void cleanup_current_screen() {
    if (current_screen != NULL) {
        lv_obj_del(current_screen);
        current_screen = NULL;
        progress_arc = NULL;
        time_label = NULL;
        state_label = NULL;
    }
}

// Create focus screen (red theme)
void create_focus_screen() {
    cleanup_current_screen();
    
    remaining_seconds = FOCUS_TIME_SECONDS;
    
    // Create main container
    current_screen = lv_obj_create(lv_scr_act());
    lv_obj_set_size(current_screen, screenWidth, screenHeight);
    lv_obj_set_style_bg_color(current_screen, lv_color_hex(0x8B0000), 0); // Dark red
    lv_obj_set_style_border_width(current_screen, 0, 0);
    lv_obj_add_event_cb(current_screen, screen_touch_event_cb, LV_EVENT_CLICKED, NULL);
    
    // Create circular progress arc
    progress_arc = lv_arc_create(current_screen);
    lv_obj_set_size(progress_arc, 180, 180);
    lv_obj_center(progress_arc);
    lv_arc_set_range(progress_arc, 0, 100);
    lv_arc_set_value(progress_arc, 0);
    lv_arc_set_bg_angles(progress_arc, 135, 405); // Background arc: 270° from left to right (135° to 405°)
    lv_arc_set_angles(progress_arc, 135, 405); // Indicator starts full (135° to 405° = 270° arc)
    lv_obj_set_style_arc_color(progress_arc, lv_color_hex(0xFF6B6B), LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(progress_arc, 8, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(progress_arc, lv_color_hex(0x4A0000), LV_PART_MAIN);
    lv_obj_set_style_arc_width(progress_arc, 8, LV_PART_MAIN);
    lv_obj_remove_style(progress_arc, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(progress_arc, LV_OBJ_FLAG_CLICKABLE);
    
    // Create state label
    state_label = lv_label_create(current_screen);
    lv_label_set_text(state_label, "FOCUS");
    lv_obj_set_style_text_color(state_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(state_label, LV_FONT_DEFAULT, 0);
    lv_obj_align(state_label, LV_ALIGN_CENTER, 0, -40);
    
    // Create time label
    time_label = lv_label_create(current_screen);
    char time_str[6];
    format_time(remaining_seconds, time_str);
    lv_label_set_text(time_label, time_str);
    lv_obj_set_style_text_color(time_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(time_label, LV_FONT_DEFAULT, 0);
    lv_obj_align(time_label, LV_ALIGN_CENTER, 0, 10);
    
    // Create instruction label
    lv_obj_t *instruction_label = lv_label_create(current_screen);
    lv_label_set_text(instruction_label, "Tap to skip");
    lv_obj_set_style_text_color(instruction_label, lv_color_hex(0xFFAAAA), 0);
    lv_obj_set_style_text_font(instruction_label, LV_FONT_DEFAULT, 0);
    lv_obj_align(instruction_label, LV_ALIGN_BOTTOM_MID, 0, -20);
    
    // Start countdown timer
    if (countdown_timer == NULL) {
        countdown_timer = lv_timer_create(countdown_timer_cb, 1000, NULL);
    } else {
        lv_timer_resume(countdown_timer);
    }
    
    Serial.println("Focus screen created");
}

// Create break screen (green theme)
void create_break_screen() {
    cleanup_current_screen();
    
    remaining_seconds = BREAK_TIME_SECONDS;
    
    // Create main container
    current_screen = lv_obj_create(lv_scr_act());
    lv_obj_set_size(current_screen, screenWidth, screenHeight);
    lv_obj_set_style_bg_color(current_screen, lv_color_hex(0x006400), 0); // Dark green
    lv_obj_set_style_border_width(current_screen, 0, 0);
    lv_obj_add_event_cb(current_screen, screen_touch_event_cb, LV_EVENT_CLICKED, NULL);
    
    // Create circular progress arc
    progress_arc = lv_arc_create(current_screen);
    lv_obj_set_size(progress_arc, 180, 180);
    lv_obj_center(progress_arc);
    lv_arc_set_range(progress_arc, 0, 100);
    lv_arc_set_value(progress_arc, 0);
    lv_arc_set_bg_angles(progress_arc, 135, 405); // Background arc: 270° from left to right (135° to 405°)
    lv_arc_set_angles(progress_arc, 135, 405); // Indicator starts full (135° to 405° = 270° arc)
    lv_obj_set_style_arc_color(progress_arc, lv_color_hex(0x90EE90), LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(progress_arc, 8, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(progress_arc, lv_color_hex(0x003200), LV_PART_MAIN);
    lv_obj_set_style_arc_width(progress_arc, 8, LV_PART_MAIN);
    lv_obj_remove_style(progress_arc, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(progress_arc, LV_OBJ_FLAG_CLICKABLE);
    
    // Create state label
    state_label = lv_label_create(current_screen);
    lv_label_set_text(state_label, "BREAK");
    lv_obj_set_style_text_color(state_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(state_label, LV_FONT_DEFAULT, 0);
    lv_obj_align(state_label, LV_ALIGN_CENTER, 0, -40);
    
    // Create time label
    time_label = lv_label_create(current_screen);
    char time_str[6];
    format_time(remaining_seconds, time_str);
    lv_label_set_text(time_label, time_str);
    lv_obj_set_style_text_color(time_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(time_label, LV_FONT_DEFAULT, 0);
    lv_obj_align(time_label, LV_ALIGN_CENTER, 0, 10);
    
    // Create exercise suggestion label
    lv_obj_t *exercise_label = lv_label_create(current_screen);
    lv_label_set_text(exercise_label, get_random_exercise());
    lv_obj_set_style_text_color(exercise_label, lv_color_hex(0xFFFFAA), 0); // Light yellow
    lv_obj_set_style_text_font(exercise_label, LV_FONT_DEFAULT, 0);
    lv_obj_set_style_text_align(exercise_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(exercise_label, LV_ALIGN_CENTER, 0, 35); // Moved closer to timer (was 60)
    
    // Create instruction label
    lv_obj_t *instruction_label = lv_label_create(current_screen);
    lv_label_set_text(instruction_label, "Tap to skip");
    lv_obj_set_style_text_color(instruction_label, lv_color_hex(0xAAFFAA), 0);
    lv_obj_set_style_text_font(instruction_label, LV_FONT_DEFAULT, 0);
    lv_obj_align(instruction_label, LV_ALIGN_BOTTOM_MID, 0, -20);
    
    // Start countdown timer
    if (countdown_timer == NULL) {
        countdown_timer = lv_timer_create(countdown_timer_cb, 1000, NULL);
    } else {
        lv_timer_resume(countdown_timer);
    }
    
    Serial.println("Break screen created");
}

// Create transition screen (light blue theme)
void create_transition_screen() {
    cleanup_current_screen();
    
    // Create main container
    current_screen = lv_obj_create(lv_scr_act());
    lv_obj_set_size(current_screen, screenWidth, screenHeight);
    lv_obj_set_style_bg_color(current_screen, lv_color_hex(0x87CEEB), 0); // Sky blue
    lv_obj_set_style_border_width(current_screen, 0, 0);
    lv_obj_add_event_cb(current_screen, screen_touch_event_cb, LV_EVENT_CLICKED, NULL);
    
    // Create main message
    lv_obj_t *main_label = lv_label_create(current_screen);
    lv_label_set_text(main_label, "Good Job!");
    lv_obj_set_style_text_color(main_label, lv_color_hex(0x2F4F4F), 0);
    lv_obj_set_style_text_font(main_label, LV_FONT_DEFAULT, 0);
    lv_obj_align(main_label, LV_ALIGN_CENTER, 0, -40);
    
    // Create next state indicator
    lv_obj_t *next_label = lv_label_create(current_screen);
    if (next_state == POMODORO_FOCUS) {
        lv_label_set_text(next_label, "Next: FOCUS (25:00)");
    } else {
        lv_label_set_text(next_label, "Next: BREAK (05:00)");
    }
    lv_obj_set_style_text_color(next_label, lv_color_hex(0x2F4F4F), 0);
    lv_obj_set_style_text_font(next_label, LV_FONT_DEFAULT, 0);
    lv_obj_align(next_label, LV_ALIGN_CENTER, 0, 0);
    
    // Create instruction label
    lv_obj_t *instruction_label = lv_label_create(current_screen);
    lv_label_set_text(instruction_label, "Tap to continue");
    lv_obj_set_style_text_color(instruction_label, lv_color_hex(0x4682B4), 0);
    lv_obj_set_style_text_font(instruction_label, LV_FONT_DEFAULT, 0);
    lv_obj_align(instruction_label, LV_ALIGN_CENTER, 0, 40);
    
    Serial.println("Transition screen created");
}

// Create initial idle screen
void create_idle_screen() {
    cleanup_current_screen();
    
    // Create main container
    current_screen = lv_obj_create(lv_scr_act());
    lv_obj_set_size(current_screen, screenWidth, screenHeight);
    lv_obj_set_style_bg_color(current_screen, lv_color_hex(0x2C3E50), 0); // Dark blue-gray
    lv_obj_set_style_border_width(current_screen, 0, 0);
    lv_obj_add_event_cb(current_screen, screen_touch_event_cb, LV_EVENT_CLICKED, NULL);
    
    // Create title
    lv_obj_t *title_label = lv_label_create(current_screen);
    lv_label_set_text(title_label, "POMODORO");
    lv_obj_set_style_text_color(title_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(title_label, LV_FONT_DEFAULT, 0);
    lv_obj_align(title_label, LV_ALIGN_CENTER, 0, -40);
    
    // Create subtitle
    lv_obj_t *subtitle_label = lv_label_create(current_screen);
    lv_label_set_text(subtitle_label, "TIMER");
    lv_obj_set_style_text_color(subtitle_label, lv_color_hex(0xBDC3C7), 0);
    lv_obj_set_style_text_font(subtitle_label, LV_FONT_DEFAULT, 0);
    lv_obj_align(subtitle_label, LV_ALIGN_CENTER, 0, -10);
    
    // Create instruction
    lv_obj_t *instruction_label = lv_label_create(current_screen);
    lv_label_set_text(instruction_label, "Tap to start\nfirst focus session");
    lv_obj_set_style_text_color(instruction_label, lv_color_hex(0x95A5A6), 0);
    lv_obj_set_style_text_font(instruction_label, LV_FONT_DEFAULT, 0);
    lv_obj_set_style_text_align(instruction_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(instruction_label, LV_ALIGN_CENTER, 0, 30);
    
    Serial.println("Idle screen created");
}

void setup()
{
    Serial.begin( 115200 ); /* prepare for possible serial debug */

    String LVGL_Arduino = "Pomodoro Timer ";
    LVGL_Arduino += String('V') + lv_version_major() + "." + lv_version_minor() + "." + lv_version_patch();

    Serial.println( LVGL_Arduino );
    Serial.println( "Starting Pomodoro Timer" );

    lv_init();
#if LV_USE_LOG != 0
    lv_log_register_print_cb( my_print ); /* register print function for debugging */
#endif

    tft.begin();          /* TFT init */
    tft.setRotation( 0 ); /* Portrait orientation */
    
    touch.begin();

    lv_disp_draw_buf_init( &draw_buf, buf, NULL, screenWidth * screenHeight / 10 );

    /*Initialize the display*/
    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init( &disp_drv );
    disp_drv.hor_res = screenWidth;
    disp_drv.ver_res = screenHeight;
    disp_drv.flush_cb = my_disp_flush;
    disp_drv.draw_buf = &draw_buf;
    lv_disp_drv_register( &disp_drv );

    /*Initialize the input device driver*/
    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init( &indev_drv );
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = my_touchpad_read;
    lv_indev_drv_register( &indev_drv );
  
    // Setup LVGL tick timer
    const esp_timer_create_args_t lvgl_tick_timer_args = {
      .callback = &example_increase_lvgl_tick,
      .name = "lvgl_tick"
    };

    esp_timer_handle_t lvgl_tick_timer = NULL;
    esp_timer_create(&lvgl_tick_timer_args, &lvgl_tick_timer);
    esp_timer_start_periodic(lvgl_tick_timer, EXAMPLE_LVGL_TICK_PERIOD_MS * 1000);

    // Create initial idle screen
    create_idle_screen();
    
    Serial.println( "Setup done" );
}

void loop()
{
    lv_timer_handler(); /* let the GUI do its work */
    delay( 5 );
}
