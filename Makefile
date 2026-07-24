clean:
	@Rscript -e 'devtools::clean_dll(".");'

install:
	@Rscript -e 'devtools::install(".");'

STANDARDS := cxx17 cxx20 cxx23
COMPILERS := gcc clang

ALL_CHECKS := $(foreach std,$(STANDARDS),$(foreach comp,$(COMPILERS),check-$(std)-$(comp)))

check: $(ALL_CHECKS)

define run-check
check-$(1)-$(2):
	@echo "Checking C++ code with $(1) standard and $(2) compiler"
	# @$$(MAKE) install
	./scripts/check_prepare.sh "$(1)" "$(2)"; \
	if ! ./scripts/check_run.sh "$(1)" "$(2)"; then \
		echo "Check failed"; \
		./scripts/check_restore.sh "$(1)" "$(2)"; \
		exit 1; \
	fi; \
	./scripts/check_restore.sh "$(1)" "$(2)"
endef

# CRAN-like containers (pair: CRAN name : r-hub image)
CRAN_PAIRS := \
	r-devel-linux-x86_64-debian-clang:ubuntu-clang \
 	r-devel-linux-x86_64-debian-gcc:ubuntu-gcc15 \
 	r-patched-linux-x86_64:ubuntu-next \
 	r-release-linux-x86_64:ubuntu-release

# Extra CRAN check images
CRAN_EXTRA := atlas clang-asan clang-ubsan clang21 clang22 donttest \
	gcc16 gcc-asan lto mkl nold nosuggests rchk valgrind

define run-check-cran
check-cran-$(2):
	@chmod +x ./check-docker/check.sh
	@echo "=== checking $(1) (r-hub: $(2)) ==="
	@./check-docker/check.sh $(2)
endef

check-cran: $(foreach pair,$(CRAN_PAIRS),check-cran-$(word 2,$(subst :, ,$(pair))))

$(foreach pair,$(CRAN_PAIRS),$(eval $(call run-check-cran,$(word 1,$(subst :, ,$(pair))),$(word 2,$(subst :, ,$(pair))))))

define run-check-cran-extra
check-cran-extra-$(1):
	@chmod +x ./check-docker/check.sh
	@echo "=== checking $(1) ==="
	@./check-docker/check.sh $(1)
endef

check-cran-extra: $(foreach rhub,$(CRAN_EXTRA),check-cran-extra-$(rhub))

$(foreach rhub,$(CRAN_EXTRA),$(eval $(call run-check-cran-extra,$(rhub))))

check-cxx:
	@chmod +x ./check-docker/check-cxx.sh
	@./check-docker/check-cxx.sh

clang_format=`which clang-format-21`

format: $(shell find . -name '*.h') $(shell find . -name '*.hpp') $(shell find . -name '*.cpp')
	@${clang_format} -i $?

build-r-devel:
	@echo "Building R-devel from source"
	./scripts/build_r_devel.sh

check-devel:
	@echo "Checking with R-devel (CXX23, gcc)"
	./scripts/check_r_devel.sh cxx23 gcc

$(foreach std,$(STANDARDS),$(foreach comp,$(COMPILERS),$(eval $(call run-check,$(std),$(comp)))))
$(foreach std,$(STANDARDS),$(eval check-$(std)-glang: check-$(std)-clang))
