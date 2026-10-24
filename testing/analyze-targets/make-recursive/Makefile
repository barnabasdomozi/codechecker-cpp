# Top-level Makefile: builds each subdirectory with its own Makefile.
SUBDIRS := core tools

all: $(SUBDIRS)

tools: core

$(SUBDIRS):
	$(MAKE) -C $@

clean:
	for d in $(SUBDIRS); do $(MAKE) -C $$d clean; done

.PHONY: all clean $(SUBDIRS)
