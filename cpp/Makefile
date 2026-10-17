# https://github.com/wolfatthegate/CS33211-Assignment
# Builds the C++ producer / consumer / cleanup programs.
#
#   make          build all three
#   make run      build, then run producer and consumer side by side
#   make clean    remove the binaries

CXX      ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -pedantic -O2 -pthread
LDFLAGS  += -pthread

# glibc before 2.34 keeps shm_open() in librt; macOS has no librt at all.
ifeq ($(shell uname -s),Linux)
LDLIBS += -lrt
endif

PROGRAMS = producer consumer cleanup
HEADERS  = protocol.hpp ipc.hpp

all: $(PROGRAMS)

$(PROGRAMS): %: %.cpp $(HEADERS)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $< -o $@ $(LDLIBS)

run: all
	./producer & ./consumer; wait

clean:
	rm -f $(PROGRAMS)

.PHONY: all run clean
