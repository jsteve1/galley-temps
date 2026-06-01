CXX = g++
CXXFLAGS = -std=c++17 -O2 -pthread -DASIO_STANDALONE
CROW_INCLUDE = vendor/crow/include
ASIO_INCLUDE = vendor/asio/include
OUTPUT = build/galley-temps

SRC = src/main.cpp

.PHONY: all clean run

all: $(OUTPUT)

$(OUTPUT): $(SRC) | build
	$(CXX) $(CXXFLAGS) -I$(CROW_INCLUDE) -I$(ASIO_INCLUDE) $(SRC) -o $(OUTPUT)

build:
	mkdir -p build

clean:
	rm -rf build

run: $(OUTPUT)
	./$(OUTPUT)
