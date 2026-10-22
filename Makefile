PYTHON ?= python3
.PHONY: all check test test-ui
all:
	$(PYTHON) tools/build.py --build
check:
	$(PYTHON) tools/build.py --check
test:
	$(PYTHON) tools/build.py --host-tests
test-ui:
	$(PYTHON) tools/test_ui.py
