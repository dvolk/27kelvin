#g++ -std=c++11 -Wall -Werror -Wno-sign-compare -I ../../../imgui main.cpp imgui_impl_a5.cpp ../../../imgui/imgui*.cpp -lallegro -lallegro_primitives

#

g++ -g3 -fsanitize=address -fsanitize=leak -fsanitize=undefined -Wall -Werror -Wno-sign-compare -std=c++14 engine.cpp main.cpp /home/dv/src/lib/imgui/imgui.o /home/dv/src/lib/imgui/imgui_draw.o imgui_impl_a5/imgui_impl_a5.o -o main -lallegro -lallegro_primitives -lallegro_image
