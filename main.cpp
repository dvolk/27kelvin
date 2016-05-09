#include "./engine.h"

#include <stdio.h>
#include <vector>
#include <deque>
#include <algorithm>
#include <memory>
#include <mutex>
#include <sstream>

const float PX_PER_LIGHTYEAR = 50;
const int TICKS_PER_SECOND = 2;

struct Observer;
struct Fleet;
struct Star;

bool g_draw_influence_circles = true;
bool g_draw_fleet_traces = true;
bool g_star_moving = true;
bool g_star_connecting = false;

std::weak_ptr<Star> g_selected_star1;
std::weak_ptr<Star> g_selected_star2;
std::weak_ptr<Fleet> g_selected_fleet;

static inline const char *get_fleet_name(const Fleet& f);
void add_fleet_buttons_for_obs(const Star& s, const Observer& o);
float distance_to_star(const Observer& o, const Star& s);
std::shared_ptr<Star> star_from_name(const char *name);
const char *get_observer_name(const Observer& o);

ALLEGRO_COLOR c_steelblue;
ALLEGRO_COLOR c_stars_bg;

static inline float lerp(float v0, float v1, float t) {
  return (1 - t) * v0 + t * v1;
}

struct Star {
  int id;
  int index; // index into the stars vector
  char *name; // freed by struct Stars
  // star position
  float x, y;
  // imgui window offset for centering
  float wx = 0;
  float wy = 0;
  int focus = 0;
  std::weak_ptr<Observer> owner;
  std::vector<std::weak_ptr<Star>> neighbors;
  bool moving = false;

  // only used by struct Stars
  Star(const char *_name, float _x, float _y, int _id) {
    name = strdup(_name); x = _x; y = _y; id = _id;
  }

  void update() {
  }

  void set_full_owner(std::shared_ptr<Observer>& o) {
    owner = o;
  }

  void draw(float offx, float offy, const Observer& viewer) {
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize;
    if(moving == false) {
      flags = flags | ImGuiWindowFlags_NoMove;
      ImGui::SetNextWindowPos(ImVec2(x - offx - wx/2, y - offy - wy/2));
    }

    ImGui::Begin(name, NULL, flags);
    bool pressed = ImGui::Button(name);
    if(pressed == true) {
      if(not g_selected_fleet.lock()) {
	ImGui::OpenPopup("star menu");
      }
      else {
	g_selected_star1 = star_from_name(this->name);
      }
    }

    if(ImGui::BeginPopup("star menu")) {
      if(g_star_moving == true) {

	if(ImGui::Button("Connect")) {
	  if(g_selected_star1.lock()) {
	    g_selected_star2 = star_from_name(this->name);
	  }
	  else {
	    g_selected_star1 = star_from_name(this->name);
	  }
	}

	if(ImGui::Button("moving")) {
	  moving = true;
	}

	if(moving == true) {
	  ImGui::SameLine();
	  if(ImGui::Button("Commit")) {
	    moving = false;
	    ImVec2 pos = ImGui::GetWindowPos();
	    x = pos.x + offx;
	    y = pos.y + offy;
	    printf("%s moved to %f, %f\n", name, x, y);
	  }
	}
      }

      ImGui::PushItemWidth(300);
      ImGui::Columns(2);
      ImGui::Text("%s             ", name);
      ImGui::Button("System Info");
      ImGui::NextColumn();
      ImGui::Text("Fleets:        ");
      add_fleet_buttons_for_obs(*this, viewer);
      ImGui::PopItemWidth();
      ImGui::EndPopup();
    }

    if(ImGui::IsItemHovered()) {
      ImGui::BeginTooltip();
      ImGui::Text("%s", name);
      float distance = distance_to_star(viewer, *this);
      if(distance > 0.1) {
	ImGui::Separator();
	ImGui::Text("Distance: %.1fly", distance);
      }
      if(auto o = owner.lock()) {
	if(distance < 0.1) {
	  ImGui::Separator();
	}
	ImGui::Text("Owner: %s", get_observer_name(*o));
      }
      ImGui::EndTooltip();
    }

    // Is there a way to do this without the first frame being borked?
    wy = ImGui::GetWindowHeight();
    wx = ImGui::GetWindowWidth();
    ImGui::End();
  }
};

struct Stars;

struct StarGraph {
  Stars* s;

  StarGraph() = default;
  StarGraph(Stars* _s) {
    s = _s;
  }

  std::vector<std::weak_ptr<Star>> shown_path;

  void add(const std::shared_ptr<Star>& s1, const std::shared_ptr<Star>& s2);
  void draw(float offx, float offy);
  std::vector<std::weak_ptr<Star>> pathfind(const std::shared_ptr<Star>& from, const std::shared_ptr<Star>& to) const;
};

/*
 * Other events?
 *
 *   observer requests general status report?
 *
 */

enum class ObservableEventType { FleetDeparture, FleetArrival, FleetIdle, OrderFleetMove, CombatReport };

struct Fleet;

struct ObservableEvent {
  ObservableEvent(ObservableEventType _type, float _x, float _y, int _id) {
    id = _id;
    type = _type;
    x = _x;
    y = _y;
    t = 0;
  }

  int id;
  ObservableEventType type;
  float x, y;
  float t;

  // TODO should be weak_ptrs?
  std::shared_ptr<Observer> orderSender;
  std::shared_ptr<Star> orderTarget;
  std::shared_ptr<Star> orderMoveTo;
  std::shared_ptr<Fleet> fleet1;
};

struct Observations;
struct Observer;

struct FleetTrace {
  FleetTrace(float _x, float _y, float _r) { x = _x; y = _y; r = _r; }
  float x, y, r;
};

struct Fleet {
  int id;
  float x, y;
  float t; // -1 if in star system
  float velocity;
  float distance;
  bool moving;

  std::shared_ptr<Star> source;
  std::shared_ptr<Star> destination;
  std::weak_ptr<Observer> owner;

  std::vector<FleetTrace> trace;
  std::vector<std::weak_ptr<Star>> path; // the path we're on

  const char *name;

  Fleet(const Fleet * const other) {
    id = other->id;
    x = other->x;
    y = other->y;
    t = other->t;
    velocity = other->velocity;
    distance = other->distance;
    moving = other->moving;
    source = other->source;
    destination = other->destination;
    trace = other->trace;
    name = other->name;
    owner = other->owner;
    path = other->path;
  }

  Fleet(const char *_name, std::shared_ptr<Star> s, std::weak_ptr<Observer> _owner) {
    name = _name;
    source = s;
    x = source->x;
    y = source->y;
    destination = source;
    t = -1;
    moving = false;
    owner = _owner;
    velocity = 0.75;
  }

  void draw(float offx, float offy) {
    if(moving == false) {
      return;
    }

    al_draw_line(source->x - offx, source->y - offy, destination->x - offx, destination->y - offy, al_map_rgb(200, 20, 20), 3);
    al_draw_filled_circle(x - offx, y - offy, 10, al_map_rgb(200, 20, 20));

    for(auto&& t : trace) {
      al_draw_circle(t.x - offx, t.y - offy, t.r * PX_PER_LIGHTYEAR, c_steelblue, 2);
      al_draw_filled_circle(t.x - offx, t.y - offy, 5, c_steelblue);
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

  void move_to(const StarGraph &g, std::shared_ptr<Star>& d);
  void update();
};

static inline const char *get_fleet_name(const Fleet& f) {
  return f.name;
}

struct Game;

struct Fleets {
  int max_id = 0;
  std::vector<std::shared_ptr<Fleet>> fleets;
  std::mutex add_locker;

  Fleets() {
    fleets.reserve(128);
  }

  void add(Fleet&& f) {
    add_locker.lock();
    f.id = max_id;
    fleets.emplace_back(std::make_shared<Fleet>(f));
    printf("new fleet with id: %d\n", max_id);
    max_id++;
    add_locker.unlock();
  }

  void update(Observations& obs, Game& g);
};

struct Observer {
  int id;
  const char *name;
  std::shared_ptr<Star> home;
  ALLEGRO_COLOR color;

  // These are *copies*
  std::vector<std::shared_ptr<Fleet>> known_travelling_fleets;
  std::vector<std::shared_ptr<Fleet>> known_idle_fleets;
  std::vector<std::shared_ptr<Star>> known_stars;

  std::vector<ObservableEvent> seen_events;

  Observer() {
    known_travelling_fleets.reserve(64);
    known_idle_fleets.reserve(64);
    known_stars.reserve(64);
    seen_events.reserve(128);
  }

  void add_stars(const std::vector<std::shared_ptr<Star>>& stars) {
    for(auto&& star : stars) {
      known_stars.emplace_back(std::make_shared<Star>(*star));
    }
  }

  void add_event(const ObservableEvent& e) {
    seen_events.emplace_back(e);
  }

  bool has_seen(const ObservableEvent& e) {
    for(auto&& seen_event : seen_events) {
      if(seen_event.id == e.id) {
	return true;
      }
    }
    return false;
  }

  void remove_event(ObservableEvent& e) {
    auto it = seen_events.begin();
    while(it != seen_events.end()) {
      if(it->id == e.id) {
	seen_events.erase(it);
	return;
      }
      it++;
    }
  }

  Observer(const char *_name, const std::shared_ptr<Star>& h, ALLEGRO_COLOR c) {
    name = _name;
    home = h;
    color = c;
  }
};

struct MessageLog {
  std::vector<std::string> messages;
  int year;

  MessageLog() {
    messages.reserve(32);
    year = -1;
  }

  void addMessage(const char *m, bool with_year = true) {
    // keep last 32 messages
    if(messages.size() >= 32) {
      messages.erase(messages.begin());
    }

    std::stringstream ss;
    if(with_year == true) {
      ss << "Year " << year << " ";
    }
    ss << m;
    messages.emplace_back(std::move(ss.str()));
  }

  void addEventMessage(const ObservableEvent& event) {
    static char buf[128];
    switch(event.type)
      {
      case ObservableEventType::FleetDeparture:
	{
	  sprintf(buf, "%s departed from %s to %s", event.fleet1->name, event.orderTarget->name, event.orderMoveTo->name);
	};
	break;
      case ObservableEventType::FleetArrival:
	{
	  sprintf(buf, "%s arrived at %s", event.fleet1->name, event.orderMoveTo->name);
	};
	break;
      case ObservableEventType::CombatReport:
	{
	  sprintf(buf, "%s was destroyed at %s", event.fleet1->name, event.fleet1->source->name);
	};
	break;
      default:
	{
	  return;
	};
	break;
      }
    printf("Log message: %s\n", buf);
    addMessage(buf);
  }
};

struct Observations {
  std::vector<ObservableEvent> events;
  std::vector<ObservableEvent> order_add_queue;

  std::vector<std::shared_ptr<Observer>> observers;
  std::shared_ptr<Observer> human_controller;

  int max_observer_id = 0; // id's for Observers
  int max_event_id = 0; // id's for ObservableEvents
  int tick_events_created = 0;

  Observations() {
    events.reserve(128);
    order_add_queue.reserve(32);
    observers.reserve(8);
  }

  void update_star_knowledge(Observer& observer, std::shared_ptr<Star>& real_star) {
    printf("update_star_knowledge: %s : %s\n", observer.name, real_star->name);
    for(auto&& star : observer.known_stars) {
      if(star->id == real_star->id) {
	assert(strcmp(star->name, real_star->name) == 0);
	float wx = star->wx;
	float wy = star->wy;

	star = real_star;

	// the real star has never been drawn so these values weren't set
	star->wx = wx;
	star->wy = wy;
	return;
      }
    }
    assert(false);
  }

  void addFleetDeparture(std::shared_ptr<Fleet>& f) {
    auto ev = ObservableEvent(ObservableEventType::FleetDeparture, f->x, f->y, max_event_id);
    max_event_id++;
    printf("Fleet departure: %s, %s to %s\n", f->name, f->source->name, f->destination->name);
    ev.fleet1 = std::make_shared<Fleet>(f.get());
    ev.orderTarget = f->source;
    ev.orderMoveTo = f->destination;
    order_add_queue.emplace_back(std::move(ev));
    tick_events_created++;
  }

  void addFleetArrival(std::shared_ptr<Fleet>& f) {
    auto ev = ObservableEvent(ObservableEventType::FleetArrival, f->x, f->y, max_event_id);
    max_event_id++;
    printf("Fleet arrival: %s at %s\n", f->name, f->destination->name);
    ev.fleet1 = std::make_shared<Fleet>(f.get());
    ev.orderTarget = f->source;
    ev.orderMoveTo = f->destination;
    events.emplace_back(std::move(ev));
    tick_events_created++;
  }

  void addFleetCombat(std::shared_ptr<Fleet>& f) {
    auto ev = ObservableEvent(ObservableEventType::CombatReport, f->x, f->y, max_event_id);
    max_event_id++;
    printf("Fleet combat: %s died at %s\n", f->name, f->source->name);
    ev.fleet1 = std::make_shared<Fleet>(f.get());
    events.emplace_back(std::move(ev));
    tick_events_created++;
  }

  void addOrderFleetMove(std::shared_ptr<Fleet>& f,
			 std::shared_ptr<Star>& from,
			 std::shared_ptr<Star>& to,
			 std::shared_ptr<Observer>& o) {
    float x = human_controller->home->x;
    float y = human_controller->home->y;

    auto ev = ObservableEvent(ObservableEventType::OrderFleetMove, x, y, max_event_id);
    max_event_id++;
    ev.fleet1 = std::make_shared<Fleet>(f.get());
    ev.orderTarget = from;
    ev.orderMoveTo = to;
    ev.orderSender = o;
    events.emplace_back(std::move(ev));
    tick_events_created++;
  }

  void add(Observer&& o) {
    o.id = max_observer_id;
    max_observer_id++;
    observers.emplace_back(std::make_shared<Observer>(o));
  }

  bool orderReachedDestination(const ObservableEvent& event) {
    float distance_squared =
      (event.x - event.orderTarget->x) * (event.x - event.orderTarget->x) +
      (event.y - event.orderTarget->y) * (event.y - event.orderTarget->y);

    float wave_distance_squared =
      event.t * event.t * PX_PER_LIGHTYEAR * PX_PER_LIGHTYEAR;

    return distance_squared <= wave_distance_squared;
  }

  bool eventReachedObserver(const Observer& observer, const ObservableEvent& event) {
    float distance_squared =
      (event.x - observer.home->x) * (event.x - observer.home->x) +
      (event.y - observer.home->y) * (event.y - observer.home->y);
    float wave_distance_squared =
      event.t * event.t * PX_PER_LIGHTYEAR * PX_PER_LIGHTYEAR;

    return distance_squared <= wave_distance_squared;
  }

  std::shared_ptr<Fleet> orderTargetIsPresent(Fleets& fleets, const ObservableEvent& event) {
    std::shared_ptr<Fleet> ret;
    for(auto&& fleet : fleets.fleets) {
      if(fleet->id == event.fleet1->id) {
	if(fleet->moving == false and
	   fleet->destination->id == event.orderTarget->id and
	   fleet->source->id == event.orderTarget->id)
	  {
	    return fleet;
	  }
      }
    }
    return NULL;
  }

  bool processOrder(const StarGraph& g, Fleets& fleets, ObservableEvent& event, Observer& observer) {
    if(not orderReachedDestination(event)) {
      return false;
    }
    if(observer.has_seen(event)) {
      return true;
    }

    std::shared_ptr<Fleet> f = orderTargetIsPresent(fleets, event);

    if(f) {
      printf("%s received order to move to %s\n", f->name, event.orderMoveTo->name);
      f->move_to(g, event.orderMoveTo);
      addFleetDeparture(f);
    }
    else {
      printf("order failed\n");
    }

    observer.add_event(event);
    return true;
  }

  bool RemoveFleetInVector(std::vector<std::shared_ptr<Fleet>>& vec,
			   const std::shared_ptr<Fleet>& fleet) {
    auto it = vec.begin();
    while(it != vec.end()) {
      if((*it)->id == fleet->id) {
	vec.erase(it);
	return true;
      }
      it++;
    }
    printf("RemoveFleetInVector: false\n");
    return false;
  }

  bool FleetEventInVector(const std::vector<std::shared_ptr<Fleet>>& vec,
			  const ObservableEvent& event) {
    auto it = vec.begin();
    while(it != vec.end()) {
      if((*it)->id == event.fleet1->id) {
	return true;
      }
      it++;
    }
    return false;
  }

  bool RemoveFleetEventInVector(std::vector<std::shared_ptr<Fleet>>& vec,
				const ObservableEvent& event) {
    auto it = vec.begin();
    while(it != vec.end()) {
      if((*it)->id == event.fleet1->id) {
	vec.erase(it);
	return true;
      }
      it++;
    }
    printf("RemoveFleetEventInVector: false\n");
    return false;
  }

  bool processEvent(Observer& observer, ObservableEvent& event, MessageLog& log) {
    if(not eventReachedObserver(observer, event)) {
      return false;
    }
    if(observer.has_seen(event)) {
      return true;
    }

    switch(event.type)
      {
      case ObservableEventType::FleetArrival:
	{
	  if(not FleetEventInVector(observer.known_idle_fleets, event)) {
	    // we haven't seen this even before
	    std::shared_ptr<Fleet> fleet_copy(new Fleet(event.fleet1.get()));
	    observer.known_idle_fleets.emplace_back(std::move(fleet_copy));
	    if(observer.id == event.fleet1->owner.lock()->id) {
	      update_star_knowledge(observer, event.fleet1->destination);
	    }
	    printf("Observer %s saw fleet \"%s\" arrive\n", observer.name, event.fleet1->name);
	    if(observer.id == human_controller->id) {
	      log.addEventMessage(event);
	    }
	    RemoveFleetEventInVector(observer.known_travelling_fleets, event);
	  }
	};
	break;

      case ObservableEventType::FleetDeparture:
	{
	  if(not FleetEventInVector(observer.known_travelling_fleets, event)) {
	    // we haven't seen this even before
	    std::shared_ptr<Fleet> fleet_copy(new Fleet(event.fleet1.get()));
	    observer.known_travelling_fleets.emplace_back(std::move(fleet_copy));
	    printf("Observer %s saw fleet \"%s\" depart\n", observer.name, event.fleet1->name);
	    if(observer.id == event.fleet1->owner.lock()->id) {
	      update_star_knowledge(observer, event.orderTarget);
	    }
	    if(observer.id == human_controller->id) {
	      log.addEventMessage(event);
	    }
	    RemoveFleetEventInVector(observer.known_idle_fleets, event);
	  }
	};
	break;

      case ObservableEventType::CombatReport:
	if(FleetEventInVector(observer.known_idle_fleets, event)) {
	  // can the arrival event come after the combat report?
	  printf("Observer %s saw fleet \"%s\" destroyed at %s\n", observer.name, event.fleet1->name, event.fleet1->source->name);
	  if(observer.id == event.fleet1->owner.lock()->id) {
	    update_star_knowledge(observer, event.fleet1->source);
	  }
	  if(observer.id == human_controller->id) {
	    log.addEventMessage(event);
	  }
	  RemoveFleetEventInVector(observer.known_idle_fleets, event);
	}
	break;

      default:
	{
	};
	break;
      }

    observer.add_event(event);
    return true;
  }

  void update(const StarGraph& graph, Fleets& fleets, MessageLog& log) {
    std::vector<ObservableEvent>::iterator it = events.begin();

    while(it != events.end()) {

      ObservableEvent& event = *it;
      bool erase_event = true;
      event.t += 1;

      switch(event.type)
	{
	case ObservableEventType::OrderFleetMove:
	  {
	    // orders are erased when they reach the target star
	    erase_event = processOrder(graph, fleets, event, *human_controller);
	  };
	  break;

	default:
	  {
	    for(auto&& observer : observers) {
	      bool reached = processEvent(*observer, event, log);
	      // other events propagate until they reach all observers
	      erase_event = erase_event && reached;
	    }
	  };
	  break;
	}

      if(erase_event == true) {
	for(auto&& observer : observers) { observer->remove_event(event); }
	it = events.erase(it);
      }
      else { it++; }
    }

    for(auto&& order : order_add_queue) {
      events.emplace_back(std::move(order));
    }
    order_add_queue.clear();
  }

  void draw(float offx, float offy, bool show_event_circles) {
    if(show_event_circles == true) {
      for(auto&& event : events) {
	al_draw_filled_circle(event.x - offx, event.y - offy, 5, al_map_rgb(100, 100, 255));
	al_draw_circle(event.x - offx, event.y - offy, event.t * PX_PER_LIGHTYEAR, al_map_rgb(100, 100, 255), 2);
      }
    }

    for(auto fleet : human_controller->known_travelling_fleets) {
      fleet->draw(offx, offy);
    }
  }
};

const char *get_observer_name(const Observer& o) {
  return o.name;
}

struct Stars {
  int max_id = 0;
  const size_t max_stars = 128;
  std::vector<std::shared_ptr<Star>> stars;
  StarGraph graph;

  ALLEGRO_BITMAP *circle_buf;

  Stars() {
    stars.reserve(64);
  }

  ~Stars() {
    for(auto&& star : stars) { free(star->name); }
  }

  void update() {
    for(auto&& star : stars) { star->update(); }
  }

  void add(const char *name) {
    add(name, 0, 0);
  }

  void add(const char *name, float _x, float _y) {
    stars.emplace_back(std::make_shared<Star>(Star(name, _x, _y, max_id)));
    max_id++;
    printf("stars.size(): %ld\n", stars.size());
  }

  std::shared_ptr<Star> from_name(const char *name) {
    std::shared_ptr<Star> ret;
    for(auto&& star : stars) {
      if(strcmp(star->name, name) == 0) {
	ret = star;
	break;
      }
    }
    return ret;
  }

  void connect(const char *name1, const char *name2) {
    graph.add(from_name(name1), from_name(name2));
  }

  void rebuild_indexes() {
    int i = 0;
    for(auto&& star : stars) {
      star->index = i;
      i++;
    }
  }

  void init() {
    circle_buf = al_create_bitmap(720, 480);
    assert(circle_buf);

    stars.reserve(max_stars);

    add("Sol", 100, 100);
    add("Procyon", 250, 0);
    add("Epsilon Eridani", 400, 200);
    add("Tau Ceti", 200, 150);
    add("Lalande", 90, 250);
    add("Alpha Centauri", -60, 130);
    add("Ross 154", -130, 240);
    add("Cygni", -70, -50);
    rebuild_indexes();

    std::shared_ptr<Star> sol = from_name("Sol");
    std::shared_ptr<Star> procyon = from_name("Procyon");
    std::shared_ptr<Star> epsiloneridani = from_name("Epsilon Eridani");
    std::shared_ptr<Star> tauceti = from_name("Tau Ceti");
    std::shared_ptr<Star> lalande = from_name("Lalande");
    std::shared_ptr<Star> alphacentauri = from_name("Alpha Centauri");
    std::shared_ptr<Star> ross154 = from_name("Ross 154");
    std::shared_ptr<Star> cygni = from_name("Cygni");

    graph.s = this;
    graph.add(sol, tauceti);
    graph.add(tauceti, lalande);
    graph.add(tauceti, epsiloneridani);
    graph.add(sol, procyon);
    graph.add(procyon, tauceti);
    graph.add(sol, alphacentauri);
    graph.add(alphacentauri, ross154);
    graph.add(alphacentauri, cygni);
    graph.add(alphacentauri, lalande);
    graph.add(sol, lalande);
    graph.add(sol, cygni);

    auto path = graph.pathfind(epsiloneridani, ross154);
    for(auto&& next : path) {
      printf("-> %s\n", next.lock()->name);
    }
  }

  void draw(float vx, float vy, const Observer& o) {
    for(auto&& star : o.known_stars) {
      if(auto s = star->owner.lock()) {
	al_draw_filled_circle(star->x - vx, star->y - vy, star->wx/1.8, s->color);
      }
    }

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2, 0.2, 0.2, 1.0));
    for(auto&& star : o.known_stars) {
      star->draw(vx, vy, o);
    }
    ImGui::PopStyleColor();
    graph.draw(vx, vy);

    for(auto&& fleet : o.known_idle_fleets) {
      if(auto f = fleet->owner.lock()) {
	al_draw_filled_circle(fleet->source->x - vx, fleet->source->y - vy - 35, 10, f->color);
	al_draw_circle(fleet->source->x - vx, fleet->source->y - vy - 35, 10, al_map_rgb(255, 255, 255), 2);
      }
    }
  }
};

void StarGraph::add(const std::shared_ptr<Star>& s1, const std::shared_ptr<Star>& s2) {
  s1->neighbors.emplace_back(s2);
  s2->neighbors.emplace_back(s1);
}

void StarGraph::draw(float offx, float offy) {
  for(auto&& star : s->stars) {
    for(auto&& neighbor : star->neighbors) {
      if(auto n = neighbor.lock()) {
	al_draw_line(star->x - offx, star->y - offy, n->x - offx, n->y - offy, al_map_rgb(200,200,200), 2);
      }
    }
  }
}

std::vector<std::weak_ptr<Star>> StarGraph::pathfind(const std::shared_ptr<Star>& from, const std::shared_ptr<Star>& to) const {
  struct bfsdata {
    std::weak_ptr<Star> parent;
  };

  std::vector<bfsdata> data(s->stars.size());

  std::deque<std::weak_ptr<Star>> q;
  q.emplace_back(from);

  while(not q.empty()) {
    std::weak_ptr<Star> cur_ = q.front();
    auto cur = cur_.lock();
    if(not cur) { continue; }

    q.pop_front();

    for(auto&& neighbor_ : cur->neighbors) {
      auto neighbor = neighbor_.lock();
      if(not neighbor) { continue; }
      bool not_visited = not data[neighbor->index].parent.lock();

      if(not_visited == true) {
	data[neighbor->index].parent = cur;
	q.push_back(neighbor);
      }
    }
  }

  if(not data[to->index].parent.lock()) { return {}; } // no path

  std::vector<std::weak_ptr<Star>> ret;
  std::shared_ptr<Star> cur = to;

  while(cur->id != from->id) {
    ret.push_back(cur);
    std::weak_ptr<Star> cur_ = data[cur->index].parent;
    cur = cur_.lock();
    if(not cur) { return {}; }
  }

  ret.push_back(from);
  reverse(ret.begin(), ret.end());

  return ret;
}

void switch_to_menu();

struct Game {
  ALLEGRO_KEYBOARD_STATE keyboard;
  ALLEGRO_BITMAP *bg;

  int t;
  const float scroll_speed = 3;
  bool fleet_window;
  bool settings_window;
  bool log_window;
  int step;
  bool show_event_circles = true;

  float vx, vy;

  Engine *e;
  MessageLog log;

  Stars stars;
  Observations obs;
  Fleets fleets;

  Game() { }
  void init(Engine& _e, float _vx, float _vy) {
    vx = _vx;
    vy = _vy;
    e = &_e;
    t = 3200;
    fleet_window = false;
    settings_window = false;
    log_window = true;
    step = -1;
  }

  void handle_panning(Engine& e) {
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

    if(e.mouse_btn_down and ImGui::IsAnyItemActive() == false) {
      vx -= round(e.mouse_dx * scroll_speed);
      vy -= round(e.mouse_dy * scroll_speed);
    }
  }

  void init() {
    c_steelblue = al_map_rgb(70, 130, 180);
    c_stars_bg = al_map_rgb(100, 255, 255);

    bg = al_load_bitmap("./bg.png");
    assert(bg);

    stars.init();

    obs.add(Observer("Dv", stars.from_name("Epsilon Eridani"), al_map_rgb(143, 188, 143)));
    obs.add(Observer("Xenos", stars.from_name("Ross 154"), al_map_rgb(72, 61, 139)));
    // obs.add(Observer("Dv", stars.from_name("Epsilon Eridani"), al_map_rgb(255, 0, 0)));
    // obs.add(Observer("Xenos", stars.from_name("Ross 154"), al_map_rgb(0, 0, 255)));
    obs.human_controller = obs.observers.front();
    std::shared_ptr<Observer> xeno = obs.observers[1];

    stars.from_name("Epsilon Eridani")->set_full_owner(obs.human_controller);
    stars.from_name("Procyon")->set_full_owner(obs.human_controller);

    stars.from_name("Ross 154")->set_full_owner(xeno);
    stars.from_name("Alpha Centauri")->set_full_owner(xeno);

    obs.human_controller->add_stars(stars.stars);
    xeno->add_stars(stars.stars);

    fleets.add(Fleet("Epsilon Eridani Fleet", stars.from_name("Epsilon Eridani"), obs.human_controller));
    fleets.add(Fleet("Lalande Fleet", stars.from_name("Lalande"), obs.human_controller));
    fleets.add(Fleet("Ross 154 Fleet", stars.from_name("Ross 154"), xeno));
    fleets.add(Fleet("Alpha Centauri Fleet", stars.from_name("Alpha Centauri"), xeno));

    log.addMessage("Welcome to 2.7 Kelvin!", false);
  }

  void stuff() {
    // move fleet if user has a fleet selected and clicked on a star
    if(auto f = g_selected_fleet.lock()) {
      if(auto s = g_selected_star1.lock()) {

    	if(s != f->source) {
    	  obs.addOrderFleetMove(f, f->source, s, obs.human_controller);
    	}

    	g_selected_star1.reset();
    	g_selected_fleet.reset();
      }
    }

    std::shared_ptr<Star> s1 = g_selected_star1.lock();
    std::shared_ptr<Star> s2 = g_selected_star2.lock();

    if(s1 and s2) {
      printf("connecting %s - %s\n", s1->name, s1->name);
      stars.graph.add(s1, s2);

      g_selected_star1.reset();
      g_selected_star2.reset();
    }

    // don't draw the circles if we're not in 720x480 because that's the bitmap's size
    // TODO fix that
    if(e->sx != 720 && e->sy != 480) {
      g_draw_influence_circles = false;
    }
  }

  void handle_key(int key) {
    switch(key)
      {
      case ALLEGRO_KEY_F:{ fleet_window ^= 1; }; break;
      case ALLEGRO_KEY_L:{ log_window ^= 1; }; break;
      case ALLEGRO_KEY_P:{ step = -1; }; break;
      case ALLEGRO_KEY_SPACE:{ step = -1; }; break;
      case ALLEGRO_KEY_FULLSTOP: { step = 1; e->paused = true; }; break;
      case ALLEGRO_KEY_B: {
	if(obs.human_controller == obs.observers[0]) {
	  obs.human_controller = obs.observers[1];
	}
	else {
	  obs.human_controller = obs.observers[0];
	}
      }; break;
      default: { }; break;
      }
  }

  void tick() {
    t++;
    log.year = t;
    obs.tick_events_created = 0;
    fleets.update(obs, *this);
    obs.update(stars.graph, fleets, log);
    stars.update();
  }

  void fleetArrived(std::shared_ptr<Fleet>& arrived)
  {
    std::vector<std::shared_ptr<Fleet>>::iterator it = fleets.fleets.begin();
    while(it != fleets.fleets.end()) {
      bool encounter = (*it)->t == 0 and arrived->id != (*it)->id and (*it)->source->id == arrived->source->id;

      if(encounter == true) {
	// TODO hmm
	bool is_enemy = (*it)->owner.lock()->id != arrived->owner.lock()->id;

	if(is_enemy == true) {
	  printf("%s died at %s\n", (*it)->name, (*it)->source->name);
	  obs.addFleetCombat(*it);
	  it = fleets.fleets.erase(it);
	  continue;
	}
      }
      it++;
    }
  }

  void draw() {
    if(e->draw_background) {
      if(bg) {
	al_draw_scaled_bitmap(bg, 0, 0, 1280, 720, 0, 0, e->sx, e->sy, 0);
      }
      // float i = 50;
      // al_draw_filled_rectangle(0, 0, e->sx, e->sy, al_map_rgba((i / 255) * 100, (i / 255) * 255, (i / 255) * 255, 30));
    }
    else {
      e->clear();
    }
    stars.draw(vx, vy, *obs.human_controller);

    extern ImFont *bigger;
    ImGui::PushFont(bigger);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.2,0.2,0.2,0.9));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0);
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::Begin("menu", NULL, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove );
    if(ImGui::Button("Menu")) {
      // switch_to_menu();
      settings_window ^= 1;
    }
    ImGui::SameLine();
    ImGui::Button("Research");
    ImGui::SameLine();
    if(ImGui::Button("Fleets")) { fleet_window ^= 1; }
    ImGui::SameLine();
    ImGui::Button("Diplomacy");
    ImGui::SameLine();
    if(ImGui::Button("Messages")) { log_window ^= 1; }
    int y = ImGui::GetWindowHeight();
    ImGui::End();

    ImGui::SetNextWindowPos(ImVec2(0, 5 + y));
    ImGui::Begin("timekeeper", NULL, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove );
    ImGui::Text("%d CE", t);
    int y2 = ImGui::GetWindowHeight();
    int x2 = ImGui::GetWindowWidth();
    ImGui::End();

    ImGui::SetNextWindowPos(ImVec2(5 + x2, 5 + y));
    ImGui::Begin("resources", NULL, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove );
    ImGui::Text("10 AP");
    x2 += ImGui::GetWindowWidth();
    ImGui::End();

    if(auto f = g_selected_fleet.lock()) {
      ImGui::SetNextWindowPos(ImVec2(0, 2 * 5 + y + y2));
      ImGui::Begin("selected fleet", NULL, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove );
      ImGui::Text("Commanding %s", f->name);
      ImGui::End();
    }

    ImGui::PopFont();

    if(e->paused == true) {
      ImGui::SetNextWindowPos(ImVec2(2 * 5 + x2, 5 + y));
      ImGui::Begin("paused", NULL, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove );
      ImGui::Text("Paused");
      ImGui::End();
    }

    if(log_window == true) {
      ImGui::Begin("message log", &log_window, ImGuiWindowFlags_NoTitleBar);
      ImGui::BeginChild("scrolling", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);
      for(auto&& message : log.messages) {
	ImGui::TextUnformatted(message.c_str());
      }
      ImGui::SetScrollHere(1.0f);
      ImGui::EndChild();
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
      for(auto&& fleet : obs.human_controller->known_idle_fleets) {
	if(name_col) { ImGui::Text("%s", fleet->name); ImGui::NextColumn(); }
	if(status_col) { ImGui::Text("idle"); ImGui::NextColumn(); }
	if(source_col) { ImGui::Text("%s", fleet->source->name); ImGui::NextColumn(); }
	if(destination_col) { ImGui::NextColumn(); }
	if(speed_col) { ImGui::NextColumn(); }
	if(mass_col) { ImGui::Text("50kt"); ImGui::NextColumn(); }
      }
      for(auto&& fleet : obs.human_controller->known_travelling_fleets) {
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

    if(settings_window) {
      ImGui::Begin("Settings", &settings_window);
      ImGui::Checkbox("Show event circles", &show_event_circles);
      ImGui::Checkbox("Draw background", &e->draw_background);
      ImGui::Checkbox("Draw fleet traces", &g_draw_fleet_traces);
      ImGui::Checkbox("Draw influence circles", &g_draw_influence_circles);
      ImGui::Checkbox("Allow star movement", &g_star_moving);
      ImGui::Separator();
      static char buf[32] = "Star name";
      ImGui::InputText("Star name", buf, 32);
      if(ImGui::Button("Create")) {
	stars.add(buf);
      }
      ImGui::End();
    }

    // for(auto&& fleet : fleets.fleets) {
    //   fleet->draw(vx, vy);
    // }
    obs.draw(vx, vy, show_event_circles);
  }
};

void add_fleet_buttons_for_obs(const Star& s, const Observer& o) {
  for(auto&& fleet : o.known_idle_fleets) {
    if(fleet->source->id == s.id) {
      if(ImGui::Button(fleet->name)) {
      	g_selected_fleet = fleet;
      	g_selected_star1.reset();
      }
    }
  }
}

float distance_to_star(const Observer& o, const Star& s) {
  return
    sqrt((s.x - o.home->x) * (s.x - o.home->x) +
	 (s.y - o.home->y) * (s.y - o.home->y)) / PX_PER_LIGHTYEAR;
}

void Fleets::update(Observations& obs, Game& g) {
  std::vector<std::weak_ptr<Fleet>> arrived_fleets;

  // move fleets
  for(auto&& fleet : fleets) {
    fleet->update();

    if(fleet->moving == false and fleet->t != 0) {
      obs.addFleetArrival(fleet);
      fleet->t = 0;
      arrived_fleets.emplace_back(std::move(fleet));
    }
  }

  // process combat
  for(auto&& fleet : arrived_fleets) {
    if(auto f = fleet.lock()) {
      g.fleetArrived(f);
    }
  }

  // move surviving ships on paths
  for(auto&& fleet : fleets) {
    if(fleet->moving == false) {
      if(not fleet->path.empty()) {
	if(fleet->path.size() == 1) { // it is what it is
	  fleet->path.clear();
	  fleet->source = fleet->destination;
	  fleet->t = 0;
	  continue;
	}

	if(auto next = fleet->path.front().lock()) {
	  fleet->move_to(g.stars.graph, next);
	  obs.addFleetDeparture(fleet);
	}
      }
    }
  }

  for(auto&& event : obs.events) {
    event.fleet1->update();
  }

  for(auto&& fleet : obs.human_controller->known_travelling_fleets) {
    fleet->update();
  }
}

void Fleet::move_to(const StarGraph& g, std::shared_ptr<Star>& d) {
  // check if d is a neighbor of the fleet's star
  bool direct = false;
  for(auto&& neighbor : source->neighbors) {
    if(auto n = neighbor.lock()) {
      if(n->id == d->id) { direct = true; }
    }
  }

  if(direct == false) {
    if(path.empty()) {
      path = g.pathfind(source, d);
    }
    printf("*** path:");
    for(auto&& p : path) printf(" %s", p.lock()->name);
    puts("\n");

    if(path.empty()) {
      printf("Fail whale: Couldn't find path from %s to %s\n", source->name, d->name);
      return;
    }

    path.erase(path.begin());
    if(auto dest = path.front().lock()) {
      source = destination;
      destination = dest;
      printf("%s -> %s\n\n", source->name, destination->name);
    }
    else {
      exit(1);
    }
  }
  else {
    destination = d;
  }

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

    if(g_draw_fleet_traces == true) {
      for(auto&& t : trace) {
	t.r += 1;
      }

      trace.emplace_back(FleetTrace(x, y, 0));
    }
  }
}

static void show_debug_window(Engine& e, Game& g) {
  if(e.debug_win == true) {
    ImGui::Begin("Debug", &e.debug_win);
    // ImGui::Text("Viewport x: %0.f", g.vx);
    // ImGui::Text("Viewport y: %0.f", g.vy);
    ImGui::Text("Stars: %ld", g.stars.stars.size());
    ImGui::Text("Fleets: %ld", g.fleets.fleets.size());
    ImGui::Text("Observers: %ld", g.obs.observers.size());

    ImGui::Text("%s :: %s",
		g.fleets.fleets.front()->source->name,
		g.fleets.fleets.front()->destination->name);

    int i = 0;

    for(auto&& o : g.obs.observers) {
      ImGui::Separator();
      ImGui::BulletText("Observer %d: %s", i, o->name);
      ImGui::Text("Residence: %s", o->home->name);
      ImGui::Text("Known travelling fleets: %ld", o->known_travelling_fleets.size());
      ImGui::Text("Known idle fleets: %ld", o->known_idle_fleets.size());
      i++;
    }
    ImGui::Separator();

    ImGui::Text("Travelling Events: %ld", g.obs.events.size());
    ImGui::Text("Created Events: %d", g.obs.tick_events_created);
    ImGui::End();
  }
}

struct UI {
  virtual void draw() = 0;
  virtual void update() = 0;
};

void switch_to_game();

struct TitleUI : public UI {
  Engine *engine;

  TitleUI(Engine *_e) { engine = _e; }

  void update() override {
    Engine& e = *engine;
    e.clear();

    static float angle = 0;
    static float size = (M_PI / 2.0);
    static float skip = (M_PI / 2.0) / 3.0;
    static float r = 190;
    const float thickness = 15;

    if(ImGui::IsAnyItemHovered()) {
      angle += 0.050;
    }
    else {
      angle += 0.002;
    }

    al_draw_arc(1.5/3.0 * e.sx, e.sy / 2, r, angle + 0, size, c_steelblue, thickness);
    al_draw_arc(1.5/3.0 * e.sx, e.sy / 2, r, angle + size + skip, size, c_steelblue, thickness);
    al_draw_arc(1.5/3.0 * e.sx, e.sy / 2, r, angle + 2 * (size + skip), size, c_steelblue, thickness);
    al_draw_filled_circle(e.sx / 2.0, e.sy / 2.0, r * 8/10.0, c_steelblue);

    extern ImFont* bigger;
    ImGui::SetNextWindowPosCenter();
    // ImGui::SetNextWindowSize(ImVec2(200, 350));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0);
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(69/255.0, 77/255.0, 87/255.0, 1.0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(50/255.0, 57/255.0, 77/255.0, 1.0));
    ImGui::Begin("2.7 Kelvin", NULL, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize);
    const ImVec2 sz = ImVec2(100, 40);
    ImGui::PushFont(bigger);
    if(ImGui::Button("New", sz)) {
      switch_to_game();
    }
    ImGui::Button("Load", sz);
    ImGui::Button("Save", sz);
    ImGui::Button("Options", sz);
    ImGui::Button("Help", sz);
    if(ImGui::Button("Exit", sz)) {
      e.running = false;
    }
    ImGui::PopFont();
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
    ImGui::PopStyleVar();

    e.end_frame();
  }
  void draw() {}
};

struct GameUI : public UI {
  Game *game;

  GameUI(Game* _g) {
    game = _g;
  }

  void draw() override {
    game->draw();
  }

  void update() override {
    Game& g = *game;
    Engine& e = *g.e;

    g.handle_panning(e);
    g.handle_key(e.key);

    draw();

    show_debug_window(e, g);

    e.end_frame();

    g.stuff();

    // take step steps and then pause
    if(g.step > 0) {
      while(g.step > 0) {
	g.tick();
	g.step--;
      }
      e.paused = true;
    }

    // otherwise just go
    if(e.paused == false) {
      e.frame++;
      if(e.frame % (60 / TICKS_PER_SECOND) == 0) {
	g.tick();
      }
      g.step--;
    }
  }
};

Game g;
GameUI *gameUI = NULL;
TitleUI *titleUI = NULL;
UI *ui = NULL;

std::shared_ptr<Star> star_from_name(const char *name) {
  return g.stars.from_name(name);
}

void switch_to_game() {
  ui = gameUI;
}

void switch_to_menu() {
  ui = titleUI;
  g.e->paused = true;
}

int main()
{
  Engine e("2.7 Kelvin", 1280, 720);
  e.init();

  g.init(e, -220, -100);
  g.init();

  gameUI = new GameUI(&g);
  titleUI = new TitleUI(&e);
  ui = titleUI;

  while (e.running) {
    // TODO imgui clamps fps at 60?
    e.begin_frame();
    ui->update();
  }
  e.stop();
}
