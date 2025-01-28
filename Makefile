# Compiler and flags
CXX = g++
CXXFLAGS = -std=c++11
OPENCV_FLAGS = `pkg-config --cflags --libs opencv4`
PEER_IP_ADDRESS = 67.148.61.26
USE_LOW_PORT_FOR_SENDING = Y

# Source and target
SRC = main.cpp
TARGET = bimba

# Default target: compile and link the program
all:
	$(CXX) $(CXXFLAGS) $(SRC) -o $(TARGET) $(OPENCV_FLAGS)

# Run the program
run: all
	./$(TARGET) $(PEER_IP_ADDRESS) $(USE_LOW_PORT_FOR_SENDING)

# Clean up
clean:
	rm -f $(TARGET)
