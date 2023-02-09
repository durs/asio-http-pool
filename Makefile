ifndef TARGET
TARGET = test
endif

OUTDIR = bin
SRCDIR = src
TESTDIR = test
LIBDIRS = /usr/lib/x86_64-linux-gnu
#LIBS = boost_date_time
#LIBS += 

#--------------------------------------------------
# prepare test target

ifeq ($(TARGET),test)
SRCS = $(TESTDIR)/test.cpp
DEFINES = ASIO_POOL_HTTPS_IGNORE
endif

#--------------------------------------------------
# prepare options

CC = cc
CXX = g++
CFLAGS = -c -Wall -Wno-unused-function
CPPFLAGS = -fexceptions
LDFLAGS = -pthread -lrt
LDFLAGS += $(addprefix -l, $(LIBS))
DEFS = $(addprefix -D, $(DEFINES))
INCS = $(addprefix -I , $(INCLUDES))
LDRS = $(addprefix -L, $(LIBDIRS))
OBJS = $(SRCS:.cpp=.opp)
OBJC = $(SRCC:.c=.o)
HDRS = $(PCHDRS:.h=.h.gch)
DIRS += $(SRCDIRS)

#--------------------------------------------------
# prepare debug mode

R_OUTDIR = $(OUTDIR)/release
R_TARGET = $(R_OUTDIR)/$(TARGET)
R_OBJS = $(addprefix $(R_OUTDIR)/, $(OBJS))
R_HDRS = $(HDRS)
R_CFLAGS = -O2 -DNDEBUG

#--------------------------------------------------
# prepare release mode

D_OUTDIR = $(OUTDIR)/debug
D_TARGET = $(D_OUTDIR)/$(TARGET)
D_OBJS = $(addprefix $(D_OUTDIR)/, $(OBJS))
D_HDRS = $(HDRS)
D_CFLAGS = -g -O0 -DDEBUG

#--------------------------------------------------

$(info >>>>> MAKE: $(TARGET))

#--------------------------------------------------
# declare rules

all: release

rebuild: clean all

release: prepare_release ${R_TARGET}
	${info ---> complete release}

debug: prepare_debug ${D_TARGET}
	${info ---> complete debug}

prepare_release:
	${info ---> prepare release}
	@mkdir -p $(R_OUTDIR)/$(SRCDIR) $(R_OUTDIR)/$(TESTDIR)

prepare_debug:
	${info ---> prepare debug}
	@mkdir -p $(D_OUTDIR)/$(SRCDIR) $(D_OUTDIR)/$(TESTDIR)

clean:
	rm -r $(OUTDIR)

# make release target executable
$(R_TARGET): $(R_OBJS)
	${info ---> make release: $@}
	$(CXX) -o $@.exe $(R_OBJS) $(LDFLAGS)

# make debug target executable
$(D_TARGET): $(D_OBJS)
	${info ---> make debug: $@}
	$(CXX) -o $@.exe $(D_OBJS) $(LDFLAGS)

# compile release object files
${R_OUTDIR}/$(SRCDIR)/%.opp: $(SRCDIR)/%.cpp
	${info ---> compile release: $@}
	$(CXX) -o $@ $(CFLAGS) $(CPPFLAGS) $(R_CFLAGS) $(DEFS) $(INCS) $(LDRS) $<
${R_OUTDIR}/$(TESTDIR)/%.opp: $(TESTDIR)/%.cpp
	${info ---> compile release: $@}
	$(CXX) -o $@ $(CFLAGS) $(CPPFLAGS) $(R_CFLAGS) $(DEFS) $(INCS) $(LDRS) $<

# compile debug object files
${D_OUTDIR}/$(SRCDIR)/%.opp: ${SRCDIR}/%.cpp
	${info ---> compile debug: $@}
	$(CXX) -o $@ $(CFLAGS) $(CPPFLAGS) $(D_CFLAGS) $(DEFS) $(INCS) $(LDRS) $<
${D_OUTDIR}/$(TESTDIR)/%.opp: ${TESTDIR}/%.cpp
	${info ---> compile debug: $@}
	$(CXX) -o $@ $(CFLAGS) $(CPPFLAGS) $(D_CFLAGS) $(DEFS) $(INCS) $(LDRS) $<
