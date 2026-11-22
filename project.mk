# Project-specific make settings and targets (the repo owns this file).
# The Makefile (managed by the template) includes it first; any *_CMD set here
# is what its target runs (empty = not applicable). Extra targets: add "## help".

MODULE_VERSION := 0.2.0

# Kernel to build against (the running one by default)
KVER ?= $(shell uname -r)
KDIR ?= /lib/modules/$(KVER)/build
KBUILD := $(MAKE) -C $(KDIR) M=$(CURDIR)

# checkpatch.pl from the kernel tree; set CHECKPATCH=/path/to/linux/scripts/checkpatch.pl
CHECKPATCH ?= $(firstword $(wildcard $(KDIR)/scripts/checkpatch.pl) \
	$(shell command -v checkpatch.pl 2>/dev/null))

BUILD_CMD := $(KBUILD) modules
CLEAN_CMD := $(KBUILD) clean
# lint: kernel coding style, without the kernel tree
LINT_CMD := if [ -n "$(CHECKPATCH)" ]; then \
	$(CHECKPATCH) --no-tree --strict -f it8612.c; \
	else echo "checkpatch.pl not found: make lint CHECKPATCH=/path/to/linux/scripts/checkpatch.pl"; exit 1; fi
# test: no unit tests for a hardware driver; build with extra warnings (W=1) and fail on any compiler
# warning in the source or modpost warning (not on kbuild's notes about the toolchain, e.g. a different gcc)
WARNING_RE := \.[ch]:[0-9]+:[0-9]+: warning:|WARNING: modpost
TEST_CMD := out=$$($(KBUILD) W=1 modules 2>&1) || { echo "$$out"; exit 1; }; echo "$$out"; ! grep -Eq '$(WARNING_RE)' <<<"$$out"
# release: set the version in the source, dkms.conf and here
VERSION_CMD := sed -i 's/MODULE_VERSION("[^"]*")/MODULE_VERSION("$(VERSION)")/' it8612.c && \
	sed -i 's/^PACKAGE_VERSION=.*/PACKAGE_VERSION="$(VERSION)"/' dkms.conf && \
	sed -i 's/^MODULE_VERSION := .*/MODULE_VERSION := $(VERSION)/' project.mk

DKMS_SRC := /usr/src/it8612-$(MODULE_VERSION)

.PHONY: dkms-install dkms-remove

dkms-install: ## Install with DKMS, rebuilt for every new kernel (as root); replaces any other installed version
	@for v in $$(dkms status -m it8612 2>/dev/null | sed -n 's|^it8612/\([^,:]*\).*|\1|p' | sort -u); do \
		if [ "$$v" != "$(MODULE_VERSION)" ]; then \
			echo "Removing it8612 $$v"; dkms remove -m it8612 -v "$$v" --all && rm -rf "/usr/src/it8612-$$v"; \
		fi; \
	done
	mkdir -p $(DKMS_SRC)
	cp it8612.c Kbuild Makefile project.mk dkms.conf $(DKMS_SRC)/
	dkms add -m it8612 -v $(MODULE_VERSION)
	dkms install -m it8612 -v $(MODULE_VERSION)

dkms-remove: ## Remove the DKMS module (as root)
	dkms remove -m it8612 -v $(MODULE_VERSION) --all
	rm -rf $(DKMS_SRC)
