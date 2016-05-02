#include "./engine.h"

#include <vector>

ImFont *bigger;

Engine::Engine(const char *win_title, float win_sx, float win_sy) {
  title = win_title;
  sx = win_sx;
  sy = win_sy;
  draw_background = true;
}

void Engine::init() {
  // Setup Allegro
  al_init();
  al_install_keyboard();
  al_install_mouse();
  al_init_primitives_addon();
  al_init_image_addon();
  al_set_new_display_flags(ALLEGRO_RESIZABLE);
  display = al_create_display(sx, sy);
  al_set_window_title(display, title);
  event_queue = al_create_event_queue();
  al_register_event_source(event_queue, al_get_display_event_source(display));
  al_register_event_source(event_queue, al_get_keyboard_event_source());
  al_register_event_source(event_queue, al_get_mouse_event_source());

  clear_color = ImColor(15, 15, 15);
  paused = false;
  running = true;
  debug_win = true;

  // Setup ImGui binding
  ImGui_ImplA5_Init(display);

  resize_window();

  ImGuiIO& io = ImGui::GetIO();
  io.Fonts->AddFontFromFileTTF("DroidSans.ttf", 18.0);
  bigger = io.Fonts->AddFontFromFileTTF("DroidSans.ttf", 22.0);

  frame = 0;
}

void Engine::resize_window() {
  sx = al_get_display_width(display);
  sy = al_get_display_height(display);
  x_scale = sx / 720;
  y_scale = sy / 480;
  scale = std::min(x_scale, y_scale);
}

void Engine::begin_frame() {
  ALLEGRO_EVENT ev;
  while (al_get_next_event(event_queue, &ev)) {
    ImGui_ImplA5_ProcessEvent(&ev);

    if (ev.type == ALLEGRO_EVENT_DISPLAY_CLOSE) {
      running = false;
    }
    else if (ev.type == ALLEGRO_EVENT_DISPLAY_RESIZE) {
      resize_window();
      ImGui_ImplA5_InvalidateDeviceObjects();
      al_acknowledge_resize(display);
      Imgui_ImplA5_CreateDeviceObjects();
    }
    else if(ev.type == ALLEGRO_EVENT_KEY_DOWN) {
      int key = ev.keyboard.keycode;
      if(key == ALLEGRO_KEY_ESCAPE or key == ALLEGRO_KEY_Q) {
	running = false;
      }
      if(key == ALLEGRO_KEY_P) {
	paused ^= 1;
      }
      if(key == ALLEGRO_KEY_D) {
	debug_win ^= 1;
      }
    }
  }
  ImGui_ImplA5_NewFrame();
}

void Engine::clear() {
  al_clear_to_color(al_map_rgba_f(clear_color.x, clear_color.y, clear_color.z, clear_color.w));
}

void Engine::end_frame() {
  // Rendering
  ImGui::Render();
     
  al_flip_display();
}

void Engine::stop() {
  // Cleanup
  ImGui_ImplA5_Shutdown();
  al_destroy_event_queue(event_queue);
  al_destroy_display(display);
}

void Engine::draw(IDrawable &drawable) {
  drawable.draw(*this);
}
