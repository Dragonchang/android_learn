CC := gcc

CFLAGS := -g -I.. -Wall

TARGETS := test gettime

.PHONY: all clean $(TARGETS)

all: $(TARGETS)

$(TARGETS): client.c
	@ echo "Linking $@ ..."
	@ $(CC) $(CFLAGS) -o $@ $< client_$(subst client,,$@).c

clean:
	@ rm -vf $(TARGETS)
#	@ rm -vf $(addprefix run_,$(TARGETS))

