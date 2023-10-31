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
e.i. Dowload jansson project, and install. For futher info read README in janson library. 
And not forget add -ljansson flag in project build properties in linker other libraries
**********************************8
for succesful compiling of sqlite3 add link library 
pthread
dl
to project build properties ->link libraries
//**********************************************************************
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
 
 {"tag":3,"base":"last","direction":"down","amount":5,"columns":["id","date","WF_counter"]}
 {"tag":3,"base":"last","direction":"down","amount":5,"columns":["date"]}
 {"tag":3,"base":"last","direction":"down","amount":5,"columns":["date","WF_counter"]}
 {"tag":3,"base":"last","direction":"down","amount":5,"columns":["WF_counter"]}
 {"tag":3,"base":"last","direction":"down","columns":["WF_counter"]}
 
 {"tag":3,"base":"date","date":1681596447,"direction":"down","amount":5,"columns":["id","date","WF_counter"]} 
 {"tag":3,"base":"date","date":1681596447,"direction":"down","amount":5,"columns":["id","pussy","date","WF_counter","huj"]}
 answer:
{"tag":3,"base":"date","date":1681596447,"direction":"down","amount":75,"columns":["date","WF_counter"],"data":{"date":[1681596447,1681596448,...,1681596478],"WF_counter":[15000,15001,...,15065]}} 
**--------------
SELECT id, date 
FROM (
      SELECT id, date,WF_counter,MIN(id) 
            FROM logtable WHERE id <= 1234 
            GROUP BY (WF_counter) 
            ORDER BY id DESC 
            LIMIT 50
      ) AS desc_res 
ORDER BY WF_counter ASC

**********************************8
**********************************8
test query only strings, where 'alarms' field changed.
way 1:
----
WITH x AS (
  SELECT id,date, WF_counter,alarms                           
  FROM logtable
)
SELECT x.id, x.date, x.WF_counter, x.alarms
FROM x LEFT OUTER JOIN x AS y
ON x.id = y.id + 1
AND x.alarms <> y.alarms
WHERE y.id IS NOT NULL;
------
way 2: 
------
SELECT x.id, x.date, x.WF_counter, x.alarms
FROM logtable x, logtable y
ON x.id = y.id + 1
AND x.alarms <> y.alarms
WHERE y.id IS NOT NULL;
-----
results of way 1 and 2 equal and is from entire table
bellow we will use modified way 2 with selfjoining
-------------------
-------------------
query for limited amount of rows from above will be as bellow. 
There are 2 requests from just above some point and bellow some point
(it will depends upon "direction" field in json request)
---
WITH r AS (
      SELECT x.id, x.date, x.WF_counter, x.alarms
      FROM logtable x, logtable y
      ON x.id = y.id + 1
      AND x.alarms <> y.alarms
      WHERE y.id IS NOT NULL AND x.id > 1092
      ORDER BY x.id
      LIMIT 5)
SELECT date, WF_counter, alarms 
FROM r
ORDER BY r.id;
-------------
WITH r AS (
      SELECT x.id, x.date, x.WF_counter, x.alarms
      FROM logtable x, logtable y
      ON x.id = y.id + 1
      AND x.alarms <> y.alarms
      WHERE y.id IS NOT NULL AND x.id < 1095
      ORDER BY x.id DESC
      LIMIT 5)
SELECT date, WF_counter, alarms 
FROM r
ORDER BY id;
*****************
request for alarms 
 {
 "tag":4,
 "base": "last"/"first"/"id"/"date"/"counter",
 "id":123  /   "date":1683338520 / "counter":1200, <-(for "last" and "first" this row is empty)
 ["direction":"up"/"down"],
 "amount":100,
 }

example 
{"tag":4,"base":"last","direction":"down","amount":5}
answer
{"tag":4,"base":"date","date":1681596447,"direction":"down","amount":75,"data":{"date":[1681596447,1681596448,...,1681596478],"WF_counter":[15000,15001,...,15065], "alarms":[0,1024,...32]}} 


