CXXFLAGS = -std=c++17 -Wall -Wextra -Werror -O1 -g

test: test_core test_frigate test_light
	./test_core
	./test_frigate
	./test_light

test_core: test_core.cpp ../src/bench_core.h
	$(CXX) $(CXXFLAGS) -o $@ test_core.cpp

test_frigate: test_frigate.cpp ../src/frigate.h
	$(CXX) $(CXXFLAGS) -o $@ test_frigate.cpp

test_light: test_light.cpp ../src/screen_light.h
	$(CXX) $(CXXFLAGS) -o $@ test_light.cpp

clean:
	rm -f test_core test_frigate test_light

.PHONY: test clean
