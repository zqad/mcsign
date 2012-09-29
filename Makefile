TARGET=mcsign
SRC_FILES=mcsign.c region.c
CNBT_DIR=external/cnbt/
EXTRA_DEPS=$(CNBT_DIR)/libnbt.a

CNBT_CFLAGS=-Iexternal/cnbt/ -std=c99
CFLAGS=$(CNBT_CFLAGS) $(shell pkg-config --cflags glib-2.0) -g -Wunused-parameter
#CFLAGS+=-DDEBUG

CNBT_LDFLAGS=-L$(CNBT_DIR) -lnbt -lz
LDFLAGS=$(CNBT_LDFLAGS) $(shell pkg-config --libs glib-2.0)

.PHONY: clean depend

default: $(TARGET)

# Magic to build Makefile.depend if it does not exist
ifndef DEPEND_RECURSE
ifeq ($(shell test -e Makefile.depend && echo yes),yes)
include Makefile.depend
else
# Do not want to try to do this again when we get called from here, hence the
# recurse block
$(info $(shell make depend DEPEND_RECURSE=1))
include Makefile.depend
endif
endif

$(TARGET): $(subst .c,.o,$(SRC_FILES)) $(EXTRA_DEPS)

$(CNBT_DIR)/libnbt.a:
	make -C $(CNBT_DIR)

clean:
	rm -f *.o $(TARGET)

depend: Makefile.depend

Makefile.depend: $(SRC_FILES)
	$(CC) $(CFLAGS) -MM $^ > $@
