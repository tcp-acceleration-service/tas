include mk/subdir_pre.mk

docs:
	doxygen doc/Doxyfile
	sphinx-build doc/ doc/_build

CLEAN += doc/html doc/latex doc/xml doc/_build

.PHONY: docs

include mk/subdir_post.mk
