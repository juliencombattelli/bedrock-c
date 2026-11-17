# Options overridable from environment or command line
CC          = $(WRAPPER) gcc
CPPFLAGS    = -Iinclude
CFLAGS      = -Wall -Wextra -Wpedantic -Wconversion -g -O0 -std=c11
LDFLAGS     =
SANITIZE    = 1
V           = 0
OUT_DIR     = out

Q := @
ifeq ($(V),1)
    Q :=
endif

ifeq ($(SANITIZE),1)
    CFLAGS  += -fsanitize=address,undefined -fno-omit-frame-pointer
    LDFLAGS += -fsanitize=address,undefined -fno-omit-frame-pointer
endif

OBJ_DIR := $(OUT_DIR)/objs
BIN_DIR := $(OUT_DIR)/tests

MODULES := opt

TEST_SRCS := $(MODULES:%=tests/test_%.c)
TESTS     := $(MODULES:%=$(BIN_DIR)/test_%)

OBJS := $(TEST_SRCS:%=$(OBJ_DIR)/%.o)
DEPS := $(OBJS:.o=.d)

all: $(TESTS)

# Per-module convenience targets: `make test-opt`, `make run-opt`
$(MODULES:%=test-%): test-%: $(BIN_DIR)/test_%
$(MODULES:%=run-%): run-%: $(BIN_DIR)/test_%
	@echo "  RUN    $<"
	$(Q) $(WRAPPER) $<

# Run every suite (use `make -k run` to keep going after a failure)
run: $(MODULES:%=run-%)

$(BIN_DIR)/test_%: $(OBJ_DIR)/tests/test_%.c.o
	@echo "  LD     $@"
	@mkdir -p $(@D)
	$(Q) $(CC) $(LDFLAGS) $^ -o $@

$(OBJ_DIR)/%.c.o: %.c
	@echo "  CC     $<"
	@mkdir -p $(@D)
	$(Q) $(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@

clean:
	@echo "  RM     $(OUT_DIR)"
	$(Q) rm -rf $(OUT_DIR)/

list:
	@echo $(MODULES)

.PHONY: all clean run list $(MODULES:%=test-%) $(MODULES:%=run-%)
.SECONDARY: $(OBJS)

-include $(DEPS)
