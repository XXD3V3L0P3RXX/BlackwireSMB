PYTHON ?= python3
.PHONY: all check
# The native single-ELF build owns these embedded assets.
all: main.c ../desk/index.html param.json icon0.png
	$(PYTHON) ../../../tools/build.py --build
check:
	$(PYTHON) ../../../tools/build.py --check
