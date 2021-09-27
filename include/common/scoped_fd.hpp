#pragma once

#include <unistd.h>

#include "common/scoped.hpp"

struct scoped_fd_traits {
    using value_type = int;

    static value_type invalid_value() {
        return 0;
    }

    static void free(value_type& value) {
        close(value);
    }
};

using scoped_fd = scoped_generic<scoped_fd_traits>;
