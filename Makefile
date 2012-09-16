TARGET=mcsign
SRC_FILES=mcsign.c region.c
CNBT_DIR=external/cnbt/
EXTRA_DEPS=$(CNBT_DIR)/libnbt.a

CNBT_CFLAGS=-Iexternal/cnbt/ -std=c99
CFLAGS=$(CNBT_CFLAGS) $(shell pkg-config --cflags glib-2.0) -g
#CFLAGS+=-DDEBUG

CNBT_LDFLAGS=-L$(CNBT_DIR) -lnbt -lz
LDFLAGS=$(CNBT_LDFLAGS) $(shell pkg-config --libs glib-2.0)

.PHONY: clean

$(TARGET): $(subst .c,.o,$(SRC_FILES)) $(EXTRA_DEPS)

$(CNBT_DIR)/libnbt.a:
	make -C $(CNBT_DIR)

clean:
	rm -f *.o $(TARGET)
