.PHONY: deps build test test-linux test-compat test-isos fixture
deps:
	./scripts/deps.sh
build:
	./scripts/build.sh
fixture:
	./scripts/fixture.sh
test:
	./scripts/test.sh

test-linux:
	./scripts/test-linux.sh

test-compat:
	./scripts/test-compat.sh

test-isos: build
	bash -c 'source scripts/env.sh; exec python3 scripts/test-isos.py'
