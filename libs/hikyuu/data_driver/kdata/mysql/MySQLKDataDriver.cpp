/*
 * MySQLKDataDriverImp.cpp
 *
 *  Created on: 2014年9月3日
 *      Author: fasiondog
 */

#include <boost/lexical_cast.hpp>
#include "../../../Log.h"
#include "MySQLKDataDriver.h"

namespace hku {

class MySQLCloser{
public:
    void operator()(MYSQL *db){
        if(db){
            mysql_close(db);
            //HKU_TRACE("Closed MySQL database!");
        }
    }
};

MySQLKDataDriver::MySQLKDataDriver(): KDataDriver("mysql") {

}

bool MySQLKDataDriver::_init() {
    string func_name(" [MySQLKDataDriver::MySQLKDataDriver]");

    string default_host("127.0.0.1");
    string default_usr("root");
    string default_pwd("");
    unsigned int default_port = 3306;

    string host, usr, pwd;
    unsigned int port;

    try {
        host = m_params.get<string>("host");
    } catch(...) {
        host = default_host;
        HKU_WARN("Can't get mysql host! " << func_name);
    }

    try {
        port = m_params.get<int>("port");
    } catch(...) {
        port = 3306;
    }

    try {
        usr = m_params.get<string>("usr");
    } catch(...) {
        usr = default_usr;
    }

    try {
        pwd = m_params.get<string>("pwd");
    } catch(...) {
        pwd = default_pwd;
    }

    shared_ptr<MYSQL> mysql(new MYSQL, MySQLCloser());
    if (!mysql_init(mysql.get())) {
        HKU_ERROR(" Initial MySQL handle error!" << func_name);
        return false;
    }

    if (!mysql_real_connect(mysql.get(), host.c_str(), usr.c_str(),
            pwd.c_str(), NULL, port, NULL, 0) ) {
        HKU_ERROR(" Failed to connect to database!" << func_name);
        return false;
    }

    if (mysql_set_character_set(mysql.get(), "utf8")) {
        HKU_ERROR(" mysql_set_character_set error!" << func_name);
        return false;
    }

    m_mysql = mysql;

    return true;
}

string MySQLKDataDriver
::getTableName(const string& market, const string& code, KQuery::KType ktype) {
    return market + "_" + KQuery::getKTypeName(ktype) + "." + code;
}

MySQLKDataDriver::~MySQLKDataDriver() {

}


void MySQLKDataDriver::
loadKData(const string& market, const string& code,
        KQuery::KType kType, size_t start_ix, size_t end_ix,
        KRecordListPtr out_buffer) {
    string func_name(" [MySQLKDataDriver::loadKData]");
    if (!m_mysql) {
        //HKU_ERROR("Null m_mysql!" << func_name);
        return;
    }

    if (kType >= KQuery::INVALID_KTYPE || start_ix >= end_ix) {
        HKU_WARN("ktype(" << kType << ") is invalid or start_ix("
                << start_ix << ") >= endix(" << end_ix << ")" << func_name);
        return;
    }

    MYSQL_RES *result;
    MYSQL_ROW row;

    string table(getTableName(market, code, kType));
    std::stringstream buf (std::stringstream::out);
    buf << "select date, open, high, low, close, amount, count from "
            << table << " order by date limit " << start_ix
            << ", " << (end_ix - start_ix);
    int res = mysql_query(m_mysql.get(), buf.str().c_str());
    if(res) {
        //mysql表不存在也会报错
        //HKU_ERROR("mysql_query error! error no: " << res
        //        << " " << buf.str() << func_name);
        return;
    }

    result = mysql_store_result(m_mysql.get());
    if (!result) {
        HKU_ERROR("mysql_store_result error!" << func_name);
        return;
    }

    int i = 0;
    while(row = mysql_fetch_row(result)) {
        try {
            KRecord k;
            hku_uint64 d = boost::lexical_cast<hku_uint64>(row[0]);
            k.datetime = Datetime(d);
            k.openPrice = boost::lexical_cast<price_t>(row[1]);
            k.highPrice = boost::lexical_cast<price_t>(row[2]);
            k.lowPrice = boost::lexical_cast<price_t>(row[3]);
            k.closePrice = boost::lexical_cast<price_t>(row[4]);
            k.transAmount = boost::lexical_cast<price_t>(row[5]);
            k.transCount = boost::lexical_cast<price_t>(row[6]);
            out_buffer->push_back(k);
            i++;
        } catch (...) {
            HKU_INFO("Can't fecth No." << i << " record in "
                    << table << func_name);
            i++;
            continue;
        }
    }

    mysql_free_result(result);
    return;
}


size_t MySQLKDataDriver::
getCount(const string& market,
        const string& code,
        KQuery::KType kType) {
    string func_name(" [MySQLKDataDriver::getCount]");
    size_t result = 0;
    if (!m_mysql) {
        HKU_ERROR("Null m_mysql!" << func_name);
        return result;
    }

    if (kType >= KQuery::INVALID_KTYPE ) {
        HKU_WARN("ktype(" << kType << ") is invalid" << func_name);
        return result;
    }

    MYSQL_RES *mysql_result;
    MYSQL_ROW row;

    string table(getTableName(market, code, kType));
    std::stringstream buf (std::stringstream::out);
    buf << "select count(1) from " << table;
    int res = mysql_query(m_mysql.get(), buf.str().c_str());
    if(res) {
        HKU_ERROR("mysql_query error! error no: " << res
                << " " << buf.str() << func_name);
        return result;
    }

    mysql_result = mysql_store_result(m_mysql.get());
    if (!mysql_result) {
        HKU_ERROR("mysql_store_result error!" << func_name);
        return result;
    }

    while(row = mysql_fetch_row(mysql_result)) {
        try {
            result = boost::lexical_cast<size_t>(row[0]);
        } catch (...) {
            HKU_INFO("Error get record count of" << table << func_name);
            result = 0;
        }
    }

    mysql_free_result(mysql_result);
    return result;
}

bool MySQLKDataDriver::
getIndexRangeByDate(const string& market, const string& code,
        const KQuery& query, size_t& out_start, size_t& out_end) {
    string func_name(" [MySQLKDataDriver::getIndexRangeByDate]");
    out_start = 0;
    out_end = 0;
    if (query.queryType() != KQuery::DATE) {
        HKU_ERROR("queryType must be KQuery::DATE" << func_name);
        return false;
    }

    if(query.startDatetime() >= query.endDatetime()
    || query.startDatetime() > (Datetime::max)()) {
        return false;
    }

    MYSQL_RES *mysql_result;
    MYSQL_ROW row;

    string table(getTableName(market, code, query.kType()));
    std::stringstream buf (std::stringstream::out);
    buf << "select (select count(1) from " << table
        << " where date<\"" << query.startDatetime().toString()
        << "\") as rownum from " << table;
    int res = mysql_query(m_mysql.get(), buf.str().c_str());
    if(res) {
        HKU_ERROR("mysql_query error! error no: " << res
                << " " << buf.str() << func_name);
        return false;
    }

    mysql_result = mysql_store_result(m_mysql.get());
    if (!mysql_result) {
        HKU_ERROR("mysql_store_result error!" << func_name);
        return false;
    }

    while(row = mysql_fetch_row(mysql_result)) {
        try {
            out_start = boost::lexical_cast<size_t>(row[0]);
        } catch (...) {
            HKU_INFO("Error boost::lexical_cast<size_t>" << table << func_name);
            out_start = 0;
            mysql_free_result(mysql_result);
            return false;
        }
    }

    mysql_free_result(mysql_result);

    buf.clear();
    buf << "select (select count(1) from " << table
        << " where date<=\"" << query.endDatetime().toString()
        << "\") as rownum from " << table;
    res = mysql_query(m_mysql.get(), buf.str().c_str());
    if(res) {
        HKU_ERROR("mysql_query error! error no: " << res
                << " " << buf.str() << func_name);
        return false;
    }

    mysql_result = mysql_store_result(m_mysql.get());
    if (!mysql_result) {
        HKU_ERROR("mysql_store_result error!" << func_name);
        return false;
    }

    while(row = mysql_fetch_row(mysql_result)) {
        try {
            out_end = boost::lexical_cast<size_t>(row[0]) - 1;
        } catch (...) {
            HKU_INFO("Error boost::lexical_cast<size_t>" << table << func_name);
            out_end = 0;
            mysql_free_result(mysql_result);
            return false;
        }
    }

    mysql_free_result(mysql_result);
    return true;
}

KRecord MySQLKDataDriver::
getKRecord(const string& market, const string& code,
          size_t pos, KQuery::KType kType) {
    string func_name(" [MySQLKDataDriver::getKRecord]");
    KRecord result;
    if (!m_mysql) {
        HKU_ERROR("Null m_mysql!" << func_name);
        return result;
    }

    if (kType >= KQuery::INVALID_KTYPE ) {
        HKU_WARN("ktype(" << kType << ") is invalid" << func_name);
        return result;
    }

    MYSQL_RES *mysql_result;
    MYSQL_ROW row;

    string table(getTableName(market, code, kType));
    std::stringstream buf (std::stringstream::out);
    buf << "select date, open, high, low, close, amount, count from "
            << table << " limit" << pos << ", 1";
    int res = mysql_query(m_mysql.get(), buf.str().c_str());
    if(res) {
        HKU_ERROR("mysql_query error! error no: " << res << func_name);
        return result;
    }

    mysql_result = mysql_store_result(m_mysql.get());
    if (!mysql_result) {
        HKU_ERROR("mysql_store_result error!" << func_name);
        return result;
    }

    while(row = mysql_fetch_row(mysql_result)) {
        try {
            result.datetime = Datetime(row[0]);
            result.openPrice = boost::lexical_cast<price_t>(row[1]);
            result.highPrice = boost::lexical_cast<price_t>(row[2]);
            result.lowPrice = boost::lexical_cast<price_t>(row[3]);
            result.closePrice = boost::lexical_cast<price_t>(row[4]);
            result.transAmount = boost::lexical_cast<price_t>(row[5]);
            result.transCount = boost::lexical_cast<price_t>(row[6]);
        } catch (...) {
            HKU_INFO("Error get record " << pos << " " << table << func_name);
            result = Null<KRecord>();
        }
    }

    mysql_free_result(mysql_result);
    return result;
}

} /* namespace hku */
