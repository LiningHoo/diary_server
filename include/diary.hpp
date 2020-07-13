#pragma once

#include "string"
#include "ormpp/dbng.hpp"

using namespace ormpp;

struct diary {
    int64_t id;
    std::string openid;
    std::string title;
    std::string content;
    std::string cover_img_url;
    int is_delete;
};
REFLECTION(diary, id, openid, title, content, cover_img_url, is_delete)
