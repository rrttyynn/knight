CXX = g++
CXXFLAGS = -O3 -std=c++20 -march=native -DNDEBUG -fno-rtti -pthread

all: k8e k8e_release

k8e: src/*.cpp
	$(CXX) $(CXXFLAGS) src/main.cpp -o $@
	strip --strip-all $@

k8e_release: src/*.cpp compressor.sh pack.py
	sh compressor.sh

release: k8e_release

clean:
	rm -rf k8e k8e_release build

.PHONY: all release clean
