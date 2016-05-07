EXE_FILE=27kelvin
CC=gcc
CXX=g++
RM=rm -f
SANITIZE=-g3 -fsanitize=address -fsanitize=leak -fsanitize=undefined
CPPFLAGS=-Wall -Wextra -Wpedantic -std=c++11 $(SANITIZE)
LDFLAGS=$(CPPFLAGS)
LDLIBS=-lallegro -lallegro_primitives -lallegro_image

SRCS=engine.cpp main.cpp
OBJS=$(subst .cpp,.o,$(SRCS))

# you'll need to get imgui. see https://github.com/ocornut/imgui

# then compile imgui.cpp, imgui_draw.cpp

# and imgui_impl_a5.cpp from examples/allegro5_example

# like so:
# g++ -fPIC -c imgui.cpp -o imgui.o

# then add the object files here:
IMGUI_OBJS=imgui_impl_a5/imgui_impl_a5.o ../../lib/imgui/imgui.o ../../lib/imgui/imgui_draw.o

all: tool

tool: $(OBJS)
	$(CXX) $(LDFLAGS) $(IMGUI_OBJS) -o $(EXE_FILE) $(OBJS) $(LDLIBS) 

depend: .depend

.depend: $(SRCS)
	rm -f ./.depend
	$(CXX) $(CPPFLAGS) -MM $^>>./.depend;

clean:
	$(RM) $(OBJS)

dist-clean: clean
	$(RM) *~ .depend

include .depend

