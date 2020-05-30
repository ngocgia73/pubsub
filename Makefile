#
# Makefile by giann <daniel.nguyen0105@gmail.com>
#

OUT = pubsub
CC = gcc

SRC += src/moda/moda.c \
	   src/moda/moda_cmd_hdl.c \
	   src/modb/modb.c \
	   src/modb/modb_cmd_hdl.c \
	   src/modc/modc.c \
	   src/modc/modc_cmd_hdl.c \
	   src/ipc_sk_srv.c \
	   src/main.c


CFLAGS += -I./inc/moda \
          -I./inc/modb \
		  -I./inc/modc \
		  -I./inc/



OBJ := $(SRC:.c=.o)
all: $(OUT)
	#$(STRIP) $(OUT)
$(OUT): $(OBJ)
	$(CC) $(CFLAGS) -g -o $@ $^ $(LDFLAGS) -lpthread -Xlinker -Map=output.map

%o:%c
	%(CC) $(CFLAGS) -g -o $@ -c $<

clean:
	rm -rf *.o $(OBJ) $(OUT) output output.map

install:
	mkdir -p output
	mv -f $(OUT) output/
