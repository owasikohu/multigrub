.PHONY: deps build test test-linux fixture
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
