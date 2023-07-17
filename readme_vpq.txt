            The Boost C++ Libraries were successfully built!
            The following directory should be added to compiler include paths:
                /media/vladimir/Work/Distributives/CodeBlocks/libraries/boost_1_78_0
            The following directory should be added to linker library paths:
                /media/vladimir/Work/Distributives/CodeBlocks/libraries/boost_1_78_0/stage/lib

or to somwhere were I copy folder (in my case /usr/include/boost_1_78_0)
{
and add: -lboost_json 
to
Project building options -> linker settings -> Other linker options

or (it is the same result because  "lib" in libboost became "l" in lboost
add path /usr/include/boost_1_78_0/stage/lib/libboost_json.a 
to Project building options -> linker settings -> link libraries
}

if during execution there is error:
./WaterPumpManager: error while loading shared libraries: libboost_json.so.1.78.0: cannot open shared object file: No such file or directory


do:
sudo ldconfig /usr/include/boost_1_78_0/stage/lib
(with path to *.so*)


**********************************8
for using jansson.h not forget install jansson library. 
e.i. Dowload jansson progect, and install. For futher info read README in janson library. 
And not forget add -ljansson flag in project build properties in linker other libraries
**********************************8
for test connection TO this application use netcat (nc):
$ netcat -U /tmp/water_pump_socket.socket
then in prompt send:
{"tag":1} 

            TAG_REQSNAP        =1,
            TAG_CMDRESETALARM  =2,
            TAG_DB_REQUEST     =3
**********************************8
request for data
 {"tag":3,
 "base": "last"/"first"/"id"/"date"/"counter",
 "id":123  /   "date":1683338520 / "counter":1200, <-(for "last" and "first" this row is empty)
 ["direction":"up"/"down"],
 "amount":100,
 "columns":["date","WF_counter","PP_capacity_avg"] 
 }
 {"tag":3,"base":"date","date":1681596447,"direction":"down","amount":5,"columns":["id","date","WF_counter"]} 
 {"tag":3,"base":"date","date":1681596447,"direction":"down","amount":5,"columns":["id","pussy","date","WF_counter","huj"]}
{"tag":3,"base":"date","date":1681596447,"direction":"down","amount":75,"columns":["date","WF_counter"],"data":[[1681596447,15000],[1681596447,15000],...[1681596447,15000]]} 
