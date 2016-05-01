#include "./engine.h"

#include <stdio.h>

#include <vector>
#include <allegro5/allegro_image.h>

static inline float lerp(float v0, float v1, float t) {
  return (1 - t) * v0 + t * v1;
}

struct Star {
  const char *name;
  float x, y;
  float wx = 0;
  float wy = 0;

  Star(const char *_name, float _x, float _y) {
    name = _name; x = _x; y = _y;
  }

  void draw(float offx, float offy) {
    ImGui::SetNextWindowPos(ImVec2(x - offx - wx/2, y - offy - wy/2));
    ImGui::Begin(name, NULL, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);
    ImGui::Button(name);
    if(ImGui::IsItemHovered()) {
      ImGui::BeginTooltip();
      ImGui::Text("tooltip");
      ImGui::EndTooltip();
    }
    wy = ImGui::GetWindowHeight();
    wx = ImGui::GetWindowWidth();
    ImGui::End();
  }
};

struct StarGraph {
  std::vector<std::pair<Star *, Star *>> connections;

  void draw(float offx, float offy) {
    for(auto&& c : connections) {
      al_draw_line(c.first->x - offx, c.first->y - offy, c.second->x - offx, c.second->y - offy, al_map_rgb(255,255,255), 3);
    }
  }
};

struct ObservableEvent {
  ObservableEvent(float _x, float _y) { x = _x; y = _y; t = 0; }

  float x, y;
  float t;
};

struct Observations {
  std::vector<ObservableEvent> events;
  std::vector<Star *> observers;
  std::vector<ObservableEvent> seeing;

  void add(ObservableEvent&& e) {
    events.emplace_back(e);
  }

  void update() {
    std::vector<ObservableEvent>::iterator event;
    seeing.clear();

    for(event = events.begin(); event != events.end();) {
      event->t += 1;

      bool all_reached = true;

      for(auto&& observer : observers) {
	float distance_squared =
 	  (event->x - observer->x) * (event->x - observer->x) +
	  (event->y - observer->y) * (event->y - observer->y);
	float wave_distance_squared =
	  event->t * event->t * 2500;

	bool reached = distance_squared < wave_distance_squared;

	all_reached = all_reached && reached;

	printf("%f < %f?\n", distance_squared, wave_distance_squared);
      }

      if(all_reached == true) {
	seeing.push_back(*event);
	event = events.erase(event);
	printf("deleting event\n");
      }
      else {
	event++;
      }
    }
    printf("--\n");
  }

  void draw(float offx, float offy) {
    for(auto&& event : events) {
      al_draw_filled_circle(event.x - offx, event.y - offy, 5, al_map_rgb(100, 100, 255));
      al_draw_circle(event.x - offx, event.y - offy, event.t * 50, al_map_rgb(100, 100, 255), 2);
    }
    for(auto&& event : seeing) {
      al_draw_filled_circle(event.x - offx, event.y - offy, 10, al_map_rgb(100, 100, 255));
    }
  }
};

struct Stars {
  std::vector<Star> stars;
  StarGraph graph;
  ALLEGRO_BITMAP *circle_buf;

  void init() {
    circle_buf = al_create_bitmap(720, 480);
    assert(circle_buf);

    stars.push_back(Star("Sol", 100, 100));
    stars.push_back(Star("Procyon", 250, 0));
    stars.push_back(Star("Epsilon Eridani", 300, 200));
    stars.push_back(Star("Tau Ceti", 200, 150));
    stars.push_back(Star("Lalande", 50, 350));
    Star& sol = stars[0];
    Star& procyon = stars[1];
    Star& epsiloneridani = stars[2];
    Star& tauceti = stars[3];
    Star& lalande = stars[4];
    graph.connections.push_back(std::make_pair(&sol, &tauceti));
    graph.connections.push_back(std::make_pair(&tauceti, &lalande));
    graph.connections.push_back(std::make_pair(&tauceti, &epsiloneridani));
    graph.connections.push_back(std::make_pair(&sol, &procyon));
    graph.connections.push_back(std::make_pair(&procyon, &tauceti));
  }

  void draw(Engine &e, float vx, float vy) {
    al_set_target_bitmap(circle_buf);
    al_clear_to_color(al_map_rgb(0,0,0));
    for(auto&& star : stars) {
      al_draw_filled_circle(star.x - vx, star.y - vy, 130, al_map_rgb(100, 255, 255));
    }
    al_set_target_backbuffer(e.display);
    const int i = 50;
    al_draw_tinted_bitmap(circle_buf, al_map_rgba(i, i, i , 30), 0, 0, 0);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2, 0.2, 0.2, 1.0));
    for(auto&& star : stars) {
      star.draw(vx, vy);
    }
    ImGui::PopStyleColor();
    graph.draw(vx, vy);
  }
};

struct Fleet {
  float x, y, t;

  Star *source;
  Star *destination; // NULL if in star system

  Fleet(Star &s) {
    source = &s;
    destination = NULL;
  }

  void draw(float offx, float offy) {
    if(destination != NULL) {
      al_draw_line(source->x - offx, source->y - offy, destination->x - offx, destination->y - offy, al_map_rgb(200, 20, 20), 3);
    }
    al_draw_filled_circle(x - offx, y - offy, 10, al_map_rgb(200, 20, 20));
  }

  void move_to(Star &d) {
    destination = &d;
    t = 0;
  }

  void update() {
    if(destination == NULL) {
      // docked in star system
      return;
    }
    else {
      // travelling
      t += 0.1;

      if(t >= 1) {
	// we've arrived
	source = destination;
	x = source->x;
	y = source->y;
	destination = NULL;
	t = 0;
      }
      else {
	x = lerp(source->x, destination->x, t);
	y = lerp(source->y, destination->y, t);
      }
    }
  }
};


struct Game {
  ALLEGRO_KEYBOARD_STATE keyboard;
  int t;
  const float scroll_speed = 3;

  // TODO should be in Engine?
  float vx, vy;

  Stars stars;
  Observations obs;
  Fleet f;

  Game(float _vx, float _vy) {
    vx = _vx;
    vy = _vy;
  }

  void handle_panning() {
    al_get_keyboard_state(&keyboard);
    if(al_key_down(&keyboard, ALLEGRO_KEY_LEFT)) {
      vx -= scroll_speed;
    }
    else if(al_key_down(&keyboard, ALLEGRO_KEY_RIGHT)) {
      vx += scroll_speed;
    }
    if(al_key_down(&keyboard, ALLEGRO_KEY_UP)) {
      vy -= scroll_speed;
    }
    else if(al_key_down(&keyboard, ALLEGRO_KEY_DOWN)) {
      vy += scroll_speed;
    }
  }

  void tick() {
    t++;
    // Star& eps = stars.stars[2];
    // obs.add(ObservableEvent(eps.x, eps.y));
    // Star& la = stars.stars[4];
    // obs.add(ObservableEvent(la.x, la.y));

    f.update();
    obs.add(ObservableEvent(f.x, f.y));

    g.tick();
    obs.update();
  }
};

int main() {
  Engine e("2.7 Kelvin", 720, 480);
  e.init();
  Game g(0, 0);
  g.vx = -140;
  g.vy = -70;
  al_init_image_addon();
  ALLEGRO_BITMAP *bg = al_load_bitmap("./bg.png");
  assert(bg);
  bool show_event_circles = true;

  ImGuiIO& io = ImGui::GetIO();
  io.Fonts->AddFontFromFileTTF("DroidSans.ttf", 18.0);
  // ImFont *big_font = io.Fonts->AddFontFromFileTTF("DroidSans.ttf", 24.0);

  stars.init();

  obs.observers.push_back(&stars.stars[0]);

  f.move_to(stars.stars[1]);

  while (e.running) {
    e.begin_frame();
    g.handle_panning();

    // e.clear();
    al_draw_bitmap(bg, 0, 0, 0);
    stars.draw(e, g.vx, g.vy);

    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.2,0.2,0.2,1.0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0);
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::Begin("menu", NULL, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove );
    ImGui::Button("Menu");
    ImGui::SameLine();
    ImGui::Button("Research");
    ImGui::SameLine();
    ImGui::Button("Fleet");
    ImGui::SameLine();
    ImGui::Button("Diplomacy");
    ImGui::SameLine();
    if(ImGui::Button("Debug")) { e.debug_win ^= 1; }
    int y = ImGui::GetWindowHeight();
    ImGui::SameLine();
    ImGui::End();

    ImGui::SetNextWindowPos(ImVec2(0, 5 + y));
    ImGui::Begin("timekeeper", NULL, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove );
    // ImGui::PushFont(big_font);
    ImGui::Text("Federation, Year: %d   ", g.t);
    // ImGui::PopFont();
    // ImGui::Separator();
    // ImGui::Text("Population: 546b");
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();

    if(e.debug_win == true) {
      ImGui::Begin("Debug", &e.debug_win);
      ImGui::Text("Viewport x: %0.f", g.vx);
      ImGui::Text("Viewport y: %0.f", g.vy);
      ImGui::Text("Stars: %ld", stars.stars.size());
      ImGui::Text("Travelling Events: %ld", obs.events.size());
      ImGui::Checkbox("Show Event Circles", &show_event_circles);
      ImGui::End();
    }

    f.draw(g.vx, g.vy);
    if(show_event_circles == true) {
      obs.draw(g.vx, g.vy);
    }

    e.end_frame();

    if(e.paused == true) {
      continue;
    }
    e.frame++;

    if(e.frame % 30 == 0) {
    }
  }

  e.stop();
  return 0;
}
