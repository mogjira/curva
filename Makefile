CC = gcc
GLC = glslc

CFLAGS = -Wall -Wno-missing-braces -Wno-attributes -fPIC
LDFLAGS = -L/opt/hfs18.0/dsolib -L/home/michaelb/lib
INFLAGS = -I/opt/hfs18.0/toolkit/include/HAPI -I/home/michaelb/dev
LIBS = -lm -ltanto -lvulkan -lxcb -lxcb-keysyms -lHAPIL
GLFLAGS = --target-env=vulkan1.2
BIN = bin
LIB = /home/michaelb/lib
LIBNAME = curva

O = build
GLSL = shaders
SPV  = shaders/spv

NAME = curva

DEPS =  \
		game.h \
		render.h \
		curva.h \
		common.h \

OBJS =  \
		$(O)/game.o \
		$(O)/render.o \
		$(O)/curva.o \

debug: CFLAGS += -g -DVERBOSE=1
debug: all

release: CFLAGS += -DNDEBUG -O3
release: all

all: bin tags shaders

FRAGS := $(patsubst %.frag,$(SPV)/%-frag.spv,$(notdir $(wildcard $(GLSL)/*.frag)))
VERTS := $(patsubst %.vert,$(SPV)/%-vert.spv,$(notdir $(wildcard $(GLSL)/*.vert)))
TESCS := $(patsubst %.tesc,$(SPV)/%-tesc.spv,$(notdir $(wildcard $(GLSL)/*.tesc)))
TESES := $(patsubst %.tese,$(SPV)/%-tese.spv,$(notdir $(wildcard $(GLSL)/*.tese)))

shaders: $(FRAGS) $(VERTS) $(TESCS) $(TESES)

clean: 
	rm -f $(O)/* $(LIB)/$(LIBNAME) $(BIN)/* $(SPV)/*

tags:
	ctags -R .

bin: main.c $(OBJS) $(DEPS) shaders
	$(CC) $(CFLAGS) $(INFLAGS) $(LDFLAGS) $(OBJS) $< -o $(BIN)/$(NAME) $(LIBS)

lib: $(OBJS) $(DEPS) shaders
	$(CC) -shared -o $(LIB)/lib$(LIBNAME).so $(OBJS)

staticlib: $(OBJS) $(DEPS) shaders
	ar rcs $(LIB)/lib$(NAME).a $(OBJS)

$(O)/%.o:  %.c $(DEPS)
	$(CC) $(CFLAGS) $(INFLAGS) -c $< -o $@

$(SPV)/%-vert.spv: $(GLSL)/%.vert $(DEPS)
	$(GLC) $(GLFLAGS) $< -o $@

$(SPV)/%-frag.spv: $(GLSL)/%.frag
	$(GLC) $(GLFLAGS) $< -o $@

$(SPV)/%-tesc.spv: $(GLSL)/%.tesc
	$(GLC) $(GLFLAGS) $< -o $@

$(SPV)/%-tese.spv: $(GLSL)/%.tese
	$(GLC) $(GLFLAGS) $< -o $@

$(SPV)/%-rchit.spv: $(GLSL)/%.rchit
	$(GLC) $(GLFLAGS) $< -o $@

$(SPV)/%-rgen.spv: $(GLSL)/%.rgen
	$(GLC) $(GLFLAGS) $< -o $@

$(SPV)/%-rmiss.spv: $(GLSL)/%.rmiss
	$(GLC) $(GLFLAGS) $< -o $@
