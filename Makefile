SOURCES := main.c http-parser.c http-io.c http-socket.c http-util.c http-server.c http-client.c log.c

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

BUILD_DIRS = $(BINDIR) $(OBJDIR) $(DEPDIR) $(RESULTDIR) $(TSTOBJDIR) $(TSTBINDIR) $(TSTDEPDIR)

SRC := $(SOURCES:%.c=$(SRCDIR)/%.c)
OBJ := $(SOURCES:%.c=$(OBJDIR)/%.o)
DEPS := $(SOURCES:%.c=$(DEPDIR)/%.d)

SOURCES_TST = $(wildcard $(TSTDIR)/*.c)

CC = gcc
CFLAGS = -Wall -g -fsanitize=address -fno-omit-frame-pointer


TST_CC = gcc
TST_CFLAGS = -Wall -I$(SRCDIR) -g -fsanitize=address -fno-omit-frame-pointer --coverage

TST_RESULTS = $(patsubst $(TSTDIR)/test_%.c,$(RESULTDIR)/test_%.txt,$(SOURCES_TST))
TST_DEPS = $(TSTDEPDIR)/*.d


.PHONY: all bin clean erase test build_dirs coverage

all: $(BINDIR)/$(TARGET)

$(TSTBINDIR)/test_http-io: $(TSTOBJDIR)/http-io.o $(TSTOBJDIR)/http-util.o
$(TSTBINDIR)/test_http-parser: $(TSTOBJDIR)/http-parser.o $(TSTOBJDIR)/http-util.o
$(TSTBINDIR)/test_http-util: $(TSTOBJDIR)/http-util.o
$(TSTBINDIR)/test_http-socket: $(TSTOBJDIR)/http-socket.o
$(TSTBINDIR)/test_http-server: $(TSTOBJDIR)/http-server.o
$(TSTBINDIR)/test_http-client: $(TSTOBJDIR)/http-client.o $(TSTOBJDIR)/http-parser.o $(TSTOBJDIR)/http-util.o



-include $(DEPS)
-include $(TST_DEPS)

$(BINDIR)/$(TARGET): build_dirs $(OBJ)
	@echo LD $@
	@$(CC) $(CFLAGS) $(OBJ) -o $@ -lcmocka

$(OBJDIR)/%.o : $(SRCDIR)/%.c
	@echo CC $<
	@$(CC) $(CFLAGS) -c $< -o $@
	@$(CC) -MM -MT $@ $(CFLAGS) $< > $(DEPDIR)/$*.d

test: build_dirs $(TST_RESULTS)
	@echo "-----------------------"
	@echo "SKIPPED:" `grep -o '\[  SKIPPED \]' $(RESULTDIR)/*.txt|wc -l`
	@echo "-----------------------"
	@grep -s '\[  SKIPPED \]' $(RESULTDIR)/*.txt || true
	@echo "\n-----------------------"
	@echo "FAILED:" `grep -o '\[  FAILED  \]' $(RESULTDIR)/*.txt|wc -l`
	@echo "-----------------------"
	@grep -s 'FAILED\|LINE\|ERROR' $(RESULTDIR)/*.txt || true
	@echo "\n-----------------------"
	@echo "PASSED:" `grep -o '\[       OK \]' $(RESULTDIR)/*.txt|wc -l`
	@echo "-----------------------"
	@echo
	@! grep -s '\[  FAILED  \]' $(RESULTDIR)/*.txt 2>&1 1>/dev/null

build_dirs:
	@mkdir -p $(BUILD_DIRS)


$(RESULTDIR)/%.txt: $(TSTBINDIR)/%
	@echo Running $<
	@echo
	@./$< > $@ 2>&1 || true

$(TSTOBJDIR)/%.o : $(TSTDIR)/%.c
	@echo CC $@
	@$(TST_CC) $(TST_CFLAGS) -c $< -o $@
	@$(TST_CC) -MM -MT $@ $(TST_CFLAGS) $< > $(TSTDEPDIR)/$*.d

$(TSTOBJDIR)/%.o : $(SRCDIR)/%.c
	@echo CC $@
	@$(TST_CC) $(TST_CFLAGS) -c $< -o $@
	@$(TST_CC) -MM -MT $@ $(TST_CFLAGS) $< > $(TSTDEPDIR)/$*.d

$(TSTBINDIR)/test_%: $(TSTOBJDIR)/test_%.o
	@echo CC $@
	@$(TST_CC) -o $@ $(TST_CFLAGS) $^ -lcmocka

coverage: test
	@echo Collecting coverage data
	@mkdir -p gcov/html
	@gcov src/http-*.c -o test/obj/ > /dev/null
	@mv *.gcov gcov/
	@lcov --quiet --capture --directory test/obj/ --output-file gcov/coverge.info
	@genhtml --quiet gcov/coverge.info --output-directory gcov/html/
	@xdg-open gcov/html/index.html

clean:
	-rm -f $(OBJ) $(DEPS) $(TST_DEPS) $(TSTOBJDIR)/*.o $(TSTBINDIR)/test_* $(RESULTDIR)/*.txt $(BINDIR/$(TARGET)
	-rm -r gcov/

.PRECIOUS: $(TSTBINDIR)/test_%
.PRECIOUS: $(DEPDIR)/%.d
.PRECIOUS: $(OBJDIR)/%.o
.PRECIOUS: $(RESULTDIR)/%.txt
.PRECIOUS: $(TSTOBJDIR)/%.o
