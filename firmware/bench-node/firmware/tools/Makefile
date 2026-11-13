CXXFLAGS = -std=c++17 -Wall -Wextra -O1

render: render.cpp ../src/gfx.cpp ../src/screens.cpp ../src/pig.cpp ../src/pig_sprites.inc ../src/pig_scenes.inc ../src/gfx.h ../src/screens.h ../src/font5x7.inc
	$(CXX) $(CXXFLAGS) -o $@ render.cpp ../src/gfx.cpp ../src/screens.cpp ../src/pig.cpp
	mkdir -p out && ./render

clean:
	rm -rf render out

.PHONY: clean
