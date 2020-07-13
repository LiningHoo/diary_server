#define CPPHTTPLIB_OPENSSL_SUPPORT

#include <dlfcn.h>
#include "httplib.h"
#include "base64lib.hpp"
#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"
#include "ormpp/dbng.hpp"
#include "ormpp/mysql.hpp"
#include "ormpp/entity.hpp"
#include "diary.hpp"
#include "string"
#include "sys/stat.h"
#include "sys/types.h"
#include "dirent.h"


class DiaryServer {
private:
    httplib::Server svr;
    ormpp::dbng<ormpp::mysql> mysql;
    std::string http_base_url = "http://47.101.59.175:1101/";
public:
    DiaryServer() {
        init();
    }

    void init() {
        // 设置静态文件访问
        svr.set_mount_point("/", "./userData");

        // 连接数据库
        mysql.connect("localhost", "root", "for_diary_mini_program", "diaryDB");

        // 获取 openid
        svr.Post("/openid", [&](const httplib::Request& req, httplib::Response& res){
            // JSON 解析
            rapidjson::Document userJson;
            userJson.Parse(req.body.c_str());
            // 获取post参数
            std::string appid = userJson["appid"].GetString();
            std::string secret = userJson["secret"].GetString();
            std::string code = userJson["code"].GetString();
            httplib::SSLClient cli("api.weixin.qq.com");
            std::string query_string = 
                "/sns/jscode2session?appid=" + appid
                    +"&secret=" + secret
                    +"&js_code=" + code
                    +"&grant_type=authorization_code";
            auto cli_res = cli.Get(query_string.c_str());
            if (cli_res && cli_res->status == 200) {
                res.set_content(cli_res->body, "application/json");
            }
        });

        // 上传日记
        svr.Post("/summit_diary", [&](const httplib::Request& req, httplib::Response& res) {
            // JSON 解析
            rapidjson::Document diaryJson;
            diaryJson.Parse(req.body.c_str());
            // 获取post参数
            std::string openid = diaryJson["openid"].GetString();
            std::string title = diaryJson["title"].GetString();
            std::string content = diaryJson["content"].GetString();
            std::string base64 = diaryJson["base64"].GetString();
            // 写入图片
            int64_t diaryId = (int64_t)mysql.query<diary>().size();
            std::string binary_code = base64lib::Decode(base64.c_str(), base64.size());
            std::string file_name = std::to_string(diaryId)+".png";
            create_folder(std::string("./userData/")+openid);
            std::string file_path = 
                std::string("./userData/")+openid+"/"+file_name;
            base64lib::write_image_to_file(file_path, binary_code);
            std::string image_url = http_base_url + openid + "/" + file_name;
            // 创建对象
            diary temp = {diaryId, 
                                openid, title, content, image_url, 0};
            // 插入数据表
            mysql.begin();
            int ret = mysql.insert(temp);
            mysql.commit();
            // 返回结果
            res.set_content(image_url, "text/plain");
        });

        // 加载openid对应的日记概览
        svr.Get("/load_diaries", [&](const httplib::Request& req, httplib::Response& res) {
            // 获取openid
            std::string openid = req.get_param_value("openid");
            // mysql 查询
            std::string condition = "where openid='" + openid + "'" + " and is_delete=0";
            const auto &result = mysql.query<diary>(condition.c_str());
            // 创建json列表
            rapidjson::StringBuffer strBuffer;
            rapidjson::Writer<rapidjson::StringBuffer> writer(strBuffer);
            writer.StartArray();
            for(const auto &diary: result) {
                writer.StartObject();
                // id
                writer.Key("id");
                writer.Int(diary.id);
                // url
                writer.Key("url");
                writer.String(diary.cover_img_url.c_str());
                // book
                writer.Key("book");
                writer.String(diary.title.c_str());
                writer.EndObject();
            }
            writer.EndArray();
            // 返回json列表
            res.set_content(strBuffer.GetString(), "application/json");
        });

        // 获取openid和日记id对应的日记详情
        svr.Post("/diary_detail", [&](const httplib::Request& req, httplib::Response& res) {
            // JSON 解析
            rapidjson::Document diaryJson;
            diaryJson.Parse(req.body.c_str());
            // 获取post参数
            std::string openid = diaryJson["openid"].GetString();
            std::string id = std::to_string(diaryJson["id"].GetInt());
            // 查询数据库
            std::string condition = "where openid='"+openid+"' and id="+id;
            const auto item = mysql.query<diary>(condition).at(0);
            // 创建json
            rapidjson::StringBuffer strBuffer;
            rapidjson::Writer<rapidjson::StringBuffer> writer(strBuffer);
            writer.StartObject();
            // title
            writer.Key("title");
            writer.String(item.title.c_str());
            // content
            writer.Key("content");
            writer.String(item.content.c_str());
            writer.EndObject();
            // 返回json数据
            res.set_content(strBuffer.GetString(), "application/json");
        });

        // 停止服务
        svr.Get("/stop", [&](const httplib::Request& req, httplib::Response& res) {
            svr.stop();
        });

        // 重置数据库
        svr.Get("/reset", [&](const httplib::Request& req, httplib::Response& res) {
            reset_database();
            res.set_content("Reset database successfully.", "text/plain");
        });
    }

    void reset_database() {
        // drop tables
        mysql.execute("drop table if exists diary");
        //create tables
        mysql.create_datatable<diary>(ormpp_key{"id"}, 
                                ormpp_not_null{{"id", "openid"}});
    }

    void create_folder(std::string path) {
        DIR *dp;
        if((dp = opendir(path.c_str())) == NULL) {
            mkdir(path.c_str(), S_IRUSR | S_IWUSR | S_IXUSR | S_IRWXG | S_IRWXO);
        }
    }

    void start() {
        svr.listen("0.0.0.0", 1101);
    }

    void stop() {
        svr.stop();
    }
};
