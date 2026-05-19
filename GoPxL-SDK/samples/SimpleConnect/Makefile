SDK_DIR := ../..

CXX := clang++
CXXFLAGS := -std=c++17 -O2

INCLUDES := -I$(SDK_DIR)/include/kApi -I$(SDK_DIR)/include/GoApi -I$(SDK_DIR)/include/GoPxLSdk
LIBS := -L$(SDK_DIR)/lib -lkApi -lGoApi -lGoPxLSdk
LDFLAGS := -Wl,-rpath,@executable_path/../lib

TARGET := $(SDK_DIR)/bin/SimpleConnect

all: $(TARGET)

$(TARGET): src/SimpleConnect.cpp
	@mkdir -p $(SDK_DIR)/bin
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(LIBS) $(LDFLAGS) -o $@ $<
	@echo "Built: $@"

clean:
	rm -f $(TARGET)

.PHONY: all clean
