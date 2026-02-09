#include "liva/Driver/Driver.h"

int main(int argc, const char **argv) {
    liva::Driver driver;

    if (!driver.parseArgs(argc, argv))
        return 1;

    return driver.execute();
}
