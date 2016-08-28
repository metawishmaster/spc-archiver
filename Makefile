LDLIBS = -llzo2

OBJDIR = ./obj

PACKER_OBJS = $(OBJDIR)/pack999.o
UNPACKER_OBJS = $(OBJDIR)/unpack999.o

PACKER_EXEC = pack999 
UNPACKER_EXEC = unpack999

PLATFORMS += x86 #mips
TARGETS = $(PACKER_EXEC) $(UNPACKER_EXEC)

all:	$(PLATFORMS)

x86:
	mkdir -p ./obj/x86
	$(MAKE) OBJDIR=./obj/x86 CC=gcc build

mips:
	mkdir -p ./obj/mips
	$(MAKE) OBJDIR=./obj/mips CC=/opt/brcm/hndtools-mipsel-uclibc/bin/mipsel-uclibc-gcc build

build:	$(TARGETS)

$(OBJDIR)/%.o: ./%.c
	$(CC) -o $(OBJDIR)/$*.o -g -W -Wall -c $< 

$(PACKER_EXEC):	$(PACKER_OBJS)
	$(CC) -o ./$(PACKER_EXEC) $(PACKER_OBJS) $(LDLIBS)
$(UNPACKER_EXEC):	$(UNPACKER_OBJS)
	$(CC) -o ./$(UNPACKER_EXEC) $(UNPACKER_OBJS) $(LDLIBS)

clean:		
	rm -rf $(OBJDIR) $(PACKER_EXEC) $(UNPACKER_EXEC) core.*

