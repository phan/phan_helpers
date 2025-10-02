# Custom make targets for phan_helpers

benchmark: all
	@echo "Running benchmark..."
	@php -d extension=modules/phan_helpers.so benchmark.php

test-manual: all
	@echo "Running manual tests..."
	@php -d extension=modules/phan_helpers.so tests/001-basic.phpt
	@php -d extension=modules/phan_helpers.so tests/002-large-array.phpt
