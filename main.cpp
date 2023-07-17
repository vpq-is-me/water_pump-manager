#include <iostream>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <string>
#include <errno.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/wait.h>
#include "up2web_comm.h"
#include <mutex>
#include "ble_messaging.h"

#include <fstream>
#include "main.h"
using namespace std;

#define UART_SOCKET "/tmp/uart_socket.socket"
#define ERROR_LOG_FILE

int sock_2ble_client;

ofstream elog_file;
int error_logged=0;

void bridge_handler(int signum){
    int len=read(sock_2ble_client,mesh2pc_msg.raw_arr,255);
    if(!len)cout<<"socket disconnected"<<endl;//exit(1);//socket disconnected
    if(len==-1 && error_logged<20){
        elog_file<<"read error:"<<errno<<endl;
        elog_file<<"->>"<<strerror(errno)<<"<<-"<<endl;
    }
    cout<<"server send me "<<len<<"bytes-->";
    for(int i=0;i<len;i++)
        printf(" %02x",mesh2pc_msg.raw_arr[i]);
    cout<<endl;
    BLE_message_exec();
    up2web.DB_AddRow();
    alarm(20);// rearm timeout
}

/********************************************//**
 * \brief Callback function, used for kernel signal
 * handling
 * \param signum int - actual signal number
 * \return void
 *
 ***********************************************/
volatile sig_atomic_t stop_flg=0;
void SigStopHandler(int signum){
    if(signum==SIGTSTP)printf("stop program by CTRL+Z\r\r");
    else if(signum==SIGALRM){
        if(error_logged<20){
            sockaddr saddr;
            socklen_t length=sizeof(sockaddr);
            error_logged++;
            getpeername(sock_2ble_client,&saddr,&length);
            elog_file<<"peer name:"<<saddr.sa_data<<endl;
        }
        printf("Timer expiration\r\n");
        bridge_handler(0);//simply try read
        return;
    }
    else /**< SIGINT */printf("user quit program by CTRL+C\r\n");
    elog_file<<"signum:"<<signum<<endl;
    stop_flg=1;
}
/********************************************//**
 * \brief
 *
 * \param void
 * \return void
 *
 ***********************************************/
void SigPreset(void){
    struct sigaction sig;
    sig.sa_handler=SigStopHandler;
    sigemptyset(&sig.sa_mask);
    sig.sa_flags=0;
    sig.sa_flags=SA_RESTART;

    sigaction(SIGTSTP,&sig,NULL);//user press CTRL+Z
    sigaction(SIGALRM,&sig,NULL);//expiration of timer
    sigaction(SIGINT,&sig,NULL);//user press CTRL+C
}


int main() {
    up2web.InitMemory();
    up2web.DB_Open();
    BLE_Init();
    elog_file.open("error_log");
    elog_file<<"header"<<endl;
    //******socket from BLE MESH bridge preparation****
    if((sock_2ble_client=socket(AF_LOCAL,SOCK_STREAM,0))<0) {
        cerr<<"socket create"<<endl;
        exit(1);
    }
    sockaddr_un sock_addr;
    memset(&sock_addr,0,sizeof(sockaddr_un));
    sock_addr.sun_family=AF_LOCAL;
    strncpy(sock_addr.sun_path,UART_SOCKET,sizeof(sock_addr.sun_path)-1);
    sock_addr.sun_path[sizeof(sock_addr.sun_path)-1]='\0';
#ifndef DEBUG_ALONE
    if(connect(sock_2ble_client,(sockaddr*)&sock_addr,offsetof(sockaddr_un, sun_path)+strlen(sock_addr.sun_path))==-1) {
        cerr<<"connect error with code:"<<errno<<endl;
        cerr<<strerror(errno)<<endl;
        exit(1);
    }
#else
#warning This is debug version! Don`t forger comment DEBUG_ALONE
#endif
#warning TODO (vladimir#1#): Add opened socket checking and try to reconnect
    /**< fork processes after creating socket because now it can be opereated from both branch*/
    pid_t pid=fork();
    if(pid<0) {
        cerr<<"Fork new process error!"<<endl;
        return 1;
    }
    if(pid==0) { //child process spawn
        Up2webCommPrc();
        _exit(0);/**< exit and notify parent  */
        goto END_LBL;
    }
    /**< else{ <- instead using else clause in child spawn brunch we use 'goto END_LBL'  */
    SigPreset();

    /**< make socket to work with signals (asynchronous) */
    fcntl(sock_2ble_client,F_SETOWN,getpid());
    struct sigaction sig;
    sig.sa_handler=bridge_handler;
    sigemptyset(&sig.sa_mask);
    sig.sa_flags=0;
    sigaction(SIGIO,&sig,NULL);
    /**< asynchronous input mode. If set, then SIGIO signals will be generated when input from socet is available */
    int prev_flags;
    prev_flags=fcntl(sock_2ble_client,F_GETFL,0);
    prev_flags|=O_ASYNC;
    fcntl(sock_2ble_client,F_SETFL,prev_flags);
    //******END socket preparation****
    char message[256];
#ifndef DEBUG_ALONE
    alarm(20);// check why no message from dispather
#else
#warning This is debug version! Don`t forger comment DEBUG_ALONE
#endif
    while(!stop_flg) {
        cout<<"TODO enter message  >";
        if(fgets(message,255,stdin)==NULL) {//now it is for nothing taking input from user
            continue;
        }
        uint8_t payload[5]={WP_PARAM_I_ALARMS,0,0,0,0};//this leave here for test
        BLE_send(WATER_PUMP_OP_SET_PARAM,5,payload);
    }
    elog_file<<"close socket"<<endl;
    close(sock_2ble_client);
    wait(NULL);/**< wait until all child process terminated */
    cout<<"Program terminated. Good by!"<<endl;
    elog_file.close();
    BLE_DeInit();
    exit(0);
END_LBL: /**< all child process clauses go here  */
    return 0;
}
