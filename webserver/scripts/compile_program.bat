@echo off



echo "Optimizing ST program..."
st_optimizer.exe ./st_files/%1 ./st_files/%1

echo "Generating C files..."
iec2c.exe ./st_files/%1

echo "Moving Files..."
move POUS.c ./core/
move POUS.h ./core/
move LOCATED_VARIABLES.h ./core/
move VARIABLES.csv ./core/
move Config0.c ./core/
move Config0.h ./core/
move Res0.c ./core/

cd core
echo "Generating glueVars..."
glue_generator.exe
echo "Compiling main program..."
g++ *.cpp *.o -o openplc -I ./lib -pthread -lwsock32 -lmodbus -lwinmm -fpermissive -I ./modbus -w
echo "Compilation finished successfully!"