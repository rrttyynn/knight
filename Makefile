CXX = g++
CXXFLAGS = -O3 -std=c++20 -march=native -DNDEBUG -fno-rtti

all:
	$(CXX) $(CXXFLAGS) src/main.cpp -o k8e
	strip --strip-all k8e

clean:
	rm -f k8e
