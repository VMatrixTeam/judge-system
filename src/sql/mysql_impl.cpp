#include "sql/mysql_impl.hpp"
#include <fcntl.h>

//Fake redefinition
struct st_vio {
   my_socket sd;
};

// int isolate_mysql_fd(MYSQL *con) {
    // return fcntl(con->net.vio->sd, F_SETFD, FD_CLOEXEC);
// }