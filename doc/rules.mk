include mk/subdir_pre.mk

docs:
	cd doc && doxygen

CLEAN += doc/html doc/latex

.PHONY: docs

include mk/subdir_post.mk
