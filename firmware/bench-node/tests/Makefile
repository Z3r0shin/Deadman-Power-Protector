CXXFLAGS = -std=c++17 -Wall -Wextra -Werror -O1

test: test_ui
	./test_ui

test_ui: test_ui.cpp ../bench_ui.h
	$(CXX) $(CXXFLAGS) -o $@ test_ui.cpp

clean:
	rm -f test_ui

.PHONY: test clean
