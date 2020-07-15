#pragma once

#include "string"
#include "ormpp/dbng.hpp"

using namespace ormpp;

struct account {
    int64_t id;
    std::string openid;
    std::string title;
    std::string type;
    std::string money;
    std::string date;
    std::string time;
};
REFLECTION(account, id, openid, title, type, money, date, time)