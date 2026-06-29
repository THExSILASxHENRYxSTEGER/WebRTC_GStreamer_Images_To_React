# Compiler
CXX := g++

# Flags
CXXFLAGS := -std=c++17 -Wall -Wextra -pthread -g

# Separate compilation flags (--cflags) from linker flags (--libs)
GST_CFLAGS    := $(shell pkg-config --cflags gstreamer-1.0 gstreamer-webrtc-1.0 gstreamer-sdp-1.0 gstreamer-app-1.0)
GST_LIBS      := $(shell pkg-config --libs gstreamer-1.0 gstreamer-webrtc-1.0 gstreamer-sdp-1.0 gstreamer-app-1.0)

OPENCV_CFLAGS := $(shell pkg-config --cflags opencv4)
OPENCV_LIBS   := $(shell pkg-config --libs opencv4)

SRC_DIR := src
BUILD_DIR := build
INC_DIR := include

SRCS := $(wildcard $(SRC_DIR)/*.cpp)
OBJS := $(patsubst $(SRC_DIR)/%.cpp,$(BUILD_DIR)/%.o,$(SRCS))

TARGET := $(BUILD_DIR)/Client

all: $(TARGET)

# Rule for linking the final executable (Requires the LIBS flags at the end)
$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(GST_LIBS) $(OPENCV_LIBS)

# Rule for compiling individual object files (Requires CFLAGS and Include directories)
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp
	@mkdir -p $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(GST_CFLAGS) $(OPENCV_CFLAGS) -I$(INC_DIR) -c $< -o $@

clean:
	rm -rf $(BUILD_DIR)

.PHONY: all clean
