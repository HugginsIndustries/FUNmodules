CXX = g++
CXXFLAGS = -std=c++11 -Wall -Wextra -O2

# Console version
CONSOLE_TARGET = scale_converter
CONSOLE_SOURCE = scale_converter.cpp

# Web GUI version
WEB_TARGET = scale_converter_web.html

all: $(CONSOLE_TARGET)

console: $(CONSOLE_TARGET)

web: $(WEB_TARGET)

$(CONSOLE_TARGET): $(CONSOLE_SOURCE)
	$(CXX) $(CXXFLAGS) -o $(CONSOLE_TARGET) $(CONSOLE_SOURCE)

$(WEB_TARGET):
	@echo "Web GUI is ready! Open scale_converter_web.html in your browser."

launcher: web_launcher.sh
	chmod +x web_launcher.sh

clean:
	rm -f $(CONSOLE_TARGET)

.PHONY: all console web launcher clean

