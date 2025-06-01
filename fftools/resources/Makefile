clean::
	$(RM) $(CLEANSUFFIXES:%=fftools/resources/%)

vpath %.html $(SRC_PATH)
vpath %.css  $(SRC_PATH)

# Uncomment to prevent deletion during build
#.PRECIOUS: %.css.c %.css.min %.html.c

OBJS-resman +=                     \
    fftools/resources/resman.o     \
    fftools/resources/graph.html.o \
    fftools/resources/graph.css.o  \
