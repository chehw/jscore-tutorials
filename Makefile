BIN_DIR=bin
TARGETS=$(BIN_DIR)/simple $(BIN_DIR)/tiny-dom

DEBUG ?= 1
OPTIMIZE ?= -O2

CC=gcc -std=gnu99 -D_DEFAULT_SOURCE -D_GNU_SOURCE
LINKER=gcc -std=gnu99 -D_DEFAULT_SOURCE -D_GNU_SOURCE

CFLAGS = -Wall -Iinclude -Iutils -Isrc
LIBS = -lm -lpthread -lcurl -ljson-c

ifeq ($(DEBUG),1)
CFLAGS += -g -D_DEBUG
OPTIMIZE = -O0
endif
LDFLAGS = $(OPTIMIZE) $(CFLAGS)

CFLAGS += $(shell pkg-config --cflags javascriptcoregtk-4.0)
LIBS += $(shell pkg-config --libs javascriptcoregtk-4.0)

CFLAGS += $(shell pkg-config --cflags webkit2gtk-4.0)
LIBS += $(shell pkg-config --libs webkit2gtk-4.0)


SRC_DIR=src
OBJ_DIR=obj
SOURCES := $(wildcard $(SRC_DIR)/*.c)
OBJECTS := $(SOURCES:$(SRC_DIR)/%.c=$(OBJ_DIR)/%.o)


UTILS_SRC_DIR=utils
UTILS_OBJ_DIR=obj/utils
UTILS_SOURCES := $(wildcard $(UTILS_SRC_DIR)/*.c)
UTILS_OBJECTS := $(UTILS_SOURCES:$(UTILS_SRC_DIR)/%.c=$(UTILS_OBJ_DIR)/%.o)


# check dependencies
ifneq (,$(wildcard $(UTILS_SRC_DIR)/regex.*))
LIBS += -lpcre
endif


all: do_init $(TARGETS)

$(BIN_DIR)/simple: $(OBJ_DIR)/simple.o $(OBJ_DIR)/net-utils.o $(OBJ_DIR)/js-utils.o $(UTILS_OBJECTS)
	$(LINKER) $(LDFLAGS) -o $@ $^ $(LIBS)

$(BIN_DIR)/tiny-dom: $(OBJ_DIR)/tiny-dom.o $(OBJ_DIR)/net-utils.o $(OBJ_DIR)/js-utils.o $(UTILS_OBJECTS)
	$(LINKER) $(LDFLAGS) -o $@ $^ $(LIBS) $(shell pkg-config --cflags --libs libxml-2.0)

$(OBJECTS): $(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) -o $@ -c $< $(CFLAGS)
	
$(UTILS_OBJECTS): $(UTILS_OBJ_DIR)/%.o: $(UTILS_SRC_DIR)/%.c
	$(CC) -o $@ -c $< $(CFLAGS)

.PHONY: do_init clean
do_init:
	mkdir -p $(OBJ_DIR) $(UTILS_OBJ_DIR) $(BIN_DIR)

clean:
	rm -f $(OBJ_DIR)/*.o $(UTILS_OBJ_DIR)/*.o $(TARGETS)
