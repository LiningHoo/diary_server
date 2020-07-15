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
#include "gmp/gmpxx.h"
#include "diary.hpp"
#include "account.hpp"
#include "string"
#include "sys/stat.h"
#include "sys/types.h"
#include "dirent.h"
#include "sstream"


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
            if(ret==1) {
                res.set_content("ok", "text/plain");
            } else {
                res.set_content("failed", "text/plain");
            }
        });

        // 删除日记
        svr.Post("/delete_diary", [&](const httplib::Request& req, httplib::Response& res) {
            // JSON 解析
            rapidjson::Document diaryJson;
            diaryJson.Parse(req.body.c_str());
            // 获取post参数
            std::string openid = diaryJson["openid"].GetString();
            std::string id = std::to_string(diaryJson["id"].GetInt());
            // 查询数据库
            std::string condition = "where openid='"+openid+"' and id="+id;
            auto item = mysql.query<diary>(condition).at(0);
            // 更改日记状态
            item.is_delete = 1;
            // 更新数据库
            mysql.begin();
            int ret = mysql.update(item);
            mysql.commit();
            // 返回数据
            if(ret==1) {
                res.set_content("ok", "text/plain");
            } else {
                res.set_content("failed", "text/plain");
            }
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

        // 加载废纸篓
        svr.Get("/load_discarded_diaries", [&](const httplib::Request& req, httplib::Response& res) {
            // 获取openid
            std::string openid = req.get_param_value("openid");
            // mysql 查询
            std::string condition = "where openid='" + openid + "'" + " and is_delete=1";
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
                writer.Key("picture");
                writer.String(diary.cover_img_url.c_str());
                // book
                writer.Key("title");
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

        // 记账
        svr.Post("/write_account", [&](const httplib::Request& req, httplib::Response& res) {
            // JSON 解析
            rapidjson::Document accountJson;
            accountJson.Parse(req.body.c_str());
            // 获取post参数
            std::string openid = accountJson["openid"].GetString();
            std::string title = accountJson["title"].GetString();
            std::string type = accountJson["type"].GetString();
            std::string money = accountJson["money"].GetString();
            std::string date = accountJson["date"].GetString();
            std::string time = accountJson["time"].GetString();
            // 创建对象
            int64_t accountId = (int64_t)mysql.query<account>().size();
            account temp = {accountId, 
                                openid, title, type, money, date, time};
            // 插入数据表
            mysql.begin();
            int ret = mysql.insert(temp);
            mysql.commit();
            // 返回结果
            if(ret==1) {
                res.set_content("ok", "text/plain");
            } else {
                res.set_content("failed", "text/plain");
            }
        });

        // 今日收支
        svr.Get("/today_accounts", [&](const httplib::Request& req, httplib::Response& res) {
            // 获取openid
            std::string openid = req.get_param_value("openid");
            std::string date = req.get_param_value("date");
            // 数据库中查询
            std::string condition = 
                "where openid='"+openid+"' and date='"+date+"'";
            const auto &today_accounts = 
                mysql.query<account>(condition);
            // 计算收入和支出
            mpf_class income, outcome;
            income = 0; outcome = 0;
            for(const auto& account: today_accounts) {
                mpf_class temp;
                temp = account.money.c_str();
                if(account.type=="+") {
                    income = income + temp;
                } else {
                    outcome = outcome + temp;
                }
            }
            // 转成json
            rapidjson::StringBuffer strBuffer;
            rapidjson::Writer<rapidjson::StringBuffer> writer(strBuffer);
            writer.StartObject();
            // title
            writer.Key("income");
            writer.String(mpf_to_string(income).c_str());
            // content
            writer.Key("outcome");
            writer.String(mpf_to_string(outcome).c_str());
            writer.EndObject();
            // 返回数据
            res.set_content(strBuffer.GetString(), "application/json");
        });

        // 收支详情
        svr.Get("/account_detail", [&](const httplib::Request& req, httplib::Response& res) {
            // 获取openid
            std::string openid = req.get_param_value("openid");
            std::string date = req.get_param_value("date");
            // 数据库中查询
            std::string condition = 
                "where openid='"+openid+"' and date='"+date+"'";
            const auto &accounts = 
                mysql.query<account>(condition);
            // json数据
            rapidjson::StringBuffer strBuffer;
            rapidjson::Writer<rapidjson::StringBuffer> writer(strBuffer);
            writer.StartArray();
            for(const auto& account: accounts) {
                writer.StartObject();
                writer.Key("title");
                writer.String(account.title.c_str());
                writer.Key("cate");
                writer.String(account.type.c_str());
                writer.Key("account");
                writer.String(account.money.c_str());
                writer.Key("sdate");
                std::string time = account.date+" "+account.time;
                writer.String(time.c_str());
                writer.EndObject();
            }
            writer.EndArray();
            // 返回数据
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
        // mysql.execute("drop table if exists diary");
        mysql.execute("drop table if exists account");
        //create tables
        // mysql.create_datatable<diary>(ormpp_key{"id"}, 
        //                         ormpp_not_null{{"id", "openid"}});
        mysql.create_datatable<account>(ormpp_key{"id"}, 
                                ormpp_not_null{{"id", "openid"}});
    }

    void create_folder(std::string path) {
        DIR *dp;
        if((dp = opendir(path.c_str())) == NULL) {
            mkdir(path.c_str(), S_IRUSR | S_IWUSR | S_IXUSR | S_IRWXG | S_IRWXO);
        }
    }

    std::string mpf_to_string(mpf_class x) {
        std::ostringstream oss;
        oss << x;
        return oss.str();
        // return x.get_str(1);
    }

    void start() {
        svr.listen("0.0.0.0", 1101);
    }

    void stop() {
        svr.stop();
    }
};
