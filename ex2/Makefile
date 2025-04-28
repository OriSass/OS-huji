CC=g++              # C++ compiler
CXX=g++             # C++ compiler (again, redundant with CC)

CODESRC=uthreads.cpp thread.cpp context_switch_lock.cpp # Source file
OBJ=$(CODESRC:.cpp=.o)        # Object file
LIBRARY=libuthreads.a # Static library name

INCS=-I.            # Include directory (current directory)
CFLAGS=-Wall -std=c++11 -O3 $(INCS) # Compilation flags
CXXFLAGS=-Wall -std=c++11 -O3 $(INCS) # Compilation flags (same as CFLAGS)

TARGETS=$(LIBRARY)    # Library target

TAR=tar
TARFLAGS=-cvf
TARNAME=ex2.tar
TARSRCS=$(CODESRC) Makefile README

# Default target to build static library
all: $(TARGETS)

# Rule to create the static library
$(LIBRARY): $(OBJ)
	$(AR) rcs $@ $^

# Rule to compile the object file
$(OBJ): $(CODESRC)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Clean up object files and library
clean:
	$(RM) $(OBJ) $(LIBRARY)

# Generate dependencies (if needed)
depend:
	makedepend -- $(CFLAGS) -- $(SRC) $(CODESRC)

# Rule to create the tarball with source files
tar:
	$(TAR) $(TARFLAGS) $(TARNAME) $(TARSRCS)
