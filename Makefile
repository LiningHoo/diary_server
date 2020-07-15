main: main.cpp
	g++ -std=c++17 main.cpp -o start_server -pthread -D ORMPP_ENABLE_MYSQL -I "./include" -L "./libs" -lssl -lcrypto -ldl -lmysqlclient -lgmpxx -lgmp

clean: 
	if [ -e start_server ]; then rm start_server; fi
