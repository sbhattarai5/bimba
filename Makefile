# Compiler and flags
CXX = g++
CXXFLAGS = -std=c++11
OPENCV_FLAGS = `pkg-config --cflags --libs opencv4`
IP_ADDRESS = 67.148.61.26
CONNECTION_MODE = S

# Source and target
SRC = main.cpp
TARGET = bimba

# Default target: compile and link the program
all:
	$(CXX) $(CXXFLAGS) $(SRC) -o $(TARGET) $(OPENCV_FLAGS)

# Run the program
run: all
	./$(TARGET) $(IP_ADDRESS) $(CONNECTION_MODE)

# Clean up
clean:
	rm -f $(TARGET)
