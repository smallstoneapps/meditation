/*
 * Meditation
 * Copyright (C) 2013 Matthew Tole
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include "pebble_os.h"
#include "pebble_app.h"
#include "pebble_fonts.h"

#include "config.h"

#if ROCKSHOT
#include "http.h"
#include "httpcapture.h"
#endif

#define MY_UUID { 0xA1, 0x4E, 0x82, 0x26, 0xBA, 0x2C, 0x49, 0x61, 0x97, 0xD8, 0x70, 0xE2, 0xFA, 0xFC, 0x03, 0x37 }

PBL_APP_INFO(MY_UUID, "Meditation Timer", "Matthew Tole", 1, 0, RESOURCE_ID_MENU_ICON, APP_INFO_STANDARD_APP);

#define PEBBLE_HEIGHT 168
#define STATUS_HEIGHT 16
#define WINDOW_HEIGHT PEBBLE_HEIGHT - STATUS_HEIGHT
#define WINDOW_WIDTH 144

#define MAX_DURATION 99
#define MIN_DURATION 5
#define MAX_INTERVAL 10
#define MIN_INTERVAL 1

#define COOKIE_COUNTDOWN 100

#define MODE_STOPPED 0
#define MODE_RUNNING 1
#define MODE_PAUSED 2
#define MODE_FINISHED 3

void load_bitmaps();
void unload_bitmaps();
void handle_init(AppContextRef ctx);
void handle_deinit(AppContextRef ctx);
void set_timer(AppContextRef ctx);
void update_timer_text();
void handle_timer(AppContextRef ctx, AppTimerHandle handle, uint32_t cookie);
void window_main_load(Window* window);
void window_main_unload(Window* window);
void window_main_click_config_provider(ClickConfig **config, Window *window);
void window_main_select_clicked(ClickRecognizerRef recognizer, Window *window);
void window_main_up_clicked(ClickRecognizerRef recognizer, Window *window);

void numwin_duration_selected(struct NumberWindow *number_window, void *context);
void numwin_interval_selected(struct NumberWindow *number_window, void *context);

void do_vibration();
void start_timer(AppContextRef ctx);
void resume_timer(AppContextRef ctx);
void reset_timer(AppContextRef ctx);
void pause_timer(AppContextRef ctx);
void init_timer(AppContextRef ctx);
void stop_timer(AppContextRef ctx);
void update_actionbar_icons();

#if ROCKSHOT
void http_success(int32_t cookie, int http_status, DictionaryIterator *dict, void *ctx);
#endif

HeapBitmap bmp_action_start;
HeapBitmap bmp_action_pause;
HeapBitmap bmp_action_restart;

AppContextRef app_ctx;
Window window_main;
NumberWindow numwin_duration;
NumberWindow numwin_interval;

TextLayer text_layer;
ActionBarLayer action_bar;

AppTimerHandle timer_handle;

int duration = 20;
int interval = 5;
int mode = MODE_STOPPED;

int time_remaining;

void pbl_main(void *params) {
  PebbleAppHandlers handlers = {
    .init_handler = &handle_init,
    .deinit_handler = &handle_deinit,
    .timer_handler = &handle_timer
  };
  
  #if ROCKSHOT
  handlers.messaging_info = (PebbleAppMessagingInfo) {
    .buffer_sizes = {
      .inbound = 124,
      .outbound = 124,
    },
  };
  http_capture_main(&handlers);
  #endif

  app_event_loop(params, &handlers);
}

void load_bitmaps() {
  heap_bitmap_init(&bmp_action_start, RESOURCE_ID_ACTION_START);
  heap_bitmap_init(&bmp_action_pause, RESOURCE_ID_ACTION_PAUSE);
  heap_bitmap_init(&bmp_action_restart, RESOURCE_ID_ACTION_RESTART);
}

void unload_bitmaps() {
  heap_bitmap_deinit(&bmp_action_start);
  heap_bitmap_deinit(&bmp_action_pause);
  heap_bitmap_deinit(&bmp_action_restart);
}

void handle_init(AppContextRef ctx) {
  app_ctx = ctx;

  resource_init_current_app(&APP_RESOURCES);
  load_bitmaps();

  #if ROCKSHOT
  http_set_app_id(15);
  http_register_callbacks((HTTPCallbacks) {
    .success = http_success
  }, NULL);
  http_capture_init(ctx);
  #endif

  window_init(&window_main, "Matthew Tole");
  window_set_window_handlers(&window_main, (WindowHandlers) {
    .load = (WindowHandler)window_main_load,
    .unload = (WindowHandler)window_main_unload,
  });
  action_bar_layer_init(&action_bar);

  text_layer_init(&text_layer, GRect(0, 53, 144 - ACTION_BAR_WIDTH, 50));
  text_layer_set_text_color(&text_layer, GColorBlack);
  text_layer_set_background_color(&text_layer, GColorClear);
  text_layer_set_font(&text_layer, fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_OPENSANS_BOLD_40)));
  text_layer_set_text_alignment(&text_layer, GTextAlignmentCenter);
  layer_add_child(&window_main.layer, &text_layer.layer);

  number_window_init(&numwin_duration, "Meditation Duration", (NumberWindowCallbacks) {
    .decremented = NULL,
    .incremented = NULL,
    .selected = (NumberWindowCallback) numwin_duration_selected
  }, NULL);
  number_window_set_value(&numwin_duration, duration);
  number_window_set_min(&numwin_duration, MIN_DURATION);
  number_window_set_max(&numwin_duration, MAX_DURATION);

  number_window_init(&numwin_interval, "Vibration Interval", (NumberWindowCallbacks) {
    .decremented = NULL,
    .incremented = NULL,
    .selected = (NumberWindowCallback) numwin_interval_selected
  }, NULL);
  number_window_set_value(&numwin_interval, interval);
  number_window_set_min(&numwin_interval, MIN_INTERVAL);
  number_window_set_max(&numwin_interval, MAX_INTERVAL);

  window_stack_push((Window*)&numwin_duration, true);
}

void window_main_load(Window* window) {
  action_bar_layer_add_to_window(&action_bar, window);
  update_actionbar_icons();
  init_timer(app_ctx);
}

void window_main_unload(Window* window) {
  stop_timer(app_ctx);
  Window* tmp2 = layer_get_window((Layer*)&action_bar);
  action_bar_layer_remove_from_window(&action_bar);
  Window* tmp = layer_get_window((Layer*)&action_bar);
}

void handle_timer(AppContextRef ctx, AppTimerHandle handle, uint32_t cookie) {
  if (cookie == COOKIE_COUNTDOWN) {
    time_remaining -= 1;
    update_timer_text();
    if (time_remaining == 0) {
      mode = MODE_FINISHED;
      vibes_long_pulse();
      update_actionbar_icons();
    }
    else {
      if (time_remaining % 60 == 0) {
        do_vibration();
      }
      if (mode == MODE_RUNNING) {
        set_timer(ctx);
      }
    }
  }
}

void do_vibration() {
  int minutes_passed = ((duration * 60) - time_remaining) / 60;
  if (minutes_passed % interval == 0) {
    vibes_short_pulse();
  }
}

void handle_deinit(AppContextRef ctx) {
  unload_bitmaps();
}

void update_timer_text() {
  static char countdown_text[] = "00:00";
  snprintf(countdown_text, sizeof(countdown_text), "%02d:%02d", (time_remaining / 60), (time_remaining % 60));
  text_layer_set_text(&text_layer, countdown_text);
}

void set_timer(AppContextRef ctx) {
  timer_handle = app_timer_send_event(ctx, 1000, COOKIE_COUNTDOWN);
}

void cancel_timer(AppContextRef ctx) {
  app_timer_cancel_event(ctx, timer_handle);
}

void init_timer(AppContextRef ctx) {
  time_remaining = duration * 60;
  mode = MODE_STOPPED;
  update_timer_text();

  update_actionbar_icons();
}

void start_timer(AppContextRef ctx) {
  mode = MODE_RUNNING;
  update_timer_text();
  set_timer(ctx);
  update_actionbar_icons();
}

void resume_timer(AppContextRef ctx) {
  mode = MODE_RUNNING;
  set_timer(ctx);
  update_actionbar_icons();
}

void pause_timer(AppContextRef ctx) {
  cancel_timer(ctx);
  mode = MODE_PAUSED;
  update_actionbar_icons();
}

void reset_timer(AppContextRef ctx) {
  pause_timer(ctx);
  init_timer(ctx);
}

void stop_timer(AppContextRef ctx) {
  cancel_timer(ctx);
  mode = MODE_STOPPED;
  update_actionbar_icons();
}

void window_main_click_config_provider(ClickConfig **config, Window *window) {
  if (mode == MODE_RUNNING || mode == MODE_PAUSED || mode == MODE_STOPPED) {
    config[BUTTON_ID_SELECT]->click.handler = (ClickHandler) window_main_select_clicked;
  }
  if (mode == MODE_PAUSED || mode == MODE_FINISHED) {
    config[BUTTON_ID_UP]->click.handler = (ClickHandler) window_main_up_clicked;
  }
}

void update_actionbar_icons() {
  switch (mode) {
    case MODE_PAUSED: {
      action_bar_layer_set_icon(&action_bar, BUTTON_ID_SELECT, &bmp_action_start.bmp);
      action_bar_layer_set_icon(&action_bar, BUTTON_ID_UP, &bmp_action_restart.bmp);
    }
    break;
    case MODE_FINISHED: {
      action_bar_layer_clear_icon(&action_bar, BUTTON_ID_SELECT);
      action_bar_layer_set_icon(&action_bar, BUTTON_ID_UP, &bmp_action_restart.bmp);
    }
    break;
    case MODE_STOPPED: {
      action_bar_layer_set_icon(&action_bar, BUTTON_ID_SELECT, &bmp_action_start.bmp);
      action_bar_layer_clear_icon(&action_bar, BUTTON_ID_UP);
    }
    break;
    case MODE_RUNNING: {
      action_bar_layer_set_icon(&action_bar, BUTTON_ID_SELECT, &bmp_action_pause.bmp);
      action_bar_layer_clear_icon(&action_bar, BUTTON_ID_UP);
    }
    break;
  }

  action_bar_layer_set_click_config_provider(&action_bar, (ClickConfigProvider) window_main_click_config_provider);
}

void window_main_select_clicked(ClickRecognizerRef recognizer, Window *window) {
  switch (mode) {
    case MODE_RUNNING:
      pause_timer(app_ctx);
    break;
    case MODE_STOPPED:
      start_timer(app_ctx);
    break;
    case MODE_PAUSED:
      resume_timer(app_ctx);
    break;
  }
}

void window_main_up_clicked(ClickRecognizerRef recognizer, Window* window) {
  reset_timer(app_ctx);
}

void numwin_duration_selected(struct NumberWindow *number_window, void *context) {
  duration = number_window_get_value(&numwin_duration);
  window_stack_push((Window*)&numwin_interval, true);
}

void numwin_interval_selected(struct NumberWindow *number_window, void *context) {
  interval = number_window_get_value(&numwin_interval);
  window_stack_push(&window_main, true);
}

#if ROCKSHOT
void http_success(int32_t cookie, int http_status, DictionaryIterator *dict, void *ctx) {
}
#endif