CC := clang
BREW_PREFIX := $(shell brew --prefix 2>/dev/null)
LIBPQ_PREFIX := $(shell brew --prefix libpq 2>/dev/null)
CFLAGS := -std=c11 -Wall -Wextra -Wpedantic -g
CPPFLAGS := -DMG_MAX_RECV_SIZE=65536 -Ibackend
BACKEND_BIN := backend/unilost_server
MATCHER_TEST_BIN := backend/matcher_test
AUTH_TEST_BIN := backend/auth_key_test

.PHONY: all build run test clean

all: build

build: $(BACKEND_BIN)

$(BACKEND_BIN): backend/server.c backend/database.c backend/database.h \
                backend/auth.c backend/auth.h \
                backend/matcher.c backend/matcher.h backend/mongoose.c backend/mongoose.h
	@if [ -z "$(LIBPQ_PREFIX)" ] || [ ! -f "$(LIBPQ_PREFIX)/include/libpq-fe.h" ]; then \
		echo "Homebrew libpq headers were not found."; \
		echo "Install PostgreSQL client tools with: brew install libpq"; \
		exit 1; \
	fi
	$(CC) $(CFLAGS) $(CPPFLAGS) -I"$(LIBPQ_PREFIX)/include" \
		backend/server.c backend/database.c backend/auth.c backend/matcher.c backend/mongoose.c \
		-L"$(LIBPQ_PREFIX)/lib" -Wl,-rpath,"$(LIBPQ_PREFIX)/lib" -lpq \
		-framework CFNetwork -framework CoreFoundation \
		-o $(BACKEND_BIN)

run: build
	@set -a; if [ -f .env ]; then . ./.env; fi; set +a; exec ./$(BACKEND_BIN)

$(MATCHER_TEST_BIN): tests/matcher_test.c backend/matcher.c backend/matcher.h
	$(CC) $(CFLAGS) -Ibackend tests/matcher_test.c backend/matcher.c -o $(MATCHER_TEST_BIN)

$(AUTH_TEST_BIN): tests/auth_key_test.c backend/auth.c backend/auth.h backend/mongoose.c backend/mongoose.h
	$(CC) $(CFLAGS) $(CPPFLAGS) -Ibackend tests/auth_key_test.c backend/auth.c backend/mongoose.c \
		-framework CFNetwork -framework CoreFoundation -o $(AUTH_TEST_BIN)

test: $(MATCHER_TEST_BIN) $(AUTH_TEST_BIN)
	./$(MATCHER_TEST_BIN)
	./$(AUTH_TEST_BIN)

clean:
	rm -f $(BACKEND_BIN) $(MATCHER_TEST_BIN) $(AUTH_TEST_BIN) backend/matcher
