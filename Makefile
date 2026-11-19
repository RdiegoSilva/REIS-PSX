EE_BIN = REISPSX.ELF
EE_OBJS = main.o

# make DEMO=1 -> versao com jogos de mentira, para ver o layout no PCSX2
ifdef DEMO
EE_CFLAGS += -DDEMO
endif

EE_INCS += -I$(PS2SDK)/ports/include -I$(GSKIT)/include
EE_LDFLAGS += -L$(PS2SDK)/ports/lib -L$(GSKIT)/lib
EE_LIBS += -lgskit -ldmakit -lpad -lc -lkernel

all: $(EE_BIN)

clean:
	rm -f $(EE_BIN) $(EE_OBJS)

include $(PS2SDK)/samples/Makefile.pref
include $(PS2SDK)/samples/Makefile.eeglobal
