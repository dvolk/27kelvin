#pragma once

#include <allegro5/allegro.h>
#include <allegro5/allegro_primitives.h>
#include <allegro5/allegro_image.h>

#include "../../lib/imgui/imgui.h"
#include "imgui_impl_a5/imgui_impl_a5.h"

struct IDrawable;

struct Engine {
  const char *title;
  float sx, sy;
  float x_scale;
  float y_scale;
  float scale;
  int frame;

  ALLEGRO_DISPLAY *display;
  ALLEGRO_EVENT_QUEUE *event_queue;
  ImVec4 clear_color;
  bool paused;
  bool running;
  bool debug_win;

  Engine(const char *win_title, float win_sx, float win_sy);
  void init();
  void resize_window();
  void begin_frame();
  void clear();
  void end_frame();
  void stop();

  void draw(IDrawable &drawable);
};

struct IDrawable {
  virtual void draw(Engine &e) = 0;
};
