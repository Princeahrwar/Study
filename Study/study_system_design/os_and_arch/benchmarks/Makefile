# Module 1 memory-subsystem labs
CXX      ?= g++
CXXFLAGS ?= -O2 -std=c++17 -march=native -pthread
BINS      = cache tlb2

all: $(BINS)

cache: cache.cpp
	$(CXX) $(CXXFLAGS) $< -o $@

tlb2: tlb2.cpp
	$(CXX) $(CXXFLAGS) $< -o $@

# Requires linux-tools and perf_event_paranoid <= 1 (Linux only; on macOS use Instruments)
perf-cache: cache
	perf stat -e cycles,instructions,L1-dcache-loads,L1-dcache-load-misses,LLC-loads,LLC-load-misses ./cache

perf-tlb: tlb2
	perf stat -e dTLB-loads,dTLB-load-misses,dtlb_load_misses.walk_active,cycles ./tlb2

clean:
	rm -f $(BINS)

.PHONY: all clean perf-cache perf-tlb
