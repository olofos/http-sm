# For a verbose build set V to an empty string when calling make: "V= make ..."
V?=@

SOURCES := main.c http-parser.c http-io.c http-socket.c http-util.c http-server.c http-client.c log.c

TARGET=http-test

SRCDIR := src/
OBJDIR := obj/
BINDIR := bin/
DEPDIR := .deps/
TSTDIR := test/
TSTOBJDIR := test/obj/
TSTBINDIR := test/bin/
TSTDEPDIR := test/.deps/
RESULTDIR := test/results/
GCOVDIR := gcov/

BUILD_DIRS = $(BINDIR) $(OBJDIR) $(DEPDIR) $(RESULTDIR) $(TSTOBJDIR) $(TSTBINDIR) $(TSTDEPDIR)

SRC := $(SOURCES:%.c=$(SRCDIR)%.c)
OBJ := $(SOURCES:%.c=$(OBJDIR)%.o)
DEPS := $(SOURCES:%.c=$(DEPDIR)%.d)

SOURCES_TST = $(wildcard $(TSTDIR)*.c)

CC = gcc
CFLAGS = -Wall -g -fsanitize=address -fno-omit-frame-pointer


TST_CC = gcc
TST_CFLAGS = -Wall -I$(SRCDIR) -g -fsanitize=address -fno-omit-frame-pointer --coverage

TST_RESULTS = $(patsubst $(TSTDIR)test_%.c,$(RESULTDIR)test_%.txt,$(SOURCES_TST))
TST_DEPS = $(TSTDEPDIR)*.d


.PHONY: all bin clean erase test build_dirs coverage

all: $(BINDIR)$(TARGET)

$(TSTBINDIR)test_http-io: $(TSTOBJDIR)http-io.o $(TSTOBJDIR)http-util.o
$(TSTBINDIR)test_http-parser: $(TSTOBJDIR)http-parser.o $(TSTOBJDIR)http-util.o
$(TSTBINDIR)test_http-util: $(TSTOBJDIR)http-util.o
$(TSTBINDIR)test_http-socket: $(TSTOBJDIR)http-socket.o
$(TSTBINDIR)test_http-server: $(TSTOBJDIR)http-server.o
$(TSTBINDIR)test_http-client: $(TSTOBJDIR)http-client.o $(TSTOBJDIR)http-parser.o $(TSTOBJDIR)http-util.o

-include $(DEPS)
-include $(TST_DEPS)

$(BINDIR)$(TARGET): build_dirs $(OBJ)
	@echo LD $@
	$(V)$(CC) $(CFLAGS) $(OBJ) -o $@ -lcmocka

$(OBJDIR)%.o : $(SRCDIR)%.c
	@echo CC $<
	$(V)$(CC) $(CFLAGS) -c $< -o $@
	$(V)$(CC) -MM -MT $@ $(CFLAGS) $< > $(DEPDIR)$*.d

test: build_dirs $(TST_RESULTS)
	@echo "-----------------------"
	@echo "SKIPPED:" `grep -o '\[  SKIPPED \]' $(RESULTDIR)*.txt|wc -l`
	@echo "-----------------------"
	@grep -s '\[  SKIPPED \]' $(RESULTDIR)*.txt || true
	@echo "\n-----------------------"
	@echo "FAILED:" `grep -o '\[  FAILED  \]' $(RESULTDIR)*.txt|wc -l`
	@echo "-----------------------"
	@grep -s 'FAILED\|LINE\|ERROR' $(RESULTDIR)*.txt || true
	@echo "\n-----------------------"
	@echo "PASSED:" `grep -o '\[       OK \]' $(RESULTDIR)*.txt|wc -l`
	@echo "-----------------------"
	@echo
	@! grep -s '\[  FAILED  \]' $(RESULTDIR)*.txt 2>&1 1>/dev/null

build_dirs:
	$(V)mkdir -p $(BUILD_DIRS)


$(RESULTDIR)%.txt: $(TSTBINDIR)%
	@echo Running $<
	@echo
	$(V)./$< > $@ 2>&1 || true

$(TSTOBJDIR)%.o : $(TSTDIR)%.c
	@echo CC $@
	$(V)$(TST_CC) $(TST_CFLAGS) -c $< -o $@
	$(V)$(TST_CC) -MM -MT $@ $(TST_CFLAGS) $< > $(TSTDEPDIR)$*.d

$(TSTOBJDIR)%.o : $(SRCDIR)%.c
	@echo CC $@
	$(V)$(TST_CC) $(TST_CFLAGS) -c $< -o $@
	$(V)$(TST_CC) -MM -MT $@ $(TST_CFLAGS) $< > $(TSTDEPDIR)$*.d

$(TSTBINDIR)test_%: $(TSTOBJDIR)test_%.o
	@echo CC $@
	$(V)$(TST_CC) -o $@ $(TST_CFLAGS) $^ -lcmocka

coverage: test
	@echo Collecting coverage data
	$(V)mkdir -p $(GCOVDIR)html
	$(V)gcov src/http-*.c -o $(TSTOBJDIR) > /dev/null
	$(V)mv *.gcov $(GCOVDIR)
	$(V)lcov --quiet --capture --directory $(TSTOBJDIR) --output-file $(GCOVDIR)coverge.info
	$(V)genhtml --quiet gcov/coverge.info --output-directory $(GCOVDIR)html/
	$(V)xdg-open $(GCOVDIR)html/index.html

clean:
	@echo Cleaning
	$(V)-rm -f $(OBJ) $(DEPS) $(TST_DEPS) $(TSTOBJDIR)*.o $(TSTBINDIR)test_* $(RESULTDIR)*.txt $(BINDIR/$(TARGET)
	$(V)-rm -rf $(GCOVDIR)

.PRECIOUS: $(TSTBINDIR)test_%
.PRECIOUS: $(DEPDIR)%.d
.PRECIOUS: $(OBJDIR)%.o
.PRECIOUS: $(RESULTDIR)%.txt
.PRECIOUS: $(TSTOBJDIR)%.o
