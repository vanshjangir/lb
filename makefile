CFLAGS = -g -I./lib -Wall -Wextra

SRCDIR = src
BUILDDIR = build

SOURCES = $(wildcard $(SRCDIR)/*.cpp)

OBJECTS = $(patsubst $(SRCDIR)/*.cpp, $(BUILDDIR)/%.o, $(SOURCES))

TARGET = $(BUILDDIR)/lb

lb: $(TARGET)

$(TARGET): $(OBJECTS)
	g++ $(CFLAGS) $^ -o $@

$(OBJDIR)/%.o: $(SRCDIR)/$.c
	g++ $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILDDIR)/*
