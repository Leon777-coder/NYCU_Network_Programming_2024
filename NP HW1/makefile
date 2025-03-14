# g++ (Ubuntu 9.3.0-10ubuntu2) 9.3.0
CC=g++
TARGET=npshell

SRCS=$(shell ls *.cpp | grep -v 'npshell*')
OBJS=$(patsubst %.cpp, %, $(SRCS))

OUT_DIR=bin
OUT_OBJS=$(addprefix $(OUT_DIR)/, $(OBJS))

$(TARGET): $(OUT_OBJS) npshell.cpp
	$(CC) -o $@ npshell.cpp

$(OUT_DIR)/%: %.cpp
	$(CC) -o $@ $<

.PHONY: clean
clean:
	rm -f npshell $(OUT_OBJS)
