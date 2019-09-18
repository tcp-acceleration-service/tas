#ifndef TESTUTILS_H_
#define TESTUTILS_H_

#include <stddef.h>

void *test_zalloc(size_t len);
void test_randinit(void *buf, size_t len);

void test_error(const char *msg) __attribute__((noreturn));
void test_assert(const char *msg, int cond);
int test_subcase(const char *name, void (*run)(void *), void *param);

#endif // ndef TESTUTILS_H_
