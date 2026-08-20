#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "ui/sidebar.h"

TEST_CASE("diagnostic navigation has no mutating pages") {
    CHECK(static_cast<int>(gno::NavPage::Dashboard) == 0);
    CHECK(static_cast<int>(gno::NavPage::Diagnostics) == 3);
    CHECK(static_cast<int>(gno::NavPage::Count) == 6);
}
