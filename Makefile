SOURCES := main.c http-parser.c

TARGET=http-test

SRCDIR := src
OBJDIR := obj
BINDIR := bin
DEPDIR := .deps
TSTDIR := test
TSTOBJDIR := test/obj
TSTBINDIR := test/bin
TSTDEPDIR := test/.deps
RESULTDIR := test/results
UNITYDIR := unity

BUILD_DIRS = $(BINDIR) $(OBJDIR) $(DEPDIR) $(RESULTDIR) $(TSTOBJDIR) $(TSTBINDIR) $(TSTDEPDIR)

SRC := $(SOURCES:%.c=$(SRCDIR)/%.c)
OBJ := $(SOURCES:%.c=$(OBJDIR)/%.o)
DEPS := $(SOURCES:%.c=$(DEPDIR)/%.d)

SOURCES_TST = $(wildcard $(TSTDIR)/*.c)

CC = gcc
CFLAGS = -Wall -g


TST_CC = gcc
TST_CFLAGS = -Wall -I$(UNITYDIR) -I$(SRCDIR) -g

TST_RESULTS = $(patsubst $(TSTDIR)/test_%.c,$(RESULTDIR)/test_%.txt,$(SOURCES_TST))
TST_DEPS = $(TSTDEPDIR)/*.d


.PHONY: all bin clean erase test build_dirs

all: $(BINDIR)/$(TARGET)

$(TSTBINDIR)/test_http-io: $(TSTOBJDIR)/http-parser.o

-include $(DEPS)
-include $(TST_DEPS)

$(BINDIR)/$(TARGET): build_dirs $(OBJ)
	$(CC) $(CFLAGS) $(OBJ) -o $@

$(OBJDIR)/%.o : $(SRCDIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@
	$(CC) -MM -MT $@ $(CFLAGS) $< > $(DEPDIR)/$*.d

test: build_dirs $(TST_RESULTS)
	@echo "-----------------------"
	@echo "IGNORE:" `grep -o ':IGNORE' $(RESULTDIR)/*.txt|wc -l`
	@echo "-----------------------"
	@grep -s ':IGNORE' $(RESULTDIR)/*.txt || true
	@echo "\n-----------------------"
	@echo "FAIL:" `grep -o ':FAIL' $(RESULTDIR)/*.txt|wc -l`
	@echo "-----------------------"
	@grep -s ':FAIL' $(RESULTDIR)/*.txt || true
	@echo "\n-----------------------"
	@echo "PASS:" `grep -o ':PASS' $(RESULTDIR)/*.txt|wc -l`
	@echo "-----------------------"
	@echo
	@! grep -s FAIL $(RESULTDIR)/*.txt 2>&1 1>/dev/null

build_dirs:
	@mkdir -p $(BUILD_DIRS)


$(RESULTDIR)/%.txt: $(TSTBINDIR)/%
	@echo Running $<
	@echo
	@./$< > $@ || true

$(TSTOBJDIR)/%.o : $(TSTDIR)/%.c
	@echo CC $@
	@$(TST_CC) $(TST_CFLAGS) -c $< -o $@
	@$(TST_CC) -MM -MT $@ $(TST_CFLAGS) $< > $(TSTDEPDIR)/$*.d

$(TSTOBJDIR)/%.o : $(SRCDIR)/%.c
	@echo CC $@
	@$(TST_CC) $(TST_CFLAGS) -c $< -o $@
	@$(TST_CC) -MM -MT $@ $(TST_CFLAGS) $< > $(TSTDEPDIR)/$*.d

$(TSTOBJDIR)/%.o : $(UNITYDIR)/%.c
	@echo CC $@
	@$(TST_CC) $(TST_CFLAGS) -c $< -o $@
	@$(TST_CC) -MM -MT $@ $(TST_CFLAGS) $< > $(TSTDEPDIR)/$*.d

$(TSTBINDIR)/test_%: $(TSTOBJDIR)/test_%.o $(TSTOBJDIR)/%.o $(TSTOBJDIR)/unity.o
	echo $^
	@echo CC $@
	$(TST_CC) -o $@ $^

clean:
	-rm -f $(OBJ) $(DEPS) $(TST_DEPS) $(TSTOBJDIR)/*.o $(TSTBINDIR)/test_* $(RESULTDIR)/*.txt $(BINDIR/$(TARGET)

.PRECIOUS: $(TSTBINDIR)/test_%
.PRECIOUS: $(DEPDIR)/%.d
.PRECIOUS: $(OBJDIR)/%.o
.PRECIOUS: $(RESULTDIR)/%.txt
.PRECIOUS: $(TSTOBJDIR)/%.o
