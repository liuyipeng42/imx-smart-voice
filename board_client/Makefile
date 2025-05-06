CC      = arm-linux-gnueabihf-gcc
# CC      = gcc

JPEG_INSTALL_PATH = ./extern/jpeg-9e/install
OPENSSL_INSTALL_PATH = ./extern/openssl-3.0.14/install
SQLITE_INSTALL_PATH = ./extern/sqlite

CFLAGS  = -std=gnu99 -Iinclude \
          -I$(JPEG_INSTALL_PATH)/include \
          -I$(OPENSSL_INSTALL_PATH)/include \
          -I$(SQLITE_INSTALL_PATH)

LDFLAGS = -L$(JPEG_INSTALL_PATH)/lib \
          -L$(OPENSSL_INSTALL_PATH)/lib \
          -lssl -lcrypto -ljpeg -lpthread -ldl

TARGET  = build/test
SRC     = src/*.c test.c $(SQLITE_INSTALL_PATH)/sqlite3.c

$(TARGET): $(SRC) | build
	$(CC) $(CFLAGS) $(SRC) $(LDFLAGS) -o $@

build:
	mkdir -p $@

clean:
	rm -rf build

.PHONY: clean
