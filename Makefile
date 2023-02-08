ifndef TARGET
TARGET = test
endif

OUTDIR = bin
SRCDIR = src
TESTDIR = test
LIBDIRS = /usr/lib/x86_64-linux-gnu
LIBS = boost_date_time
LIBS += boost_asio boost_beast

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
CPPFLAGS = -std=c++17 -fexceptions
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
#R_OUTHDR = $(R_OUTDIR)/pch
R_TARGET = $(R_OUTDIR)/$(TARGET)
R_OBJS = $(addprefix $(R_OUTDIR)/, $(OBJS))
R_OBJC = $(addprefix $(R_OUTDIR)/, $(OBJC))
#R_HDRS = $(addprefix $(R_OUTHDR)/, $(HDRS))
R_HDRS = $(HDRS)
R_CFLAGS = -O2 -DNDEBUG

#--------------------------------------------------
# prepare release mode

D_OUTDIR = $(OUTDIR)/debug
#D_OUTHDR = $(D_OUTDIR)/pch
D_TARGET = $(D_OUTDIR)/$(TARGET)
D_OBJS = $(addprefix $(D_OUTDIR)/, $(OBJS))
D_OBJC = $(addprefix $(D_OUTDIR)/, $(OBJC))
#D_HDRS = $(addprefix $(D_OUTHDR)/, $(HDRS))
D_HDRS = $(HDRS)
D_CFLAGS = -g -O0 -DDEBUG

#--------------------------------------------------

#$(info >>>>> $(ICECPP))

#--------------------------------------------------
# declare rules

all: release

rebuild: clean all

release: prepare_release ${R_TARGET}

debug: prepare_debug ${D_TARGET}

prepare_release:
	@mkdir -p $(R_OUTDIR)/$(SRCDIR)

prepare_debug:
	@mkdir -p $(D_OUTDIR)/$(SRCDIR)

clean:
	rm -r $(OUTDIR)
#	rm -rf $(R_TARGET) $(R_OBJS) $(R_OBJC) $(R_HDRS) $(D_TARGET) $(D_OBJS) $(D_OBJC) $(D_HDRS)

# make release target executable
$(R_TARGET): $(R_OBJS) $(R_OBJC)
	$(CXX) -o $@ $(R_OBJS) $(R_OBJC) $(LDFLAGS)

# make debug target executable
$(D_TARGET): $(D_OBJS) $(D_OBJC)
	$(CXX) -o $@ $(D_OBJS) $(D_OBJC) $(LDFLAGS)

# compile release object files
${R_OUTDIR}/$(SRCDIR)/%.opp: $(SRCDIR)/%.cpp
	$(CXX) -o $@ $(CFLAGS) $(CPPFLAGS) $(R_CFLAGS) $(DEFS) $(INCS) $(LDRS) $<
${R_OUTDIR}/$(TESTDIR)/%.opp: $(TESTDIR)/%.cpp
	$(CXX) -o $@ $(CFLAGS) $(CPPFLAGS) $(R_CFLAGS) $(DEFS) $(INCS) $(LDRS) $<

# compile debug object files
${D_OUTDIR}/$(SRCDIR)/%.opp: ${SRCDIR}/%.cpp
	$(CXX) -o $@ $(CFLAGS) $(CPPFLAGS) $(D_CFLAGS) $(DEFS) $(INCS) $(LDRS) $<
${D_OUTDIR}/$(TESTDIR)/%.opp: ${TESTDIR}/%.cpp
	$(CXX) -o $@ $(CFLAGS) $(CPPFLAGS) $(D_CFLAGS) $(DEFS) $(INCS) $(LDRS) $<

# release precompiled headers (not used)
${SRCDIR}/%.h.gch: ${SRCDIR}/%.h
	$(CXX) -o $@ -x c++-header $(CFLAGS) $(CPPFLAGS) $(R_CFLAGS) $(DEFS) $(INCS) $(LDRS) $<

# debug precompiled headers (not used)
${SRCDIR}/%.h.gch: ${SRCDIR}/%.h
	$(CXX) -o $@ -x c++-header $(CFLAGS) $(CPPFLAGS) $(D_CFLAGS) $(DEFS) $(INCS) $(LDRS) $<
