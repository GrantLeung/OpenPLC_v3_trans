# OpenPLC_v3_trans

g++ *.cpp *.o -o openplc -I ./lib -pthread -lwsock32 -lmodbus -lwinmm -fpermissive -I ./modbus -w
