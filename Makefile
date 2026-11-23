# Managed by the project template - do not edit. Project settings and
# extra targets go in project.mk; update with `copier update --trust`.

KIND := c-kernel-module
SHELL := bash
.SHELLFLAGS := -eu -o pipefail -c
.DEFAULT_GOAL := help
.PHONY: help setup format lint test check docs build clean release

# project.mk may set any *_CMD below (empty = "no aplica") and add targets
-include project.mk


# $(call step,NAME,VAR): run the command in VAR, or say it does not apply
# (the variable name is passed, not the command: commands contain commas)
define step
	@$(if $(strip $($(2))),echo "→ $(1)" && $($(2)),echo "$(1): no aplica en este proyecto")
endef

help: ## List the targets
	@echo "$(notdir $(CURDIR)) ($(KIND))"
	@grep -hE '^[a-zA-Z0-9_-]+:.*## ' $(MAKEFILE_LIST) | \
		awk -F ':.*## ' '{printf "  make %-10s %s\n", $$1, $$2}'

setup: ## Create the environment (and pre-commit, if configured)
	$(call step,setup,SETUP_CMD)
	@if [ -f .pre-commit-config.yaml ]; then uvx pre-commit install; fi

format: ## Format the code
	$(call step,format,FORMAT_CMD)

lint: ## Static checks; changes nothing
	$(call step,lint,LINT_CMD)

test: ## Run the tests
	$(call step,test,TEST_CMD)

check: lint test ## lint + test (+ R CMD check --as-cran in R)
	@$(if $(strip $(CHECK_CMD)),echo "→ check" && $(CHECK_CMD),:)

docs: ## Build the documentation
	$(call step,docs,DOCS_CMD)

build: ## Build the package, book or PDF
	$(call step,build,BUILD_CMD)

clean: ## Remove what can be regenerated
	$(call step,clean,CLEAN_CMD)

release: ## make release VERSION=x.y.z: check, version, commit and tag
	@[ -n "$(VERSION)" ] || { echo "Uso: make release VERSION=x.y.z"; exit 1; }
	@git diff --quiet && git diff --cached --quiet || \
		{ echo "Hay cambios sin commit"; exit 1; }
	@grep -qs "$(VERSION)" CHANGELOG.md NEWS.md || { echo \
		"Añade $(VERSION) a CHANGELOG.md (o NEWS.md), haz commit y repite"; \
		exit 1; }
	@$(MAKE) --no-print-directory check
	$(call step,version,VERSION_CMD)
	git add -A && git commit -m "Release $(VERSION)" && \
		git tag -a "v$(VERSION)" -m "v$(VERSION)"
	@echo "Etiqueta v$(VERSION) creada; sube con: git push --follow-tags"
