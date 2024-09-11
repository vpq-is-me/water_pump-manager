#ifndef UP2WEB_COMM_H_INCLUDED
#define UP2WEB_COMM_H_INCLUDED

#include <vector>
#include <algorithm>
#include <mutex>
#include "sqlite/sqlite3.h"
#include "time.h"
#include "jansson.h"


#define MAX_REQUESTED_ROW_AMOUNT 1000
#define DEFAULT_REQUESTED_ROW_AMOUNT 100
void Up2webCommPrc(void);

#define MAX_WEB_BUFFER_LENGTH 8192
#define MAXLOGROWNUMS 1000000l //maximum size of database, magic number
#define MAXLOG_TRUNCATE_THRESHOLD 100

#define UP2WEB_SOCKET "/tmp/water_pump_socket.socket"


typedef enum{PP_ON_ACC,PP_RUN_MAX,PP_RUN_MIN,PP_RUN_AT_PULSE}touts_arr_en;
typedef struct{
    std::mutex pump_mutex;
    int acc_counter;
    float pump_capacity;
    float pump_capacity_avg;
    float exp_tank_vol;
    uint32_t alarms;
    int16_t pp_timeouts[4];
    time_t last_time;
}tShared_mem_st;


class tUp2Web_cl{
public:
    ~tUp2Web_cl();
    enum new_row_fgs_en{NEW_EMPTY=0,NEW_COUNTER=(0x0001<<0),NEW_CAPACITY=(0x0001<<1),NEW_VOLUME=(0x0001<<2),NEW_ALARM=(0x0001<<3),NEW_CAPACITY_AVG=(0X0001<<4),NEW_TIMEOUTS=(0X0001<<5),NEW_FULL_ROW_MSK=0x0003f};
    void InitMemory(void);

    void UpdateAccumulator(uint32_t val);
    void UpdatePumpCapacity(float val);
    void UpdatePumpCapacityAvg(float val);
    void UpdateTankVolume(float val);
    void UpdateAlarms(uint32_t val);
    void UpdatePPtimeouts(int16_t val[4]);

    int Serialize(char*buf);
    void DB_Open(void);
    int DB_AddRow(void);
    int DB_ServeTableRequest(json_t*root,char**answ);
    int DB_ServeAlarmsRequest(json_t*root,char**answ);
private:
    tShared_mem_st* smem;
    sqlite3* log_db=nullptr;
    new_row_fgs_en db_new_row_fgs=NEW_EMPTY;//we use this set of flags to check when all data from hardware received and refreshed. Starting always from counter/alarm value. Only when new data value from counter/alarm received we start checking others
    static const std::vector <std::string> DB_columns_arr;//store array of columns name of database
    bool IsInColumns(const std::string str){
        return std::find(DB_columns_arr.begin(),DB_columns_arr.end(),str)!=DB_columns_arr.end();
    }
    int DBreqGetAmount(json_t*root);
    int DBreqGetDirection(json_t*root);
    uint32_t DBreqGetAlarmFilter(json_t*root);
    int64_t DBreqGetBaseID(json_t*root,int& direction,int& amount);
    int DBreqRetriveData(json_t *root,std::string fields,std::string sql,char**answ);
};
enum tag_en{TAG_REQSNAP        =1,
            TAG_CMDRESETALARM  =2,
            TAG_DB_REQUEST     =3,
            TAG_DB_ALARMS      =4
            };
extern tUp2Web_cl up2web;






#endif // UP2WEB_COMM_H_INCLUDED
