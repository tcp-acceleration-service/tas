include mk/subdir_pre.mk

docs:
	doxygen doc/Doxyfile

CLEAN += doc/html doc/latex

.PHONY: docs

include mk/subdir_post.mk
