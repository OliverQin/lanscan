//-march=corei7-avx -O3

#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <netdb.h>
#include <stdlib.h>
//#include <string.h>
#include <unistd.h>

#include <vector>
#include <thread>
#include <mutex>
#include <chrono>
#include <string>
#include <iostream>
#include <fstream>
#include <cstdlib>
#include <atomic>      

#include <boost/asio/connect.hpp>
#include <boost/asio/deadline_timer.hpp>
#include <boost/asio/io_service.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/read_until.hpp>
#include <boost/asio/streambuf.hpp>
#include <boost/system/system_error.hpp>
#include <boost/asio/write.hpp>
#include <boost/lambda/bind.hpp>
#include <boost/lambda/lambda.hpp>

using boost::asio::deadline_timer;
using boost::asio::ip::tcp;
using boost::lambda::bind;
using boost::lambda::var;
using boost::lambda::_1;

const int StatusBlocking = -1;
const int StatusSuccess = 0;
const int StatusFail = 1;

class client
{
public:
  client()
    : socket_(io_service_),
      deadline_(io_service_)
  {
    // No deadline is required until the first socket operation is started. We
    // set the deadline to positive infinity so that the actor takes no action
    // until a specific deadline is set.
    deadline_.expires_at(boost::posix_time::pos_infin);

    // Start the persistent actor that checks for deadline expiry.
    check_deadline();
    
    isRunning = false;
  }

  int connect_assyn(const std::string& host, const std::string& service,
      boost::posix_time::time_duration timeout)
  {
    try {
        lastIP = host;
        lastPort = service;
        //std::cerr << "conn... " << lastIP << std::endl;

        tcp::resolver::query query(host, service);
        tcp::resolver::iterator iter = tcp::resolver(io_service_).resolve(query);

        deadline_.expires_from_now(timeout);

        this ->ec = boost::asio::error::would_block;
        
        isRunning = true;
        boost::asio::async_connect(socket_, iter, var(this ->ec) = _1);
        
        return 0;
    }
    catch (std::exception& e) {
        std::cerr << e.what() << std::endl;
        isRunning = false;
        return -1;
    }   
  }
  
  int status() {
    //std::cerr << "once " << lastIP << std::endl;

    try {
        io_service_.run_one(); 
        //std::this_thread::sleep_for( std::chrono::microseconds( 1 ) );  
        //io_service_.poll_one(); 
        
        if (ec == boost::asio::error::would_block)
            return StatusBlocking;


        if (ec || !socket_.is_open()) {
            isRunning = false;
            return StatusFail;
        }
        else {
            isRunning = false;
            boost::system::error_code ignored_ec;
            socket_.close(ignored_ec);
            return StatusSuccess;
        }
    }
    catch (std::exception& e) {
        std::cerr << e.what() << std::endl;
        return StatusFail;
    }   
  }


 private:
  void check_deadline()
  {
    // Check whether the deadline has passed. We compare the deadline against
    // the current time since a new asynchronous operation may have moved the
    // deadline before this actor had a chance to run.
    if (deadline_.expires_at() <= deadline_timer::traits_type::now())
    {
      // The deadline has passed. The socket is closed so that any outstanding
      // asynchronous operations are cancelled. This allows the blocked
      // connect(), read_line() or write_line() functions to return.
      boost::system::error_code ignored_ec;
      socket_.close(ignored_ec);

      // There is no longer an active deadline. The expiry is set to positive
      // infinity so that the actor takes no action until a new deadline is set.
      deadline_.expires_at(boost::posix_time::pos_infin);
    }

    // Put the actor back to sleep.
    deadline_.async_wait(bind(&client::check_deadline, this));
  }

  boost::asio::io_service io_service_;
  tcp::socket socket_;
  deadline_timer deadline_;
  boost::asio::streambuf input_buffer_;
  boost::system::error_code ec;
public:
  bool isRunning;
  std::string lastIP, lastPort;
};

//----------------------------------------------------------------------

class RndGen {
    uint64_t x , y;
public:
    RndGen(uint64_t seedx = 66666, uint64_t seedy = 23333) {
        x = (seedx == 0 ? 66666 : seedx);
        y = seedy;
    }

    uint32_t nextRnd() {
        x ^= x >> 12; // a
        x ^= x << 25; // b
        x ^= x >> 27; // c
        
        y = (y * 6364136223846793005ULL) + 1442695040888963407ULL;
        
        uint64_t nx = (x ^ y) * 2685821657736338717ULL;
        
        //nx ^= y;
        return (uint32_t)(nx >> 32);
    }
    
    uint64_t nextRnd64() {
        return (((uint64_t)nextRnd()) << 32) | ((uint64_t)nextRnd());
    }
    
    template <typename T>
    void shuffle(std::vector<T> &a) {
        for (long long i = 0; i < a.size() - 1; ++ i) {
            std::swap( a[i], a[ i + nextRnd64() % ((uint64_t)(a.size() - i)) ] );
        }
    }
};


class Tester {
public:
    void test(const std::vector< uint64_t > &addrs) {
        const std::size_t InnerSize = 63000;
        
        RndGen rng;
        std::size_t launched = 0, finished = 0;
        std::vector< client > pool( InnerSize );
               
        int LoopTimes = 0;
        while (launched < addrs.size() || finished < addrs.size()) {
            for (int i = 0; i < pool.size(); ++ i) {
                if ( (!pool[i].isRunning) && (launched < addrs.size()) ) {
                    uint64_t iIp = addrs[launched] >> 16, iPort = addrs[launched] & 0xFFFF;
                    if ((iIp & 0xff) == 0xff || (iIp & 0xff) == 0) {
                        ++ launched;
                        ++ finished;
                        -- i;
                    }
                    else {
                        std::this_thread::sleep_for( std::chrono::microseconds( 1 ) ); // approx
                        
                        std::string addr = std::to_string((iIp>>24)&0xff) + "." + std::to_string((iIp>>16)&0xff) + "." + std::to_string((iIp>>8)&0xff) + "." + std::to_string(iIp&0xff);
                        std::string port = std::to_string( iPort );
                        pool[i].connect_assyn( addr, port, boost::posix_time::seconds(2 + rng.nextRnd() % 2));
                        
                        ++ launched;
                    }
                
                }
                else if (pool[i].isRunning && (finished < addrs.size() ) ) {
                    int ret = pool[i].status();
                    if (ret == StatusSuccess) {
                        std::cout << "\r" << pool[i].lastIP << ":" << pool[i].lastPort << std::endl;
                        
                        std::ofstream fout("possible_proxies.txt", std::ios_base::app);
                        fout << pool[i].lastIP << ":" << pool[i].lastPort << std::endl;
                        fout.close();
                    }
                    if (ret == StatusSuccess || ret == StatusFail) {
                        if (++ finished < addrs.size()) -- i;
                        //++ finished;
                    }
                }
            }
            
        
            std::string out = std::to_string( 100.0 * finished/ (addrs.size() - 1) );
            while (out.size() < 6)
                out += "0";
            if (out.size() > 6)
                out = out.substr(0, 6);
            
            std::cerr << '\r' << out << "%" << std::flush;
        
            ++ LoopTimes;
        }
       
        return;
    }
};


int main(int argc, char * argv[]) {
    std::vector< uint64_t > addrs;
    
    if (argc < 2) {
        std::cerr << argv[0] << " Port !!!!" << std::endl;
        return 0;
    }
    
    int GlobalPort = atoi(argv[1]);
    
    addrs.resize( 256*256*256 );
    //addrs.resize(20);
    for (int i = 0; i < addrs.size(); ++ i) {
        //addrs[i] = (uint64_t((10<<24) | (10<<16) | (8<<8) | (i) ) << 16) | GlobalPort;
        //addrs[i] = (uint64_t((10<<24) | (202<<16) | (73<<8) | (i) ) << 16) | GlobalPort;
        addrs[i] = (uint64_t((10<<24) | (i+1)) << 16) | GlobalPort;
    }
    
    RndGen rng( std::time(NULL) );
    for (int tt = 0; tt < 1; ++ tt)
        rng.shuffle( addrs );
    
    //std::cerr << "Roll!" << std::endl;
    Tester testall;
    testall.test( addrs );
    return 0;
}