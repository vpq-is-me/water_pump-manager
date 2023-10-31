#include <unistd.h>
#include <sys/socket.h>
#include <iostream>
#include <sys/un.h>
#include <sys/mman.h>

#include "up2web_comm.h"
#include "main.h"
#include "sqlite/sqlite3.h"
#include <string>
#include <math.h>  //for fpclassify(), check float for NaN
#include "time.h"
#include "ble_messaging.h"
#include <fstream>


using namespace std;

int ReceiveNewRow5DB_CB(void*dist,int col_n,char** fields,char**col_names);
int ReceiveAnswer5DB_CB(void*dist,int col_n,char** fields,char**col_names);
ofstream err_db_log_file;


#define BACKLOG 1 // how many pending connections queue will hold
///**************************************************************************************************

static int up2web_serv_socket, client_fd;

void ChildStopHandler(int signum) {
    cerr<<"child stopping"<<endl;
    close(client_fd);
    close(up2web_serv_socket);
    unlink(UP2WEB_SOCKET);
    _exit(0);
}
/********************************************//**
 * \brief
 *
 * \param void
 * \return void
 *
 ***********************************************/
void SigStopHandlerPreset(void) {
    struct sigaction sig;
    sig.sa_handler=ChildStopHandler;
    sigemptyset(&sig.sa_mask);
    sig.sa_flags=0;
    sigaction(SIGINT,&sig,NULL);//user press CTRL+C
}


///*******************************************************************************************************************
void Up2webCommPrc(void) {//chiled process
    SigStopHandlerPreset();
    unlink(UP2WEB_SOCKET);// unbind if previously not properly terminated
    /**< create socket */
    if((up2web_serv_socket=socket(AF_LOCAL,SOCK_STREAM,0))<0) { //UNIX type of socket
        cerr<<"socket not achieved"<<endl;
        _exit(1);
    }
    /**< prepare address structure to bind to socket */
    struct sockaddr_un sock_addr;
    memset(&sock_addr,0,sizeof(struct sockaddr_un));
    sock_addr.sun_family=AF_LOCAL;
    strncpy(sock_addr.sun_path, UP2WEB_SOCKET, sizeof(sock_addr.sun_path)-1);
    sock_addr.sun_path[sizeof(sock_addr.sun_path)-1]='\0';
    /**< bind socket to address and create listening queue */
    size_t sock_addr_length=offsetof(struct sockaddr_un, sun_path)+strlen(sock_addr.sun_path);
    if(bind(up2web_serv_socket, (struct sockaddr*)&sock_addr, sock_addr_length)<0) {
        cerr<<"bind error"<<endl;
        _exit(1);
    }
    if(listen(up2web_serv_socket,BACKLOG)<0) {
        cerr<<"listening error"<<endl;
        _exit(1);
    }

    //now socket is entirely prepared and can start waiting for client connection.
    //In oppose to general approaches with:
    //           (1) forking new process for each new connection or
    //           (2) using 'select()' function for several connection
    //we assume that ONLY one upper (web) client will connect to this program
    //that is why we not actually provide multiprocessing in this socket execution
    int len;
    char buffer[MAX_WEB_BUFFER_LENGTH];
    json_t *root;
    json_error_t jerror;
    while(1) {
        client_fd=accept(up2web_serv_socket,NULL,NULL);
        if(client_fd<0) {
            cerr<<"error in accepting new web client"<<endl;
            continue;
        }
        while(1) {
            len=read(client_fd,buffer,MAX_WEB_BUFFER_LENGTH);
            if(len<0) {
                cerr<<"Error reading from web socket"<<endl;
                //???have to close socket??
            } else if(!len) { //client disconnected
                cerr<<"WEB bro disconnected"<<endl;
                close(client_fd);
                break;//go to outer while(1) waiting new connection
            } else { //new message arived
                cerr<<endl<<"New message from WEB bro with len:"<<len<<endl;
                buffer[len]='\0';
                cerr<<buffer<<endl;
                root=json_loadb(buffer,len,0,&jerror);
                if(!root) {
                    cerr<<"JOSN error in line: "<<jerror.line<<"->"<<jerror.text<<endl;
                    json_object_clear(root);
                    continue;
                }
                if(!json_is_object(root)) {
                    cerr<<"JOSN: received not Object!"<<endl;
                    json_object_clear(root);
                    continue;
                }
                int tag,len;
                tag=json_integer_value(json_object_get(root,"tag"));
                switch(tag) {
                case TAG_REQSNAP:
                    len=up2web.Serialize(buffer);
                    cout<<"send current data snap with length:"<<len<<endl;
                    write(client_fd,buffer,len);
                    break;
                case TAG_CMDRESETALARM: {
                    uint8_t data_arr[5]= {WP_PARAM_I_ALARMS,0,0,0,0}; //send distanation command 'tag' and command contents 4 bytes length
                    int er=BLE_send(WATER_PUMP_OP_SET_PARAM,5,data_arr);
                    if(er)cout<<"BLE_send() error="<<er<<endl;
                    cout<<"Received 'Reset Alarm command'"<<endl;
                    break;
                }
                case TAG_DB_REQUEST:{
                    char *answer=nullptr;
                    if(up2web.DB_ServeTableRequest(root,&answer)){
                        cout<<"Received unsupported request"<<endl;
                        const char err_answer[]=R"({"tag":3,"error":"error"})";
                        write(client_fd,err_answer,strlen(err_answer));
                        DEBUG(cout<<endl<<"Answer:"<<endl<<err_answer<<endl;)
//                      json_object_clear(root);???
                        break;
                    }
                    if(answer!=NULL){
                        DEBUG(cout<<endl<<"answer:"<<endl<<answer<<endl;)
                        write(client_fd,answer,strlen(answer));
                        free(answer);
                    }
//                    json_object_clear(root);???
                    break;}
                case TAG_DB_ALARMS:{
                    char *answer=nullptr;
                    if(up2web.DB_ServeAlarmsRequest(root,&answer)){
                        DEBUG(cout<<"Received unsupported request"<<endl;)
                        const char err_answer[]=R"({"tag":4,"error":"error"})";
                        write(client_fd,err_answer,strlen(err_answer));
                        DEBUG(cout<<endl<<"Answer:"<<endl<<err_answer<<endl;)
//                      json_object_clear(root);???
                        break;
                    }
                    if(answer!=NULL){
                        DEBUG(cout<<endl<<"answer:"<<endl<<answer<<endl;)
                        write(client_fd,answer,strlen(answer));
                        free(answer);
                    }
//                    json_object_clear(root);???
                    break;}
                default:
                    cout<<"Received unknown tag No:"<<tag<<endl;
                    break;
                }
            }
        }//END while(1){... reading from connected upper client
    }//END while(1){...  trying wait connection new client
    _exit(0);
}
///*******************************************************************************************************************
/**< communication child <-> parent processes block */
void tUp2Web_cl::InitMemory(void) {
    smem=(tShared_mem_st*)mmap(NULL,sizeof(tShared_mem_st),PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1,0);
    smem->acc_counter=-1;
    smem->pump_capacity=-1.0;
    smem->exp_tank_vol=-1.0;
    smem->alarms=0;
}
tUp2Web_cl::~tUp2Web_cl(void) {
    munmap(smem,sizeof(tShared_mem_st));
    sqlite3_close(log_db);
    err_db_log_file.close();
}

void tUp2Web_cl::UpdateAccumulator(uint32_t val) {
    std::lock_guard<std::mutex> lg(smem->pump_mutex);
    /**< If gathering of new row already initiated (probably change in alarm value) just confirm of counter was updated*/
    if(db_new_row_fgs!=NEW_EMPTY)db_new_row_fgs=(new_row_fgs_en)((int)db_new_row_fgs|(int)NEW_COUNTER);
    /**< Initiate adding new row to DB if new counter value arrived */
    else if(smem->acc_counter!=(int32_t)val)db_new_row_fgs=NEW_COUNTER;
    smem->acc_counter=val;
}
void tUp2Web_cl::UpdatePumpCapacity(float val) {
    std::lock_guard<std::mutex> lg(smem->pump_mutex);
    if(fpclassify (val) != FP_NORMAL)val=0;
    if(db_new_row_fgs!=NEW_EMPTY)db_new_row_fgs=(new_row_fgs_en)((int)db_new_row_fgs|(int)NEW_CAPACITY);
    smem->pump_capacity=val;
}
void tUp2Web_cl::UpdatePumpCapacityAvg(float val) {
    std::lock_guard<std::mutex> lg(smem->pump_mutex);
    if(fpclassify (val) != FP_NORMAL)val=0;
    if(db_new_row_fgs!=NEW_EMPTY)db_new_row_fgs=(new_row_fgs_en)((int)db_new_row_fgs | (int)NEW_CAPACITY_AVG);
    smem->pump_capacity_avg=val;
}
void tUp2Web_cl::UpdateTankVolume(float val) {
    std::lock_guard<std::mutex> lg(smem->pump_mutex);
    if(fpclassify (val) != FP_NORMAL)val=0;
    if(db_new_row_fgs!=NEW_EMPTY)db_new_row_fgs=(new_row_fgs_en)((int)db_new_row_fgs|(int)NEW_VOLUME);
    smem->exp_tank_vol=val;
}
void tUp2Web_cl::UpdateAlarms(uint32_t val) {
    std::lock_guard<std::mutex> lg(smem->pump_mutex);
    /**< If gathering of new row already initiated (probably change in counter value) just confirm of alarm was updated*/
    if(db_new_row_fgs!=NEW_EMPTY)db_new_row_fgs=(new_row_fgs_en)((int)db_new_row_fgs|(int)NEW_ALARM);
    /**< Initiate adding new row to DB if new counter value arrived */
    else if(smem->alarms!=(uint32_t)val)db_new_row_fgs=NEW_ALARM;
    smem->alarms=val;
    time(&smem->last_time);
}
void tUp2Web_cl::UpdatePPtimeouts(int16_t val[4]) {
    std::lock_guard<std::mutex> lg(smem->pump_mutex);
    if(db_new_row_fgs!=NEW_EMPTY)db_new_row_fgs=(new_row_fgs_en)((int)db_new_row_fgs|(int)NEW_TIMEOUTS);
    for(int i=0; i<4; i++)
        smem->pp_timeouts[i]=val[i];

}

///*******************************************************************************************************************
int tUp2Web_cl::Serialize(char*buf) {
    int len;
    struct tm *time_st;
    char time_str_buf[32];
    std::lock_guard<std::mutex> lg(smem->pump_mutex);
    time_st=localtime(&smem->last_time);
    strftime(time_str_buf,sizeof(time_str_buf),"%T",time_st);

    json_t *root;
 //   json_error_t jerror;
    root=json_object();
    json_object_set_new(root,"WF_counter",json_integer(smem->acc_counter));
    json_object_set_new(root,"PP_capacity",json_real(smem->pump_capacity));
    json_object_set_new(root,"PP_capacity_avg",json_real(smem->pump_capacity_avg));
    json_object_set_new(root,"TK_volume",json_real(smem->exp_tank_vol));
    json_object_set_new(root,"alarms",json_integer(smem->alarms));
    json_object_set_new(root,"last_time",json_string(time_str_buf));
    json_object_set_new(root,"PP_ON_ACC",json_real((float)smem->pp_timeouts[PP_ON_ACC]/100));
    json_object_set_new(root,"PP_RUN_MAX",json_real((float)smem->pp_timeouts[PP_RUN_MAX]/100));
    json_object_set_new(root,"PP_RUN_MIN",json_real((float)smem->pp_timeouts[PP_RUN_MIN]/100));
    json_object_set_new(root,"PP_RUN_AT_PULSE",json_real((float)smem->pp_timeouts[PP_RUN_AT_PULSE]/100));
    len=json_dumpb(root,buf,MAX_WEB_BUFFER_LENGTH,JSON_REAL_PRECISION(3));
    json_decref(root);
    return len;
}

/**< This vector used later to check column presence in database
drawback of this - we have to check that same name declared here and at the moment of DB creation*/
#warning TODO (vpq@list.ru#1#): 3 time declared same columns of database
const vector <string> tUp2Web_cl::DB_columns_arr={"id","date","WF_counter","PP_capacity","PP_capacity_avg","TK_volume","PP_ON_ACC","PP_RUN_MAX","PP_RUN_MIN","PP_RUN_AT_PULSE","alarms"};

void tUp2Web_cl::DB_Open(void) {
    int res;
    char *zErrMsg;
    err_db_log_file.open("error_db_log");
    err_db_log_file<<"header"<<endl;
    res = sqlite3_open("log.db", &log_db);
    if(res!=SQLITE_OK)
        err_db_log_file<<"error opening DB with err:"<<res<<endl;
    const char*sql ="SELECT * FROM logtable LIMIT 1";
    res = sqlite3_exec(log_db, sql, 0, 0, &zErrMsg);/* Create SQL statement */
    if( res != SQLITE_OK ) {
        cerr<<"error to SELECT at opening with error:"<<zErrMsg<<endl;
        err_db_log_file<<"error to SELECT at opening with error:"<<zErrMsg<<endl;
        if(strncmp(zErrMsg,"no such table:",14)==0) {
            sqlite3_free(zErrMsg);
            sql="CREATE TABLE logtable(" \
                "id INTEGER PRIMARY KEY," \
                "date INTEGER NOT NULL," \
                "WF_counter INTEGER," \
                "PP_capacity REAL," \
                "PP_capacity_avg REAL," \
                "TK_volume REAL,"\
                "PP_ON_ACC REAL,"\
                "PP_RUN_MAX REAL,"\
                "PP_RUN_MIN REAL,"\
                "PP_RUN_AT_PULSE REAL,"\
                "alarms INTEGER);";
            res = sqlite3_exec(log_db, sql, 0, 0, &zErrMsg);/* Create SQL statement */
            cerr<<"Create DB table with result:"<<zErrMsg<<endl;
        }
        sqlite3_free(zErrMsg);
    }
}
///*******************************************************************************************************************
#define MAXSQLLENGTH 1024
int tUp2Web_cl::DB_AddRow(void) {
    int res;
    char *zErrMsg;
    if(db_new_row_fgs!=NEW_FULL_ROW_MSK)return 0;//new row not fully prepared
    db_new_row_fgs=NEW_EMPTY;
    char sql[MAXSQLLENGTH];
    {
        std::lock_guard<std::mutex> lg(smem->pump_mutex);
        snprintf(sql,MAXSQLLENGTH,"INSERT INTO logtable (date,WF_counter,PP_capacity,PP_capacity_avg,TK_volume,PP_ON_ACC,PP_RUN_MAX,PP_RUN_MIN,PP_RUN_AT_PULSE,alarms) " \
                 "VALUES (unixepoch('now','localtime'),%d,%f,%f,%f,%f,%f,%f,%f,%d);",smem->acc_counter,smem->pump_capacity,smem->pump_capacity_avg,smem->exp_tank_vol,
                 (float)smem->pp_timeouts[PP_ON_ACC]/100,(float)smem->pp_timeouts[PP_RUN_MAX]/100,(float)smem->pp_timeouts[PP_RUN_MIN]/100,(float)smem->pp_timeouts[PP_RUN_AT_PULSE]/100,smem->alarms);
    }
    /* Execute SQL statement */
    res = sqlite3_exec(log_db, sql, 0, 0, &zErrMsg);
    if( res != SQLITE_OK ) {
        cout<<endl<<zErrMsg<<endl;
        err_db_log_file<<"error to INSERT new row to DB:"<<zErrMsg<<endl;
        sqlite3_free(zErrMsg);
        cerr<<"error to INSERT new row to DB"<<endl;
        return 1;
    }
    /**< Check if number of row in database exceed predefined limit */
    snprintf(sql,MAXSQLLENGTH,"SELECT count(*) FROM logtable");
    int64_t row_count;
    res = sqlite3_exec(log_db, sql, &ReceiveAnswer5DB_CB, (void*)&row_count, &zErrMsg);
    if( res != SQLITE_OK ) {
        sqlite3_free(zErrMsg);
        cerr<<"error to find row count in DB"<<endl;
        return 2;
    }
    if (row_count<(MAXLOGROWNUMS+MAXLOG_TRUNCATE_THRESHOLD))return 0;
    /**< Delete band of most older rows */
    snprintf(sql,MAXSQLLENGTH,"SELECT min(id) FROM logtable");
    int64_t min_id;
    res = sqlite3_exec(log_db, sql, &ReceiveAnswer5DB_CB, (void*)&min_id, &zErrMsg);
    if( res != SQLITE_OK ) {
        sqlite3_free(zErrMsg);
        cerr<<"error to find min ID in DB"<<endl;
        return 3;
    }
    snprintf(sql,MAXSQLLENGTH,"DELETE FROM logtable WHERE id<%lld",(min_id+MAXLOG_TRUNCATE_THRESHOLD));
    res = sqlite3_exec(log_db, sql, 0, 0, &zErrMsg);
    if( res != SQLITE_OK ) {
        sqlite3_free(zErrMsg);
        cerr<<"error to delete old rows from DB"<<endl;
        return 4;
    }
    return 0;
}
///*******************************************************************************************************************
#define CheckRequestOk(r,str,errmsg) if(r!=SQLITE_OK){\
    err_db_log_file<<str<<errmsg<<endl; \
    sqlite3_free(errmsg); \
    cerr<<str<<endl;\
    return -2; \
}
unsigned int constexpr str_hash(char const* str,int i=0) {
    return !str[i] ? 5381 : (str_hash(str,i+1)*33)^str[i];
}
unsigned int str_hash_vol(char const*str,int max_len=256) {
    unsigned int res=5381;
    int i;
    for(i=0; i<max_len; i++)
        if(!str[i])break;
    while(i) {
        res=(res*33)^str[--i];
    }
    return res;
}
///*******************************************************************************************************************
int tUp2Web_cl::DBreqGetAmount(json_t*root) {
    int res=0;
    if((res=json_integer_value(json_object_get(root,"amount")))!=0){
        if(res>MAX_REQUESTED_ROW_AMOUNT)res=MAX_REQUESTED_ROW_AMOUNT;
        DEBUG(cout<<"amount="<<res<<endl;)
    }
    return res;
}
///*******************************************************************************************************************
int tUp2Web_cl::DBreqGetDirection(json_t *root){
    int res=0;
    char const* str_val;
    if((str_val=json_string_value(json_object_get(root,"direction")))!=NULL){
        if(!strncmp(str_val,"up",2))res=1;
        DEBUG(cout<<"direction="<<res<<endl;)
    }
    return res;
}
///*******************************************************************************************************************
int64_t tUp2Web_cl::DBreqGetBaseID(json_t*root,int& direction,int& amount){
    char const* str_val;
    string sql;
    int sql_res;
    char *zErrMsg;
    int64_t res=-1;

    if((str_val=json_string_value(json_object_get(root,"base")))==NULL)return -1;
    switch(str_hash_vol(str_val)) {
    case str_hash("last"):
        sql="SELECT max(id) FROM logtable";
        sql_res = sqlite3_exec(log_db, sql.c_str(), &ReceiveAnswer5DB_CB, (void*)&res, &zErrMsg);
        CheckRequestOk(sql_res,"error to find 'last' id for DB_ServeTableRequest:",zErrMsg);
        DEBUG(cout<<"first ID="<<res<<endl;)
        direction=0;//other direction have no sense
        break;
    case str_hash("first"):
        sql="SELECT min(id) FROM logtable";
        sql_res = sqlite3_exec(log_db, sql.c_str(), &ReceiveAnswer5DB_CB, (void*)&res, &zErrMsg);
        CheckRequestOk(sql_res,"error to find 'first' id for DB_ServeTableRequest:",zErrMsg);
        DEBUG(cout<<"first ID="<<res<<endl;)
        direction=1;//other direction have no sense
        break;
    case str_hash("id"):{
        json_t* id_obj=json_object_get(root,"id");
        if(id_obj==NULL)return -1;
        json_int_t id=json_integer_value(id_obj);
        DEBUG(cout<<"intended ID for query from DB="<<id<<endl;)
        sql="SELECT id FROM logtable WHERE id=" + std::to_string(id);
        sql_res = sqlite3_exec(log_db, sql.c_str(), &ReceiveAnswer5DB_CB, (void*)&res, &zErrMsg);
        CheckRequestOk(sql_res,"error to find requested id for DB_ServeTableRequest:",zErrMsg);
        DEBUG(cout<<" actual requested ID="<<res<<endl;)
        if(res!=id)amount=0;///**<-if we reach boundary somtimes DB gives wrong result in sequential fast request
        break;}
    case str_hash("date"):{
        json_t* date_obj=json_object_get(root,"date");//get borrowed reference, so not required to call 'json_decref()'
        if(date_obj==NULL)return -1;
        json_int_t date=json_integer_value(date_obj);
        if(direction)
            sql="SELECT min(id) FROM logtable WHERE date>=" + std::to_string(date);
        else
            sql="SELECT max(id) FROM logtable WHERE date<=" + std::to_string(date);
        sql_res = sqlite3_exec(log_db, sql.c_str(), &ReceiveAnswer5DB_CB, (void*)&res, &zErrMsg);
        CheckRequestOk(sql_res,"error to find requested id for DB_ServeTableRequest:",zErrMsg);
        DEBUG(cout<<"requested ID="<<res<<endl;)
        break;}
    case str_hash("counter"):{
        json_t* WF_counter_obj=json_object_get(root,"counter");
        if(WF_counter_obj==NULL)return -1;
        json_int_t WF_counter=json_integer_value(WF_counter_obj);
        if(direction)
            sql="SELECT min(id) FROM logtable WHERE WF_counter>=" + std::to_string(WF_counter);
        else
            sql="SELECT max(id) FROM logtable WHERE WF_counter<=" + std::to_string(WF_counter);
        sql_res = sqlite3_exec(log_db, sql.c_str(), &ReceiveAnswer5DB_CB, (void*)&res, &zErrMsg);
        CheckRequestOk(sql_res,"error to find requested id for DB_ServeTableRequest:",zErrMsg);
        DEBUG(cout<<"requested ID="<<res<<endl;)
        break;}
    }
    return res;
}
///*******************************************************************************************************************
int tUp2Web_cl::DBreqRetriveData(json_t *root,string fields,string sql,char**answ){
    int js_res;
    DEBUG(cout<<"SQL  request>>"<<sql<<"<<"<<endl;)
    char *zErrMsg;
    json_t* data_obj_arr_obj=json_object();//prepare object for answer from DB. This object will contain arrays of answer's columns.
    string::size_type b=0,e=0;
    string val_name;///!!DON'T modify this after do-while()! We will use it later to count real amount of answer data!!
    do {
        e=fields.find(",",b);
        val_name=fields.substr(b,e-b);
        val_name.erase(val_name.find_last_not_of(' ')+1); //suffixing spaces
        val_name.erase(0, val_name.find_first_not_of(' ')); //prefixing spaces
        js_res=json_object_set_new(data_obj_arr_obj,val_name.c_str(),json_array());
        b=e+1;
    } while (e!=string::npos);
    int sql_res = sqlite3_exec(log_db, sql.c_str(), &ReceiveNewRow5DB_CB, (void*)&data_obj_arr_obj, &zErrMsg);
    CheckRequestOk(sql_res,"sql error to request answer table:",zErrMsg);
    int real_amount=json_array_size(json_object_get(data_obj_arr_obj,  val_name.c_str()));
    js_res=json_object_set(root,"data",data_obj_arr_obj);
    js_res=json_object_set_new(root,"amount",json_integer(real_amount));
    DEBUG(cout<<"data row count="<<real_amount<<endl;)
    *answ=json_dumps(root,JSON_REAL_PRECISION(3)|JSON_INDENT(2));
    json_decref(data_obj_arr_obj);
    //!!! It is required??? -> json_object_clear(root);
    return 0;
}
/**<
NOTE: for request from database we have to remove all rows with same WF counter. Which occurs due to appering any alarm.
I.e. when alarm arise new row inserted to DB, but for pump performance evaluation this duplicate row are waste
From group of rows with same WF couter reading we have to select earliest row, i.e. earliest date/time or lowest id.
So despite request fields we first of all select rows with mandatory columns 'MIN(id)' and 'wf_counter and from result select only required columns
 */
int tUp2Web_cl::DB_ServeTableRequest(json_t* root,char**answ) {
    string sql;
    //**
    int dir_up1_down0=DBreqGetDirection(root);
    int row_amount=DBreqGetAmount(root);
    //NN. Pick up requested collumns. We'll prepare 2 strings (actually 'values' list in sql querry)
    //1 - for final answer, another will always consist 'WF_counter' to exclude rows wich stored in DB due to
    //occuring any alarm. By checking same value of 'WF_counter' it is possible to return only values
    //concerning water consumption/pump behaviour inself
    json_auto_t* columns_arr_obj;
    string columns_sql_str="";
    string columns_sql_str_group;
    if((columns_arr_obj=json_object_get(root,"columns"))!=NULL){
        size_t idx;
        json_t* arr_elem_obj;
        string arr_elem;
        char colon=' ';
        idx=0;
        bool WF_counter_was=false;
        while(idx<json_array_size(columns_arr_obj) && (arr_elem_obj=json_array_get(columns_arr_obj,idx))){//arr_elem_obj is 'borrowed' reference (see json library), so it is not required to call 'json_decref()'
            if(IsInColumns(arr_elem=json_string_value(arr_elem_obj))) {
                columns_sql_str+=colon;
                columns_sql_str+=arr_elem;
                if(!arr_elem.compare(0,10,"WF_counter")){//don't add it later again. See NOTE before function body
                    WF_counter_was=true;
                }
                colon=',';
                idx++;
            }else json_array_remove(columns_arr_obj,idx);//remove this unknown colunm name from array, because this array would be send back with actual memebers
        }
        columns_sql_str_group=columns_sql_str;
        if(!WF_counter_was)columns_sql_str_group+=", WF_counter";
        columns_sql_str_group+=", MIN(id)";
    }else return -2;
    if(columns_sql_str=="")return -2;
    DEBUG(cout<<"   colomns string="<<columns_sql_str<<endl;)
    //END NN
    int64_t base_id=DBreqGetBaseID(root,dir_up1_down0,row_amount);
    if(base_id<0)return base_id;
    //***
    if(dir_up1_down0)
        sql="SELECT " +columns_sql_str_group+" FROM logtable WHERE id >= "+ std::to_string(base_id) + " GROUP BY (WF_counter) ORDER BY id ASC LIMIT " + std::to_string(row_amount);
    else
        sql="SELECT " +columns_sql_str_group+" FROM logtable WHERE id <= "+ std::to_string(base_id) + " GROUP BY (WF_counter) ORDER BY id DESC LIMIT " + std::to_string(row_amount);
    sql= "SELECT " +columns_sql_str+" FROM (" + sql + ") AS desc_res ORDER BY WF_counter ASC";

    return DBreqRetriveData(root,columns_sql_str,sql,answ);
}
///*******************************************************************************************************************
int tUp2Web_cl::DB_ServeAlarmsRequest(json_t* root,char**answ) {
    string sql;
    //**
    int dir_up1_down0=DBreqGetDirection(root);
    int row_amount=DBreqGetAmount(root);

    int64_t base_id=DBreqGetBaseID(root,dir_up1_down0,row_amount);
    if(base_id<0)return base_id;

    string data_fields=              "date, WF_counter, alarms";
    string data_fields_extra="x.id, x.date, x.WF_counter, x.alarms";
    //***
    sql="WITH r AS (\
        SELECT "+data_fields_extra+ " \
        FROM logtable x, logtable y \
        ON x.id = y.id + 1 \
        AND x.alarms <> y.alarms \
        WHERE y.id IS NOT NULL AND x.id";
    sql+=(dir_up1_down0 ? ">" : "<") +std::to_string(base_id) +" ORDER BY x.id ";
    sql+=(dir_up1_down0 ? " ASC " : " DESC ");
    sql+=" LIMIT " + to_string(row_amount) + " ) \n";
    sql+= "SELECT " +data_fields+" FROM r ORDER BY id";
    //***
    return DBreqRetriveData(root,data_fields,sql,answ);
}
///*******************************************************************************************************************
tUp2Web_cl up2web;

///*******************************************************************************************************************
int ReceiveNewRow5DB_CB(void*json_ptr,int col_n,char** fields,char**col_names) {
    //if(col_n!=json_array_size(array))return 1;
    json_t* col_arr_obj;
    int js_res;
    for(auto i=0; i<col_n; i++ ) {
        col_arr_obj=json_object_get(*(json_t**)json_ptr,col_names[i]);
        js_res=json_array_append_new(col_arr_obj,json_string(fields[i]));
    }
    return 0;
}
///*******************************************************************************************************************
int ReceiveAnswer5DB_CB(void*dist,int col_n,char** fields,char**col_names) {
    if(fields[0]){
        *(int64_t*)dist=atol(fields[0]);
        return 0;
    }else return -1;
}
///*******************************************************************************************************************
