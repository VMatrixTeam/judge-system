#pragma once

#include <cstdio>

#include "common/scoped.hpp"

struct scoped_file_traits {
    using value_type = FILE*;

    static value_type invalid_value() {
        return nullptr;
    }

    static void free(value_type& value) {
        fclose(value);
    }
};

using scoped_file = scoped_generic<scoped_file_traits>;
