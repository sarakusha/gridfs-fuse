CXXFLAGS += -g -D_FILE_OFFSET_BITS=64 -I.

MACHINE = $(shell uname -s)

ifeq ($(MACHINE),Darwin)
	LDFLAGS +=-L. -lmongoclient -lfuse_ino64 -lboost_thread-mt -lboost_filesystem-mt -lboost_system-mt
else
	LDFLAGS +=-L. -lmongoclient -lfuse -lboost_thread -lboost_filesystem -lboost_system
endif

OBJS = $(patsubst %.cpp,%.o,$(wildcard *.cpp))

mount_gridfs : $(OBJS)
	$(CXX) $^ $(LDFLAGS) -o $@

debian : mount_gridfs

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

operations.o : operations.cpp operations.h

local_gridfile.o : local_gridfile.cpp local_gridfile.h

clean:
	rm -f $(OBJS)
