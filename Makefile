# For a verbose build set V to an empty string when calling make: "V= make ..."
V?=@

LIBSOURCES := http-parser.c http-io.c http-socket.c http-util.c http-server.c http-server-main.c http-client.c

BINSOURCES := main.c log.c

TARGET=http-test
LIBTARGET=libhttp-sm.a

SRCDIR := src/
BINSRCDIR := main/
OBJDIR := obj/
BINDIR := bin/
LIBDIR := lib/
DEPDIR := .deps/
TSTDIR := test/
TSTOBJDIR := test/obj/
TSTBINDIR := test/bin/
TSTDEPDIR := test/.deps/
RESULTDIR := test/results/
GCOVDIR := gcov/

BUILD_DIRS = $(BINDIR) $(OBJDIR) $(LIBDIR) $(DEPDIR) $(RESULTDIR) $(TSTOBJDIR) $(TSTBINDIR) $(TSTDEPDIR)

LIBSRC := $(LIBSOURCES:%.c=$(SRCDIR)%.c)
LIBOBJ := $(LIBSOURCES:%.c=$(OBJDIR)%.o)
LIBDEPS := $(LIBSOURCES:%.c=$(DEPDIR)%.d)

BINSRC := $(BINSOURCES:%.c=$(SRCDIR)%.c)
BINOBJ := $(BINSOURCES:%.c=$(OBJDIR)%.o)
BINDEPS := $(BINSOURCES:%.c=$(DEPDIR)%.d)

SOURCES_TST = $(wildcard $(TSTDIR)*.c)

AR = ar
CC = gcc
CFLAGS = -Wall -g -fsanitize=address -fno-omit-frame-pointer -I$(SRCDIR) -I$(BINSRCDIR)

INCLUDES=-Iinclude/

TST_CC = gcc
TST_WRAP = -Wl,--wrap=malloc,--wrap=free,--wrap=read,--wrap=write
TST_CFLAGS = -Wall -I$(SRCDIR) -I$(BINSRCDIR) -g -fsanitize=address -fno-omit-frame-pointer --coverage $(TST_WRAP)

TST_RESULTS = $(patsubst $(TSTDIR)test_%.c,$(RESULTDIR)test_%.txt,$(SOURCES_TST))
TST_DEPS = $(TSTDEPDIR)*.d


.PHONY: all bin clean erase test build_dirs coverage

all: $(BINDIR)$(TARGET)

$(TSTBINDIR)test_http-io: $(TSTOBJDIR)http-io.o $(TSTOBJDIR)http-util.o $(TSTOBJDIR)test-util.o
$(TSTBINDIR)test_http-io_wrap: $(TSTOBJDIR)http-io.o $(TSTOBJDIR)http-util.o $(TSTOBJDIR)test-util.o
$(TSTBINDIR)test_http-parser: $(TSTOBJDIR)http-parser.o $(TSTOBJDIR)http-util.o $(TSTOBJDIR)test-util.o
$(TSTBINDIR)test_http-util: $(TSTOBJDIR)http-util.o $(TSTOBJDIR)test-util.o
$(TSTBINDIR)test_http-socket: $(TSTOBJDIR)http-socket.o $(TSTOBJDIR)test-util.o
$(TSTBINDIR)test_http-server: $(TSTOBJDIR)http-server.o $(TSTOBJDIR)test-util.o
$(TSTBINDIR)test_http-client: $(TSTOBJDIR)http-client.o $(TSTOBJDIR)http-parser.o $(TSTOBJDIR)http-util.o $(TSTOBJDIR)test-util.o

-include $(LIBDEPS)
-include $(BINDEPS)
-include $(TST_DEPS)

$(BINDIR)$(TARGET): build_dirs $(BINOBJ) $(LIBDIR)$(LIBTARGET)
	@echo LD $@
	$(V)$(CC) $(CFLAGS) $(BINOBJ) -o $@ -lcmocka -L$(LIBDIR) -lhttp-sm

$(LIBDIR)$(LIBTARGET): build_dirs $(LIBOBJ)
	@echo AR $@
	$(V)$(AR) cr $@ $(LIBOBJ)

$(OBJDIR)%.o : $(SRCDIR)%.c
	@echo CC $<
	$(V)$(CC) $(CFLAGS)  $(INCLUDES) -c $< -o $@
	$(V)$(CC) -MM -MT $@ $(CFLAGS)  $(INCLUDES) $< > $(DEPDIR)$*.d

$(OBJDIR)%.o : $(BINSRCDIR)%.c
	@echo CC $<
	$(V)$(CC) $(CFLAGS)  $(INCLUDES) -c $< -o $@
	$(V)$(CC) -MM -MT $@ $(CFLAGS)  $(INCLUDES) $< > $(DEPDIR)$*.d

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
	$(V)$(TST_CC) $(TST_CFLAGS)  $(INCLUDES) -c $< -o $@
	$(V)$(TST_CC) -MM -MT $@ $(TST_CFLAGS) $(INCLUDES) $< > $(TSTDEPDIR)$*.d

$(TSTOBJDIR)%.o : $(SRCDIR)%.c
	@echo CC $@
	$(V)$(TST_CC) $(TST_CFLAGS)  $(INCLUDES) -c $< -o $@
	$(V)$(TST_CC) -MM -MT $@ $(TST_CFLAGS) $(INCLUDES) $< > $(TSTDEPDIR)$*.d

$(TSTBINDIR)test_%: $(TSTOBJDIR)test_%.o
	@echo CC $@
	$(V)$(TST_CC) -o $@ $(TST_CFLAGS) $^ -lcmocka

coverage: test
	@echo Collecting coverage data
	$(V)mkdir -p $(GCOVDIR)html
	$(V)gcov src/http-*.c -o $(TSTOBJDIR) > /dev/null
	$(V)mv *.gcov $(GCOVDIR)
	$(V)lcov --quiet --capture --directory $(TSTOBJDIR) --output-file $(GCOVDIR)coverage.info
	$(V)genhtml --quiet gcov/coverage.info --output-directory $(GCOVDIR)html/
	$(V)lcov --quiet --remove $(GCOVDIR)coverage.info 'test/*' '/usr/*' --output-file $(GCOVDIR)coverage-src.info
	@echo
	$(V)lcov --summary $(GCOVDIR)coverage-src.info 2>&1 | grep -v 'Reading\|branches'

coverage-open: coverage
	$(V)xdg-open $(GCOVDIR)html/index.html

clean:
	@echo Cleaning
	$(V)-rm -f $(LIBOBJ) $(LIBDEPS) $(BINOBJ) $(BINDEPS) $(TST_DEPS) $(TSTOBJDIR)*.o $(TSTOBJDIR)*.gcda $(TSTOBJDIR)*.gcno $(TSTBINDIR)test_* $(RESULTDIR)*.txt $(BINDIR)$(TARGET) $(LIBDIR)$(LIBTARGET)
	$(V)-rm -rf $(GCOVDIR)

.PRECIOUS: $(TSTBINDIR)test_%
.PRECIOUS: $(DEPDIR)%.d
.PRECIOUS: $(OBJDIR)%.o
.PRECIOUS: $(RESULTDIR)%.txt
.PRECIOUS: $(TSTOBJDIR)%.o
