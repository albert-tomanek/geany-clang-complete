
PLUGIN_NAME := geanyclangcomplete.so

LANG_SRCS := preferences.cpp completion_framework.cpp completion.cpp plugin_info.cpp
LANG_SRCS := $(addprefix src/, $(LANG_SRCS))

CXXFLAGS += -O2 -I/usr/lib/llvm-3.8/include/
LDFLAGS  += -lclang -L/usr/lib/llvm-3.8/lib

include geany-complete-core/Makefile.core

