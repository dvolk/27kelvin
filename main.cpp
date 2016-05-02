#include "./engine.h"

#include <stdio.h>
#include <vector>
#include <algorithm>
#include <memory>

const float PX_PER_LIGHTYEAR = 50;
const int TICKS_PER_SECOND = 2;

static inline float lerp(float v0, float v1, float t) {
  return (1 - t) * v0 + t * v1;
}

struct Fleet;
struct Star;
static inline const char *get_fleet_name(std::shared_ptr<Fleet> f);

std::weak_ptr<Fleet> g_selected_fleet;
Star *g_selected_star = NULL;
struct Observer;
void add_fleet_buttons_for_obs(Star *s, Observer *o);

struct Star {
  const char *name;
  // star position
  float x, y;
  // imgui window offset for centering
  float wx = 0;
  float wy = 0;
  int focus = 0;
  // fleets idle at star
  // std::vector<std::shared_ptr<Fleet>> system_fleets;

  Star(const char *_name, float _x, float _y) {
    name = _name; x = _x; y = _y;
  }

  // void addFleet(std::shared_ptr<Fleet> f) {
  //   system_fleets.push_back(f);
  // }

  // void removeFleet(std::shared_ptr<Fleet> f) {
  //   system_fleets.erase(std::find(system_fleets.begin(), system_fleets.end(), f));
  // }

  void draw(float offx, float offy, Observer *o) {
    ImGui::SetNextWindowPos(ImVec2(x - offx - wx/2, y - offy - wy/2));
    ImGui::Begin(name, NULL, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);
    bool pressed = ImGui::Button(name);
    if(pressed == true) {
      if(auto f = g_selected_fleet.lock()) {
	g_selected_star = this;
      }
      else {
	ImGui::OpenPopup("star menu");
      }
    }
    if(ImGui::BeginPopup("star menu")) {
      ImGui::Columns(2);
      ImGui::Text("%s             ", name);
      ImGui::Button("System Info");
      ImGui::NextColumn();
      ImGui::Text("Fleets:        ");
      add_fleet_buttons_for_obs(this, o);
      ImGui::EndPopup();
    }

    if(ImGui::IsItemHovered()) {
      ImGui::BeginTooltip();
      ImGui::Text("%s", name);
      ImGui::Separator();
      ImGui::Text("Population: 123 billion");
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

enum class ObservableEventType { FleetDeparture, FleetArrival, FleetIdle, OrderFleetMove };

struct Fleet;

struct ObservableEvent {
  ObservableEvent(ObservableEventType _type, float _x, float _y) {
    type = _type;
    x = _x;
    y = _y;
    t = 0;
  }

  ObservableEventType type;
  float x, y;
  float t;

  // should really be a union, but isn't for $reasons
  Star *orderTarget;
  Star *orderMoveTo;
  std::shared_ptr<Fleet> fleet1;
};

struct Observations;

struct FleetTrace {
  FleetTrace(float _x, float _y, float _r) { x = _x; y = _y; r = _r; }
  float x, y, r;
};

struct Fleet {
  float x, y;
  float t; // -1 if in star system
  float velocity = 0.12;
  float distance;
  int time_since_departure; // in years, presumably 1 update per year
  bool moving;

  Star *source;
  Star *destination;

  std::vector<FleetTrace> trace;

  const char *name;

  Fleet(const char *_name, Star &s) {
    name = _name;
    source = &s;
    x = source->x;
    y = source->y;
    destination = source;
    t = -1;
    moving = false;
  }

  void draw(float offx, float offy) {
    if(destination != NULL) {
      al_draw_line(source->x - offx, source->y - offy, destination->x - offx, destination->y - offy, al_map_rgb(200, 20, 20), 3);

      for(auto&& t : trace) {
	al_draw_circle(t.x - offx, t.y - offy, t.r * PX_PER_LIGHTYEAR, al_map_rgb(100, 100, 255), 2);
	al_draw_filled_circle(t.x - offx, t.y - offy, 5, al_map_rgb(100, 100, 255));
      }

      ImGui::SetNextWindowPos(ImVec2(x - offx - 20, y - offy - 20));
      ImGui::SetNextWindowSize(ImVec2(40, 40));
      ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.01);
      ImGui::Begin(name, NULL,
		   ImGuiWindowFlags_NoTitleBar |
		   ImGuiWindowFlags_NoResize |
		   ImGuiWindowFlags_NoMove |
		   ImGuiWindowFlags_NoScrollbar |
		   ImGuiWindowFlags_NoBringToFrontOnFocus);
      ImGui::InvisibleButton(name, ImVec2(40, 40));
      
      bool hovered = ImGui::IsItemHovered();
      ImGui::End();
      ImGui::PopStyleVar();

      if(hovered == true) {
	ImGui::BeginTooltip();
	ImGui::Text("%s", name);
	ImGui::Separator();
	ImGui::Text("Source: %s", source->name);
	ImGui::Text("Destination: %s", destination->name);
	ImGui::Text("Mass: 50kt");
	ImGui::Text("Speed: %.2fc", velocity);
	ImGui::EndTooltip();
      }
    }

    al_draw_filled_circle(x - offx, y - offy, 10, al_map_rgb(200, 20, 20));
  }

  void move_to(Star &d, Observations &obs);
  void update();
};

static inline const char *get_fleet_name(std::shared_ptr<Fleet> f) {
  return f->name;
}

struct Fleets {
  std::vector<std::shared_ptr<Fleet>> fleets;

  void add(Fleet&& f) {
    fleets.push_back(std::make_shared<Fleet>(f));
  }

  void draw(float offx, float offy) {
    for(auto&& fleet : fleets) {
      fleet->draw(offx, offy);
    }
  }

  void observe(Observations &obs);
  void update(Observations &obs);
};

struct Observer {
  const char *name;
  Star *home;
  std::vector<std::shared_ptr<Fleet>> known_travelling_fleets;
  std::vector<std::shared_ptr<Fleet>> known_idle_fleets;

  Observer(const char *_name, Star &h) {
    name = _name;
    home = &h;
  }
};

struct Observations {
  std::vector<ObservableEvent> events;
  std::vector<Observer> observers;

  int event_counter;

  void addFleetDeparture(std::shared_ptr<Fleet> f) {
    auto ev = ObservableEvent(ObservableEventType::FleetDeparture, f->x, f->y);
    printf("Fleet departure: %s, %s to %s\n", f->name, f->source->name, f->destination->name);
    ev.fleet1 = f;
    events.push_back(ev);
    event_counter++;
  }

  void addFleetArrival(std::shared_ptr<Fleet> f) {
    auto ev = ObservableEvent(ObservableEventType::FleetArrival, f->x, f->y);
    printf("Fleet arrival: %s, %s from %s\n", f->name, f->destination->name, f->source->name);
    ev.fleet1 = f;
    events.push_back(ev);
    event_counter++;
  }

  void addOrderFleetMove(std::shared_ptr<Fleet> f, Star *from, Star *to) {
    float x = observers.front().home->x;
    float y = observers.front().home->y;
    auto ev = ObservableEvent(ObservableEventType::OrderFleetMove, x, y);
    ev.fleet1 = f;
    ev.orderTarget = from;
    ev.orderMoveTo = to;
    events.push_back(ev);
    event_counter++;
  }

  void add(Observer&& o) {
    observers.emplace_back(o);
  }

  void removeEventIter(std::vector<ObservableEvent>::iterator& it) {
    printf("Removing event: %p\n", &*it);
    it = events.erase(it);
  }

  void update(Fleets &fleets) {
    std::vector<ObservableEvent>::iterator event;

    // for(auto&& observer : observers) {
    //   observer.known_idle_fleets.clear();
    // }

    for(event = events.begin(); event != events.end();) {
      printf("%ld processing event %p\n", events.size(), &*event);
      event->t += 1;

      /*
	Events with specific targets
       */
      switch(event->type)
	{
	case ObservableEventType::OrderFleetMove:
	  {
	    float distance_squared =
	      (event->x - event->orderTarget->x) * (event->x - event->orderTarget->x) +
	      (event->y - event->orderTarget->y) * (event->y - event->orderTarget->y);

	    float wave_distance_squared =
	      event->t * event->t * PX_PER_LIGHTYEAR * PX_PER_LIGHTYEAR;

	    bool reached = distance_squared <= wave_distance_squared;

	    std::shared_ptr<Fleet> f = event->fleet1;
	    Star *orderMoveTo = event->orderMoveTo;

	    /*
	      TODO unclear how this works. I arrived at this order through
	      trial and error from crashes
	     */
	    if(reached == true) {
	      removeEventIter(event);
	    }

	    if(reached == true) {
	      printf("%s received order to move to %s\n", f->name, orderMoveTo->name);
	      f->move_to(*orderMoveTo, *this);
	      addFleetDeparture(f);
	    }

	    if(reached == false) {
	      event++;
	    }
	    
	    continue;
	  };
	  break;

	default:
	  {
	  };
	  break;
	}

      bool all_reached = true;

      /*
	Events that go out for everyone
       */
      for(auto&& observer : observers) {
	
	float distance_squared =
 	  (event->x - observer.home->x) * (event->x - observer.home->x) +
	  (event->y - observer.home->y) * (event->y - observer.home->y);
	float wave_distance_squared =
	  event->t * event->t * PX_PER_LIGHTYEAR * PX_PER_LIGHTYEAR;

	bool reached = distance_squared <= wave_distance_squared;

	if(reached == true) {
	  switch(event->type)
	    {
	    case ObservableEventType::FleetDeparture:
	      {
		std::shared_ptr<Fleet> f = event->fleet1;
		if(std::find(observer.known_travelling_fleets.begin(),
			     observer.known_travelling_fleets.end(),
			     event->fleet1) == observer.known_travelling_fleets.end())
		  {
		    observer.known_travelling_fleets.push_back(f);
		    auto it =
		      std::find(observer.known_idle_fleets.begin(),
				observer.known_idle_fleets.end(),
				event->fleet1);
		    bool found = it != observer.known_idle_fleets.end();

		    if(found == true) {
		      observer.known_idle_fleets.erase(it);
		    }
		    printf("Observer %s saw fleet \"%s\" depart\n", observer.name, event->fleet1->name);
		  }

	      };
	      break;

	    case ObservableEventType::FleetArrival:
	      {
		if(std::find(observer.known_idle_fleets.begin(),
			     observer.known_idle_fleets.end(),
			     event->fleet1) == observer.known_idle_fleets.end())
		  {
		    observer.known_idle_fleets.push_back(event->fleet1);
		    auto it =
		      std::find(observer.known_travelling_fleets.begin(),
				observer.known_travelling_fleets.end(),
				event->fleet1);
		    bool found = it != observer.known_travelling_fleets.end();

		    if(found == true) {
		      observer.known_travelling_fleets.erase(it);
		    }
		
		    printf("Observer %s saw fleet \"%s\" arrive\n", observer.name, event->fleet1->name);
		  }
	      };
	      break;

	    default:
	      {
		assert(false);
	      };
	      break;
	    }
	}

	all_reached = all_reached && reached;
      }

      /*
	TODO this has to be per observer
       */
      // switch(event->type)
      // 	{
      // 	case ObservableEventType::FleetDeparture:
      // 	  {
      // 	    event->fleet1->source->removeFleet(event->fleet1);
      // 	  };
      // 	  break;
      // 	case ObservableEventType::FleetArrival:
      // 	  {
      // 	    event->fleet1->destination->addFleet(event->fleet1);
      // 	  };
      // 	  break;
      // 	default:
      // 	  {
      // 	  };
      // 	  break;
      // 	}

      if(all_reached == true) {
	removeEventIter(event);
      }
      else {
	event++;
      }
    }
  }

  void draw(float offx, float offy) {
    for(auto&& event : events) {
      al_draw_filled_circle(event.x - offx, event.y - offy, 5, al_map_rgb(100, 100, 255));
      al_draw_circle(event.x - offx, event.y - offy, event.t * PX_PER_LIGHTYEAR, al_map_rgb(100, 100, 255), 2);
    }
    Observer& me = observers.front();

    for(auto fleet : me.known_travelling_fleets) {
      fleet->draw(offx, offy);
    }
    for(auto fleet : me.known_idle_fleets) {
      fleet->draw(offx, offy);
    }
    
    // for(auto&& event : seeing) {
    //   al_draw_filled_circle(event.x - offx, event.y - offy, 10, al_map_rgb(100, 100, 255));
    // }
  }
};

void Fleets::update(Observations &obs) {
  for(auto&& fleet : fleets) {
    fleet->update();

    if(fleet->moving == false and fleet->t != 0) {
      obs.addFleetArrival(fleet);
      fleet->t = 0;
    }
  }
}

void Fleets::observe(Observations &obs) {
  // for(auto&& fleet : fleets) {
  //   // if(fleet->destination == NULL) {
  //   //   obs.addFleetIdle(fleet);
  //   // }
  // }
}

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

  void draw(Engine &e, float vx, float vy, Observer *o) {
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
      star.draw(vx, vy, o);
    }
    ImGui::PopStyleColor();
    graph.draw(vx, vy);
  }
};

struct Game {
  ALLEGRO_KEYBOARD_STATE keyboard;
  ALLEGRO_BITMAP *bg;

  int t;
  const float scroll_speed = 3;
  bool fleet_window;
  bool show_event_circles = true;

  // TODO should be in Engine?
  float vx, vy;

  Engine* e;

  Stars stars;
  Observations obs;
  Fleets fleets;

  Game(Engine& _e, float _vx, float _vy) {
    vx = _vx;
    vy = _vy;
    e = &_e;
    t = 3200;
    fleet_window = false;
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

  void init() {
    bg = al_load_bitmap("./bg.png");
    assert(bg);

    stars.init();

    Star& sol = stars.stars[0];
    // Star& procyon = stars.stars[1];
    Star& epsiloneridani = stars.stars[2];
    // Star& tauceti = stars.stars[3];
    Star& lalande = stars.stars[4];

    obs.add(Observer("President Dv", sol));
    obs.add(Observer("Bill Gates", epsiloneridani));

    fleets.add(Fleet("Epsilon Eridani Fleet", epsiloneridani));
    std::shared_ptr<Fleet> fleet1 = fleets.fleets[0];

    // fleet1->source->addFleet(fleet1);
    // obs.addFleetDeparture(fleet1);
    // fleet1->move_to(tauceti, obs);

    fleets.add(Fleet("Lalande Fleet", lalande));
    std::shared_ptr<Fleet> fleet2 = fleets.fleets[1];

    fleet2->velocity = 0.65;
    // fleet2->source->addFleet(fleet2);
    // fleet2->move_to(tauceti, obs);
    // obs.addFleetDeparture(fleet2);
  }

  void stuff() {
    // move fleet if user has a fleet selected and clicked on a star
    if(auto f = g_selected_fleet.lock()) {
      if(g_selected_star != NULL) {

	if(g_selected_star != f->source) {
	  obs.addOrderFleetMove(f, f->source, g_selected_star);
	}

	g_selected_star = NULL;
	g_selected_fleet.reset();
      }
    }
  }

  void tick() {
    t++;
    obs.event_counter = 0;
    fleets.update(obs);
    fleets.observe(obs);
    obs.update(fleets);
  }

  void draw() {
    if(e->draw_background) {
      al_draw_scaled_bitmap(bg, 0, 0, 720, 480, 0, 0, e->sx, e->sy, 0);
    }
    else {
      e->clear();
    }
    stars.draw(*e, vx, vy, &obs.observers.front());

    extern ImFont *bigger;
    ImGui::PushFont(bigger);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.2,0.2,0.2,0.9));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0);
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::Begin("menu", NULL, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove );
    ImGui::Button("Menu");
    ImGui::SameLine();
    ImGui::Button("Research");
    ImGui::SameLine();
    if(ImGui::Button("Fleet")) { fleet_window ^= 1; }
    ImGui::SameLine();
    ImGui::Button("Diplomacy");
    ImGui::SameLine();
    if(ImGui::Button("Debug")) { e->debug_win ^= 1; }
    int y = ImGui::GetWindowHeight();
    ImGui::SameLine();
    ImGui::End();

    ImGui::SetNextWindowPos(ImVec2(0, 5 + y));
    ImGui::Begin("timekeeper", NULL, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove );
    ImGui::Text("Year: %d ", t);
    int y2 = ImGui::GetWindowHeight();
    int x2 = ImGui::GetWindowWidth();
    ImGui::End();

    if(auto f = g_selected_fleet.lock()) {
      ImGui::SetNextWindowPos(ImVec2(0, 2 * 5 + y + y2));
      ImGui::Begin("selected fleet", NULL, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove );
      ImGui::Text("Commanding %s", f->name);
      ImGui::End();
    }

    ImGui::PopFont();

    if(e->paused == true) {
      ImGui::SetNextWindowPos(ImVec2(5 + x2, 5 + y));
      ImGui::Begin("paused", NULL, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove );
      ImGui::Text("Paused");
      ImGui::End();
    }

    if(fleet_window == true) {
      ImGui::Begin("Fleets");

      static int filter = 0;

      static bool name_col = true;
      static bool status_col = true;
      static bool source_col = true;
      static bool destination_col = true;
      static bool speed_col = true;
      static bool mass_col = true;

      ImGui::Spacing();
      ImGui::Text("Filter: "); ImGui::SameLine();
      ImGui::RadioButton("All", &filter, 0); ImGui::SameLine();
      ImGui::RadioButton("Mine", &filter, 1); ImGui::SameLine();
      ImGui::RadioButton("Enemy", &filter, 2);

      ImGui::Spacing();
      ImGui::Text("Columns: "); ImGui::SameLine();
      ImGui::Checkbox("Name", &name_col); ImGui::SameLine();
      ImGui::Checkbox("Status", &status_col); ImGui::SameLine();
      ImGui::Checkbox("Source", &source_col); ImGui::SameLine();
      ImGui::Checkbox("Destination", &destination_col); ImGui::SameLine();
      ImGui::Checkbox("Speed", &speed_col); ImGui::SameLine();
      ImGui::Checkbox("Mass", &mass_col);
      ImGui::Spacing();

      int n = 0;
      if(name_col) n++;
      if(status_col) n++;
      if(source_col) n++;
      if(destination_col) n++;
      if(speed_col) n++;
      if(mass_col) n++;

      ImGui::Columns(n);
      if(name_col) { ImGui::Text("Name"); ImGui::NextColumn(); }
      if(status_col) { ImGui::Text("Status"); ImGui::NextColumn(); }
      if(source_col) { ImGui::Text("Source"); ImGui::NextColumn(); }
      if(destination_col) { ImGui::Text("Destination"); ImGui::NextColumn(); }
      if(speed_col) { ImGui::Text("Speed"); ImGui::NextColumn(); }
      if(mass_col) { ImGui::Text("Mass"); ImGui::NextColumn(); }
      ImGui::Separator();
      for(auto&& fleet : obs.observers.front().known_idle_fleets) {
	if(name_col) { ImGui::Text("%s", fleet->name); ImGui::NextColumn(); }
	if(status_col) { ImGui::Text("idle"); ImGui::NextColumn(); }
	if(source_col) { ImGui::Text("%s", fleet->source->name); ImGui::NextColumn(); }
	if(destination_col) { ImGui::NextColumn(); }
	if(speed_col) { ImGui::NextColumn(); }
	if(mass_col) { ImGui::Text("50kt"); ImGui::NextColumn(); }
      }
      for(auto&& fleet : obs.observers.front().known_travelling_fleets) {
	if(name_col) { ImGui::Text("%s", fleet->name); ImGui::NextColumn(); }
	if(status_col) { ImGui::Text("%s", fleet->destination == NULL ? "idle?" : "moving"); ImGui::NextColumn(); }
	if(source_col) { ImGui::Text("%s", fleet->source->name); ImGui::NextColumn(); }
	if(destination_col) { if(fleet->destination != NULL) { ImGui::Text("%s", fleet->destination->name); } ImGui::NextColumn(); }
	if(speed_col) { if(fleet->destination != NULL) { ImGui::Text("%.2fc", fleet->velocity); } ImGui::NextColumn(); }
	if(mass_col) { ImGui::Text("50kt"); ImGui::NextColumn(); }
      }
      ImGui::End();
    }

    ImGui::PopStyleColor();
    ImGui::PopStyleVar();

    fleets.draw(vx, vy);
    if(show_event_circles == true) {
      obs.draw(vx, vy);
    }
  }
};

void add_fleet_buttons_for_obs(Star *s, Observer *o) {
  for(auto&& fleet : o->known_idle_fleets) {
    if(fleet->source == s) {
      if(ImGui::Button(get_fleet_name(fleet))) {
	g_selected_fleet = fleet;
	g_selected_star = NULL;
      }
    }
  }
}

void Fleet::move_to(Star &d, Observations &obs) {
  destination = &d;
  distance =
    sqrt((source->x - destination->x) * (source->x - destination->x) +
	 (source->y - destination->y) * (source->y - destination->y));

  moving = true;
  t = 0;
}

void Fleet::update() {
  if(moving == false) {
    // docked in star system
    return;
  }

  // travelling
  t += (velocity * PX_PER_LIGHTYEAR) / distance;
  time_since_departure += 1;

  if(t >= 1) {
    // we've arrived
    source = destination;
    x = source->x;
    y = source->y;
    trace.clear();
    moving = false;
  }
  else {
    x = lerp(source->x, destination->x, t);
    y = lerp(source->y, destination->y, t);

    for(auto&& t : trace) {
      t.r += 1;
    }

    trace.emplace_back(FleetTrace(x, y, 0));
  }
}

static void show_debug_window(Engine &e, Game &g) {
  if(e.debug_win == true) {
    ImGui::Begin("Debug", &e.debug_win);
    ImGui::Text("Viewport x: %0.f", g.vx);
    ImGui::Text("Viewport y: %0.f", g.vy);
    ImGui::Text("Stars: %ld", g.stars.stars.size());
    ImGui::Text("Fleets: %ld", g.fleets.fleets.size());
    ImGui::Text("Observers: %ld", g.obs.observers.size());

    // if(ImGui::TreeNode("Colored")) {
    //   ImGui::Text("Hello");
    //   ImGui::TreePop();
    // }
    
    int i = 0;

    for(auto&& o : g.obs.observers) {
      ImGui::Separator();
      ImGui::BulletText("Observer %d: %s", i, o.name);
      ImGui::Text("Residence: %s", o.home->name);
      ImGui::Text("Known travelling fleets: %ld", o.known_travelling_fleets.size());
      ImGui::Text("Known idle fleets: %ld", o.known_idle_fleets.size());
      i++;
    }
    ImGui::Separator();

    ImGui::Text("Travelling Events: %ld", g.obs.events.size());
    ImGui::Text("Created Events: %d", g.obs.event_counter);
    ImGui::Checkbox("Show Event Circles", &g.show_event_circles);
    ImGui::Checkbox("Draw background", &e.draw_background);
    ImGui::End();
  }
}

    // al_draw_filled_circle(720/2, 480/2, 20, al_map_rgb(0,0,0));
    // al_draw_arc(720/2, 480/2, 20, 0, a, al_map_rgb(255,255,255), 5);
    // a += 0.1;
    // if(a >= 2 * M_PI) a = 0;
    
int main()
{
  Engine e("2.7 Kelvin", 720, 480);
  e.init();
  Game g(e, -140, -70);
  g.init();

  while (e.running) {
    // TODO imgui clamps fps at 60?
    e.begin_frame();
    g.handle_panning();
    g.draw();

    show_debug_window(e, g);
    e.end_frame();
    g.stuff();

    if(e.paused == false) {
      e.frame++;
      if(e.frame % (60 / TICKS_PER_SECOND) == 0) {
	g.tick();
	printf("tick\n");
      }
    }
  }
  e.stop();
}
