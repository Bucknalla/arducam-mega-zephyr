# Zephyr Test Framework (ztest)

Source: https://docs.zephyrproject.org/latest/develop/test/ztest.html

## Overview

Zephyr ships a built-in test framework. Tests are compiled as Zephyr applications and run via the **twister** test runner.

## Test Structure

```c
#include <zephyr/ztest.h>

/* Optional: suite-level setup/teardown */
static void *suite_setup(void) { return NULL; }
static void  suite_teardown(void *f) {}
static void  test_before(void *f) {}
static void  test_after(void *f) {}

ZTEST_SUITE(my_suite, NULL, suite_setup, test_before, test_after, suite_teardown);

ZTEST(my_suite, test_addition)
{
    zassert_equal(1 + 1, 2, "expected 2");
}
```

## Assertion Macros (fail-fast)

```c
zassert_true(cond, msg)
zassert_false(cond, msg)
zassert_equal(a, b, msg)
zassert_not_equal(a, b, msg)
zassert_is_null(ptr, msg)
zassert_not_null(ptr, msg)
zassert_mem_equal(buf1, buf2, size, msg)
zassert_str_equal(s1, s2, msg)
```

Test halts immediately on the first failing assertion.

## Expectation Macros (continue on failure)

```c
zexpect_true(cond, msg)
zexpect_equal(a, b, msg)
/* ... same pattern as zassert_ but with zexpect_ prefix */
```

Test continues after failure; the test is marked failed at the end.

## Assumption Macros (skip on failure)

```c
zassume_true(cond, msg)
zassume_equal(a, b, msg)
```

Test is skipped (not failed) if the assumption does not hold.

## Kconfig Options

| Option | Effect |
|--------|--------|
| `CONFIG_ZTEST=y` | Enable the framework |
| `CONFIG_ZTEST_ASSERT_VERBOSE=1` | Print expression details on failure |
| `CONFIG_ZTEST_ASSERT_VERBOSE=0` | Compact output (smaller binary) |

## Running Tests with Twister

```sh
west twister -T tests/ -p native_sim
```

## References

- Test framework docs: https://docs.zephyrproject.org/latest/develop/test/ztest.html
- Assertion macros API: https://docs.zephyrproject.org/apidoc/latest/group__ztest__assert.html
- Test macros API: https://docs.zephyrproject.org/apidoc/latest/group__ztest__test.html
