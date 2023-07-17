#include "ble_messaging.h"
#include <stdint.h>
#include "up2web_comm.h"
#include <iostream>
#include <sys/socket.h>
#include "main.h"
#include <unistd.h>
#include <mutex>
#include <sys/mman.h>

pc_transm_msg_t mesh2pc_msg;
pc_transm_msg_t pc2mesh_msg={
    .length=0,
    .msg_copy={0},//not allowed address. Wait reseiving any message from peer to catch its adderss
};
typedef struct {
    std::mutex ble_mutex;
    uint16_t PP_mesh_dst_addr;
}tBLE_shared_mem;

static tBLE_shared_mem *BLE_smem;

void BLE_Init(void){
    BLE_smem=(tBLE_shared_mem*)mmap(NULL,sizeof(tBLE_shared_mem),PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1,0);
    BLE_smem->PP_mesh_dst_addr=0;
}
void BLE_DeInit(void){
    munmap(BLE_smem,sizeof(tBLE_shared_mem));
}

static void UpdatePPmeshAddr(uint16_t addr){
    std::lock_guard<std::mutex> lg(BLE_smem->ble_mutex);
    BLE_smem->PP_mesh_dst_addr=addr;
}

void BLE_message_exec(void){
    static uint16_t dst_addr_prev=0;
    if(mesh2pc_msg.type==PRX_BLE_MESH_COPY
            && mesh2pc_msg.msg_copy.dst_addr==GROUP_WATERPUMP) {
        if(dst_addr_prev!=mesh2pc_msg.msg_copy.src_addr) {//remember address for send something back
            dst_addr_prev=mesh2pc_msg.msg_copy.src_addr;
            UpdatePPmeshAddr(dst_addr_prev);
        }
        switch(mesh2pc_msg.msg_copy.opcode){
        case WATER_PUMP_OP_STATUS_COUNTER:
            if(mesh2pc_msg.msg_copy.length==4){
                up2web.UpdateAccumulator(*(int*)mesh2pc_msg.msg_copy.msg);
            }
            break;
        case WATER_PUMP_OP_STATUS_CAPACITY:
            if(mesh2pc_msg.msg_copy.length==4){
                float dat=*(float*)mesh2pc_msg.msg_copy.msg;
                up2web.UpdatePumpCapacity(dat);
            }
            break;
        case WATER_PUMP_OP_STATUS_CAP_AVG:
            if(mesh2pc_msg.msg_copy.length==4){
                float dat=*(float*)mesh2pc_msg.msg_copy.msg;
                up2web.UpdatePumpCapacityAvg(dat);
            }
            break;
        case WATER_PUMP_OP_STATUS_TK_VOL:
            if(mesh2pc_msg.msg_copy.length==4){
                float dat=*(float*)mesh2pc_msg.msg_copy.msg;
                up2web.UpdateTankVolume(dat);
            }
            break;
        case WATER_PUMP_OP_STATUS_PP_TIMEOUTS:
            if(mesh2pc_msg.msg_copy.length==8){
                int16_t dat[4];
                for(unsigned int i=0;i<sizeof(dat);i++)
                    dat[i]=((int16_t*)mesh2pc_msg.msg_copy.msg)[i];
                up2web.UpdatePPtimeouts(dat);
            }
            break;
        case WATER_PUMP_OP_STATUS_ALARM:
            if(mesh2pc_msg.msg_copy.length==4){
                up2web.UpdateAlarms(*(uint32_t*)mesh2pc_msg.msg_copy.msg);
            }
            break;
        }//END switch(...
    }
}
int BLE_send(uint32_t opcode, uint16_t length,uint8_t*msg){
    uint16_t dst_addr=0;
    {
        std::lock_guard<std::mutex> lg(BLE_smem->ble_mutex);
        dst_addr=BLE_smem->PP_mesh_dst_addr;
    }
//pc2mesh_msg.msg_copy.dst_addr=3;//!!!
    if(dst_addr==0)return -1;//still not received any message. Distination address unknown!
    pc2mesh_msg.msg_copy.dst_addr=dst_addr;
    if(opcode!=WATER_PUMP_OP_SET_PARAM && opcode!=WATER_PUMP_OP_GET_PARAM)return -2;
    if(length>PC2BLE_MAX_LENGTH)return -3;
    pc2mesh_msg.type=PRX_BLE_MESH_COPY;
    pc2mesh_msg.msg_copy.length=length;
    pc2mesh_msg.msg_copy.opcode=opcode;
    pc2mesh_msg.msg_copy.src_addr=0;//does not matter
    for(auto i=0;i<length;i++)
        pc2mesh_msg.msg_copy.msg[i]=msg[i];
    pc2mesh_msg.msg_copy.length=length;
    length+=offsetof(pc_transm_msg_t,msg_copy.msg[0])-offsetof(pc_transm_msg_t,raw_arr[0]);
    pc2mesh_msg.length=length;
    length+=sizeof(pc2mesh_msg.length);
    write(sock_2ble_client,pc2mesh_msg.raw_arr,pc2mesh_msg.length);
    return 0;
}


