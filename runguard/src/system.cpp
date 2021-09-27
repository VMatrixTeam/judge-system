#include "system.hpp"

#include <errno.h>
#include <grp.h>
#include <pwd.h>
#include <sys/types.h>
#include <unistd.h>

#include <boost/log/trivial.hpp>

int get_userid(const char *name) {
    BOOST_LOG_TRIVIAL(debug) << "name = " << name;
    BOOST_LOG_TRIVIAL(debug) << "running: uid = " << getuid() << " gid = " << getgid();

    struct passwd *pwd;

    errno = 0;
    pwd = getpwnam(name);

    BOOST_LOG_TRIVIAL(debug) << "in function get_userid: !pwd = " << (!pwd) << " errno = " << errno;

    if (!pwd || errno) return -1;

    return (int)pwd->pw_uid;
}

int get_groupid(const char *name) {
    BOOST_LOG_TRIVIAL(debug) << "name = " << name;
    BOOST_LOG_TRIVIAL(debug) << "running: uid = " << getuid() << " gid = " << getgid();

    struct group *g;

    errno = 0;
    g = getgrnam(name);

    BOOST_LOG_TRIVIAL(debug) << "in function get_groupid: !g = " << (!g) << " errno = " << errno;

    if (!g || errno) return -1;
    return (int)g->gr_gid;
}