all:
	clang -O0 -std=c++11 -ggdb examples/main.cpp imgui.cpp imgui_demo.cpp imgui_draw.cpp -lm -lstdc++ termbox/build/src/libtermbox.a -oexample
