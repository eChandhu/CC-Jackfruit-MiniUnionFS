CC      = gcc
# Changed fuse to fuse3 to match your code's FUSE_USE_VERSION 31
CFLAGS  = -Wall -Wextra -g `pkg-config fuse3 --cflags`
LDFLAGS = `pkg-config fuse3 --libs`
TARGET  = mini_unionfs

all: $(TARGET)

$(TARGET): mini_unionfs.c
	$(CC) $(CFLAGS) -o $(TARGET) mini_unionfs.c $(LDFLAGS)

clean:
	rm -f $(TARGET)

.PHONY: all clean