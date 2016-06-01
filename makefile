all: myscan

myscan: myscan.cpp
	g++ -g -std=c++11 myscan.cpp -lpthread -lboost_system -o myscan

clean:
	rm myscan
	
