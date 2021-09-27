#pragma once

#include <boost/log/trivial.hpp>

std::string LOG_PREFIX(const char *file, int line, const char *function);
void LOG_BEGIN(const std::string &prefix);
void LOG_END();

#define LOG_DEBUG BOOST_LOG_TRIVIAL(debug) << LOG_PREFIX(__FILE__, __LINE__, __FUNCTION__)
#define LOG_INFO BOOST_LOG_TRIVIAL(info) << LOG_PREFIX(__FILE__, __LINE__, __FUNCTION__)
#define LOG_WARN BOOST_LOG_TRIVIAL(warning) << LOG_PREFIX(__FILE__, __LINE__, __FUNCTION__)
#define LOG_ERROR BOOST_LOG_TRIVIAL(error) << LOG_PREFIX(__FILE__, __LINE__, __FUNCTION__)
#define LOG_FATAL BOOST_LOG_TRIVIAL(fatal) << LOG_PREFIX(__FILE__, __LINE__, __FUNCTION__)