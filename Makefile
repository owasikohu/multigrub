.PHONY: deps build test test-linux test-compat fixture
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
