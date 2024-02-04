RM = rm -fr
MKDIR = mkdir -p
CXX = g++
AS = as
CXXFLAGS := -Wall -Wextra -pedantic -Wno-literal-suffix -fPIC -fno-rtti -std=c++14 -pthread -O0 -g -flto
LDFLAGS := -pthread -g -flto
TARGETS := risconvert

.PHONY : all clean

all : $(TARGETS)

clean :
	$(RM) $(TARGETS) obj

obj :
	$(MKDIR) $@

obj/%.o : src/%.cpp | obj
	$(CXX) $(CXXFLAGS) -I./include -c $< -o $@

risconvert : obj/main.o
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -I./include $< -o $@
