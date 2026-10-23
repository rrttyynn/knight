CXX = g++
CXXFLAGS = -O3 -std=c++20 -march=native -DNDEBUG -fno-rtti -pthread

all: k8e k8e_release

k8e: src/*.cpp
	$(CXX) $(CXXFLAGS) src/main.cpp -o $@
	strip --strip-all $@

k8e_release: src/*.cpp pack.py
	python3 pack.py

release: k8e_release

test: k8e k8e_release
	mkdir -p build
	$(CXX) -O1 -g -march=native -std=c++20 -fsanitize=address,undefined -fno-omit-frame-pointer -pthread tests/core.cpp -o build/test_core
	ASAN_OPTIONS=detect_leaks=0 ./build/test_core
	$(CXX) -O2 -march=native -U__BMI2__ -std=c++20 -pthread tests/core.cpp -o build/test_fallback
	./build/test_fallback
	$(CXX) $(CXXFLAGS) -U__BMI2__ src/main.cpp -o build/k8e_generic
	python3 tests/regression.py ./build/k8e_generic ./k8e
	python3 tests/regression.py ./k8e
	python3 tests/regression.py ./k8e_release ./k8e

clean:
	rm -rf k8e k8e_release build

.PHONY: all release test clean
