TARGET = hid_test

CFLAGS = -Wall $(shell pkg-config hidapi-libusb --cflags)

LIBS = $(shell pkg-config hidapi-libusb --libs)

OBJS = hid.o

$(TARGET): $(OBJS)
	$(CC) -o $@ $^ $(LIBS)

clean:
	rm -f $(TARGET) $(OBJS)
