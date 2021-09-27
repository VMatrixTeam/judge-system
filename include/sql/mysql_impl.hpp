#pragma once

extern "C" {
#include <mysql/mysql.h>
}

int isolate_mysql_fd(MYSQL *con);