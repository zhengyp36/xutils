.PHONY: all clean
.SECONDARY:

BIN        += xcmp
xcmp-objs  += xutils/file_map.o

TOP_DIR   := $(shell pwd)
SRC_DIR   := $(TOP_DIR)/src
INC_DIR   := $(TOP_DIR)/inc
RELEASE   := $(TOP_DIR)/release
BUILD_DIR := $(TOP_DIR)/build

CFLAGS    += -g -std=gnu99 -I$(INC_DIR)

all: $(patsubst %,$(RELEASE)/%,$(BIN))

clean:
	$(RM) -r $(RELEASE) $(BUILD_DIR)

$(BUILD_DIR)/src/%.o: $(SRC_DIR)/%.c
	@echo "CC $<"
	@mkdir -p $(shell dirname $@)
	@$(CC) $(CFLAGS) -c -o $@ $<

define LINK
$(1)-objs += tools/$(1).o
$$(RELEASE)/$(1): $$(patsubst %.o,$$(BUILD_DIR)/src/%.o,$$($(1)-objs))
	@echo "LD $$(shell pwd)/$$@"
	@mkdir -p $$(shell dirname $$@)
	@$$(CC) $$^ -o $$@
endef

$(foreach bin,$(BIN),$(eval $(call LINK,$(bin))))
