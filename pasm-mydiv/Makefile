TARGET = mydiv.exe
OBJS = mydiv.o
Z_TOOLS = ../z_tools

CC = $(Z_TOOLS)/mingw32-gcc/bin/gcc
CFLAGS += -Wall

.PHONY: all
all :
	make $(TARGET)

%.o : %.c Makefile
	$(CC) $(CFLAGS) -c $<

$(TARGET) : $(OBJS) Makefile
	$(CC) -o $@ $(OBJS)
