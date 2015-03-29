
CC = g++

TARGET := flvparse
SRC := $(wildcard *.cpp)
OBJS := $(SRC:%.cpp=%.o)

all:$(OBJS)
	$(CC) -g -o $(TARGET) $(OBJS) 
%.o:%.cpp
	$(CC) -c $< -o $@
clean:
	rm -rf $(OBJS) $(TARGET)
