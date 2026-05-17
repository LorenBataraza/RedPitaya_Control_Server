CXX      ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -O2 -pthread
LDFLAGS  ?= -pthread

BINS = control_server hardware_client admin_client

all: $(BINS)

control_server: control_server.cpp api.cpp include/ShmQueue.h
	$(CXX) $(CXXFLAGS) -o $@ control_server.cpp $(LDFLAGS)

hardware_client: hardware_client.cpp api.cpp
	$(CXX) $(CXXFLAGS) -o $@ hardware_client.cpp $(LDFLAGS)

admin_client: admin_client.cpp api.cpp
	$(CXX) $(CXXFLAGS) -o $@ admin_client.cpp $(LDFLAGS)

clean:
	rm -f $(BINS)

.PHONY: all clean
