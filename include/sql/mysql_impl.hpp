#pragma once

extern "C" {
#include <mysql.h>
}

int isolate_mysql_fd(MYSQL *con);