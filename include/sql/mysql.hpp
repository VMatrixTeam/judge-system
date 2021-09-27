#pragma once

#include <climits>
#include <cstring>
#include <iostream>
#include <map>
#include <stdexcept>
#include <utility>
#include <vector>
#include "common/exceptions.hpp"
#include "sql/entity.hpp"
#include "sql/type_mapping.hpp"
#include "sql/utility.hpp"
#include "sql/mysql_impl.hpp"

namespace ormpp {

class mysql {
public:
    ~mysql() {
        disconnect();
    }

    bool connect(const char *host, const char *user, const char *passwd, const char *db, int port = 0, int timeout = -1) {
        if (con_ != nullptr) {
            mysql_close(con_);
        }

        con_ = mysql_init(nullptr);
        if (con_ == nullptr) {
            set_last_error("mysql init failed");
            return false;
        }

        if (timeout > 0) {
            if (mysql_options(con_, MYSQL_OPT_CONNECT_TIMEOUT, &timeout) != 0) {
                set_last_error(mysql_error(con_));
                return false;
            }
        }

        char value = 1;
        mysql_options(con_, MYSQL_OPT_RECONNECT, &value);
        mysql_options(con_, MYSQL_SET_CHARSET_NAME, "utf8");

        if (mysql_real_connect(con_, host, user, passwd, db, port, nullptr, 0) == nullptr) {
            set_last_error(mysql_error(con_));
            return false;
        }

        // isolate_mysql_fd(con_);

        return true;
    }

    void set_last_error(std::string last_error) {
        last_error_ = std::move(last_error);
        std::cout << last_error_ << std::endl;  //todo, write to log file
    }

    std::string get_last_error() const {
        return last_error_;
    }

    bool ping() {
        return mysql_ping(con_) == 0;
    }

    template <typename... Args>
    bool disconnect(Args&&... args) {
        if (con_ != nullptr) {
            mysql_close(con_);
            con_ = nullptr;
        }

        return true;
    }

    //for tuple and string with args...
    template <typename T, typename... Args>
    constexpr int query(std::vector<T>& result, const char* sql, Args&&... args) {
        static_assert(iguana::is_tuple<T>::value);

        constexpr auto args_sz = sizeof...(Args);
        if (args_sz != std::count(sql, sql + strlen(sql), '?')) {
            // or we can do compile-time checking
            BOOST_THROW_EXCEPTION(judge::judge_exception("argument number not matched"));
        }

        if (!(stmt_ = mysql_stmt_init(con_))) {
            BOOST_THROW_EXCEPTION(judge::database_error(mysql_error(con_)));
        }

        if (mysql_stmt_prepare(stmt_, sql, strlen(sql))) {
            BOOST_THROW_EXCEPTION(judge::database_error(mysql_error(con_)));
        }

        std::tuple<Args...> param_tp{args...};
        MYSQL_BIND params[args_sz];
        if constexpr (args_sz > 0) {
            memset(params, 0, sizeof params);

            size_t index = 0;
            iguana::for_each(param_tp, [&params, &index](auto& item, auto /* I */) {
                using U = std::decay_t<decltype(item)>;
                if constexpr (std::is_arithmetic_v<U> && std::is_integral_v<U>) {
                    params[index].buffer_type = (enum_field_types)ormpp_mysql::type_to_id(identity<std::make_signed_t<U>>{});
                    params[index].buffer = (void*)&item;
                    params[index].is_unsigned = std::is_unsigned_v<U>;
                } else if constexpr (std::is_arithmetic_v<U>) {
                    params[index].buffer_type = (enum_field_types)ormpp_mysql::type_to_id(identity<U>{});
                    params[index].buffer = (void*)&item;
                } else if constexpr (std::is_same_v<std::string, U>) {
                    params[index].buffer_type = MYSQL_TYPE_STRING;
                    params[index].buffer = (void*)item.data();
                    params[index].buffer_length = item.length();
                } else if constexpr (std::is_same_v<const char*, U> || std::is_same_v<char*, U>) {
                    params[index].buffer_type = MYSQL_TYPE_VAR_STRING;
                    params[index].buffer = (void*)item;
                    params[index].buffer_length = strlen(item);
                } else {
                    std::cout << typeid(U).name() << std::endl;
                    static_assert(std::is_arithmetic_v<U> || std::is_same_v<std::string, U> || std::is_same_v<const char*, U> || std::is_same_v<char*, U>,
                                  "Unrecognized type, only arithmetic types and string are allowed");
                }

                static_assert(!std::is_arithmetic_v<U> || sizeof(U) >= 4, "bool, char, short are not supported");
                ++index;
            });

            if (mysql_stmt_bind_param(stmt_, params)) {
                BOOST_THROW_EXCEPTION(judge::database_error(mysql_error(con_)));
            }
        }

        auto guard = guard_statment(stmt_);

        std::vector<std::vector<char>> mp;
        MYSQL_BIND results[result_size<T>::value];
        T result_tp{};
        if constexpr (result_size<T>::value > 0) {
            memset(results, 0, sizeof results);

            size_t index = 0;
            iguana::for_each(result_tp, [&results, &mp, &index](auto& item, auto /* I */) {
                using U = std::decay_t<decltype(item)>;
                if constexpr (std::is_arithmetic_v<U> && std::is_integral_v<U>) {
                    results[index].buffer_type = (enum_field_types)ormpp_mysql::type_to_id(identity<std::make_signed_t<U>>{});
                    results[index].buffer = &item;
                    results[index].is_unsigned = std::is_unsigned_v<U>;
                } else if constexpr (std::is_arithmetic_v<U>) {
                    results[index].buffer_type = (enum_field_types)ormpp_mysql::type_to_id(identity<U>{});
                    results[index].buffer = (void*)&item;
                } else if constexpr (std::is_same_v<std::string, U>) {
                    mp.emplace_back(65536, 0);
                    results[index].buffer_type = MYSQL_TYPE_STRING;
                    results[index].buffer = &(mp.back()[0]);
                    results[index].buffer_length = 65536;
                } else {
                    std::cout << typeid(U).name() << std::endl;
                    static_assert(std::is_arithmetic_v<U> || std::is_same_v<std::string, U>,
                                  "Unrecognized type, only arithmetic types and string are allowed");
                }
                ++index;
            });

            if (auto ret = mysql_stmt_bind_result(stmt_, results)) {
                BOOST_THROW_EXCEPTION(judge::database_error(mysql_error(con_)));
            }
        }

        if (auto ret = mysql_stmt_execute(stmt_)) {
            BOOST_THROW_EXCEPTION(judge::database_error(mysql_error(con_)));
        }

        int ret = mysql_stmt_affected_rows(stmt_);

        while (mysql_stmt_fetch(stmt_) == 0) {
            if constexpr (result_size<T>::value > 0) {
                auto it = mp.begin();
                iguana::for_each(result_tp, [&mp, &it](auto& item, auto /* i */) {
                    using U = std::decay_t<decltype(item)>;
                    if constexpr (std::is_same_v<std::string, U>) {
                        item = std::string(&(*it)[0], strlen((*it).data()));
                        it++;
                    }
                });

                result.push_back(std::move(result_tp));
            } else {
                result.emplace_back();
            }
        }

        return ret;
    }

    bool has_error() {
        return has_error_;
    }

    //transaction
    bool begin() {
        if (mysql_query(con_, "BEGIN")) {
            //                fprintf(stderr, "%s\n", mysql_error(con_));
            return false;
        }

        return true;
    }

    bool commit() {
        if (mysql_query(con_, "COMMIT")) {
            //                fprintf(stderr, "%s\n", mysql_error(con_));
            return false;
        }

        return true;
    }

    bool rollback() {
        if (mysql_query(con_, "ROLLBACK")) {
            //                fprintf(stderr, "%s\n", mysql_error(con_));
            return false;
        }

        return true;
    }

private:
    struct guard_statment {
        guard_statment(MYSQL_STMT* stmt) : stmt_(stmt) {}
        MYSQL_STMT* stmt_ = nullptr;
        int status_ = 0;
        ~guard_statment() {
            if (stmt_ != nullptr)
                status_ = mysql_stmt_close(stmt_);

            if (status_)
                fprintf(stderr, "close statment error code %d\n", status_);
        }
    };

    MYSQL* con_ = nullptr;
    MYSQL_STMT* stmt_ = nullptr;
    bool has_error_ = false;
    std::string last_error_;
    inline static std::map<std::string, std::string> auto_key_map_;
};
}  // namespace ormpp
