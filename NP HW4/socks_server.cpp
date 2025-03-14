#include <iostream>
#include <boost/asio.hpp>
#include <string>
#include <sstream>
#include <vector>
#include <sys/wait.h>
#include <netinet/in.h>
#include <stdio.h>
#include <algorithm>
#include <fstream>
#include <regex>


using boost::asio::ip::tcp;
boost::asio::io_context io_context;

class socks_server {
 public:
  socks_server(uint16_t port)
      : _acceptor(io_context, {tcp::v4(), port})
  {
    do_accept();
  }

 private:
  void do_accept() {
    _acceptor.async_accept(
        [this](boost::system::error_code ec, tcp::socket new_socket) {
          if (!ec) {
            _browser_socket = std::move(new_socket);
            io_context.notify_fork(boost::asio::io_context::fork_prepare);

            int pid;
            if ((pid = fork()) == -1){
              std::cerr << "fork: " << strerror(errno) << std::endl;
              exit(0);
            }
            if (pid == 0) { //child
              io_context.notify_fork(boost::asio::io_context::fork_child);
              _acceptor.close();
              do_read_req();

            } else { //paraent
              io_context.notify_fork(boost::asio::io_context::fork_parent);
              _browser_socket.close();
              do_accept();
            }
          } else {
            std::cerr << "Accept error: " << ec.message() << std::endl;
            do_accept();

          }
        });
  }

  void do_read_req() {
    _browser_socket.async_read_some(
        boost::asio::buffer(input_buffer_, max_length),
        [this](boost::system::error_code ec, std::size_t length)
        {
          if (!ec)
          {
            //std::cout << "read some length: " << length << std::endl;
            /*
            for (size_t i = 0; i < length; i++){
              printf("%d\n", input_buffer_[i]);
            }
            */
            
            //std::cout << "socks4 size: " << sizeof(req) << std::endl;
            req.VN = input_buffer_[0];
            req.CD = input_buffer_[1];
            req.DSTPORT = input_buffer_[2] << 8 |
                          input_buffer_[3];
            req.DSTIP[0] = input_buffer_[4];
            req.DSTIP[1] = input_buffer_[5];
            req.DSTIP[2] = input_buffer_[6];
            req.DSTIP[3] = input_buffer_[7];
            /*
            if(req.DSTIP[0] != 140 || req.DSTIP[1] != 113){
              std::cout << "reject: " << IPtoStr(req.DSTIP) << std::endl;
              _browser_socket.close();
              exit(0);
            }
            */
             
            if (isSocks4a()){
              bool foundNull = false;
              for (std::size_t i = 8; i < length; i++){
                if (input_buffer_[i] == 0 && foundNull == false){
                  foundNull = true;
                  continue;
                }
                if (foundNull == true){
                  unresolveDst.push_back(input_buffer_[i]);
                }
              }
              if (unresolveDst.size()!=0){
                //std::cout << "<D_DOMAIN>: " << unresolveDst << std::endl;
              }
              
            }
            

            SRCIP = _browser_socket.remote_endpoint().address().to_string();
            SRCPORT = _browser_socket.remote_endpoint().port();
            SOCKSIP = _browser_socket.local_endpoint().address();

            //std::cout << "req.VN: " << (int)req.VN << std::endl;
            //std::cout << "req.CD: " << (int)req.CD << std::endl;
            std::cout << std::endl;
            std::cout << "<S_IP>: " << SRCIP << std::endl;
            std::cout << "<S_PORT>: " << SRCPORT << std::endl;
            //std::cout << "<SOCKS_IP>: " << SOCKSIP << std::endl;
            
            if (req.CD == CD_CONN){
              // resolve
              if (isSocks4a()){
                do_resolve(unresolveDst);
              }else{
                do_resolve(IPtoStr(req.DSTIP));
              }
            }else if (req.CD == CD_BIND){
              dstip = IPtoStr(req.DSTIP);
              std::cout << "<D_IP>: " << dstip << std::endl;
              std::cout << "<D_PORT>: " << req.DSTPORT << std::endl;
              std::cout << "<Command>: BIND" << std::endl;
          
              firewall();
              if (rep.CD == CD_ACCP){
                std::cout << "<Reply>: Accept" << std::endl;
              }else if (rep.CD == CD_RJCT){
                std::cout << "<Reply>: Reject" << std::endl;
              }
              
              output_buffer_.clear();
              rep.VN = (unsigned char) 0;
              rep.CD = (unsigned char) CD_ACCP;
              output_buffer_.push_back(rep.VN);
              output_buffer_.push_back(rep.CD);

              
              //std::cout << "start binding ftpListener ******************" << std::endl;
              do_ftp_accept();
              do_write_rep();
              
            }
                
            
          }
        });
  }

  void do_ftp_accept(){
      tcp::endpoint endpoint(tcp::v4(), 0);

      _ftp_acceptor.open(endpoint.protocol());
      _ftp_acceptor.bind(endpoint);
      _ftp_acceptor.listen();
      BINDPORT = _ftp_acceptor.local_endpoint().port();

      // build server for ftp data transmission
      boost::asio::ip::address_v4::bytes_type socksipByte = SOCKSIP.to_v4().to_bytes();
      // DST PORT & IP are set
      unsigned char h_port = (unsigned char) (BINDPORT >> 8);
      unsigned char l_port = (unsigned char) (BINDPORT & 0x00FF);
      output_buffer_.push_back(h_port);
      output_buffer_.push_back(l_port);
      for (int i = 0; i < 4; i++){
        output_buffer_.push_back(socksipByte[i]);
      }
      /*
      std::cout << "output_buffer ******************" << std::endl;
      for (int i = 0; i < 8; i++){
        printf("%d\n", output_buffer_[i]);
      }
      */

      do_write_rep_only();
      _ftp_acceptor.accept(_service_socket);

    /*
    _ftp_acceptor.async_accept(
      [this](boost::system::error_code ec, tcp::socket new_socket) {
        if (!ec) {
          _service_socket = std::move(new_socket);
          std::cout << "Listening on ftp_socket... " << ec.message() << std::endl;

        } else {
          std::cerr << "Accept error: " << ec.message() << std::endl;
          do_ftp_accept();
        }
      });
      */
  }

  void do_resolve(std::string ipOrUrl) {
    _resolver.async_resolve(ipOrUrl, std::to_string(req.DSTPORT),
      [this](boost::system::error_code ec,
      tcp::resolver::results_type returned_endpoints)
      {
        if (!ec)
        {
          endpoints = returned_endpoints;
          tcp::resolver::results_type::iterator endpoint_iter = endpoints.begin();
          /*
          while(endpoint_iter != endpoints.end())
          {
            std::cout << "Trying " << endpoint_iter->endpoint() << "\n";
            endpoint_iter++;
            
          }
          */
          do_connect();
        }
        else{
          std::cout << "resolve Error: " << ec.message() << "\n";
        }
      });
  }

  void do_connect() { // get socket after connect
    boost::asio::async_connect(_service_socket, endpoints,
    [this](boost::system::error_code ec, tcp::endpoint endpoint){
      if (!ec)
      {
        //std::cout << "Done service connection" << std::endl;
        
        // print remaining info
        dstip = endpoint.address().to_string();
        std::cout << "<D_IP>: " << dstip << std::endl;
        std::cout << "<D_PORT>: " << req.DSTPORT << std::endl;
        std::cout << "<Command>: CONNECT" << std::endl;
        
        firewall();
        if (rep.CD == CD_ACCP){
          std::cout << "<Reply>: Accept" << std::endl;
        }else if (rep.CD == CD_RJCT){
          std::cout << "<Reply>: Reject" << std::endl;
        }

        // form reply
        output_buffer_.clear();
        rep.VN = (unsigned char) 0;
        output_buffer_.push_back(rep.VN);
        output_buffer_.push_back(rep.CD);

        // DST PORT & IP are useless fields
        for (int i = 0; i < 6; i++){
          output_buffer_.push_back(0);
        }
        do_write_rep();

      }
      else{
        std::cout << "connect Error: " << ec.message() << "\n";
      }
    });
  }
  
  void firewall(){
    // match endpoints socks.conf 
    std::ifstream fs;
    rep.CD = (unsigned char) CD_RJCT;

    try{
      fs.open("socks.conf");
    }catch(std::exception& e){
      std::cerr << e.what() << std::endl;
    }
    std::string rawRule;
    std::string permit, action, ippat, ipreg;
    while(std::getline(fs, rawRule)){
      std::istringstream iss(rawRule);
      std::getline(iss, permit, ' ');
      std::getline(iss, action, ' ');
      if ((req.CD == CD_CONN && action == "c") ||
          (req.CD == CD_BIND && action == "b")){
        //std::cout << "rule: " << rawRule << std::endl;
        
        std::getline(iss, ippat, ' ');
        //ipreg="/";
        ipreg.clear();
        for(size_t i = 0; i < ippat.size(); i++){
          if(ippat[i] == '.'){
            ipreg.append("\\.");
          }else if(ippat[i] == '*'){
            ipreg.append("(25[0-5]|2[0-4]\\d|1\\d\\d|[1-9]\\d|\\d)");
          }else{
            ipreg.append(ippat.substr(i,1));
          }
        }
        //ipreg.append("/g");
        //std::cout << "regex: " << ipreg << std::endl;

        if (std::regex_match(dstip, std::regex(ipreg))){
          //std::cout << "dstip accept\n";
          rep.CD = (unsigned char) CD_ACCP;
          break;
        }
      }
    }
    fs.close();
  }

  void do_write_rep() {
    boost::asio::async_write(_browser_socket, boost::asio::buffer(output_buffer_, output_buffer_.size()),
        [this](boost::system::error_code ec, std::size_t )
        {
          if (!ec)
          {
            

            if (req.CD == CD_CONN){
              //std::cout << "Done socks4 CONNECT reply" << std::endl;
            }else if(req.CD == CD_BIND){
              //std::cout << "Done socks4 BIND reply 2" << std::endl;
            }
            
            if (rep.CD == CD_RJCT){
              _browser_socket.close();
              _service_socket.close();
              //std::cout << "Reject, socket closed..." << std::endl;

            }else{
              do_read_serv_req();
              do_read_serv_rep();
            }
            
          }
        });
  }
  void do_write_rep_only() {
    boost::asio::async_write(_browser_socket, boost::asio::buffer(output_buffer_, output_buffer_.size()),
        [this](boost::system::error_code ec, std::size_t )
        {
          if (!ec)
          {
            //std::cout << "Done socks4 BIND reply 1" << std::endl;
          }
        });
  }
  void do_read_serv_req() {
    _browser_socket.async_read_some(
        boost::asio::buffer(brdata_, max_length-1),
        [this](boost::system::error_code ec, std::size_t length)
        {
          if (!ec)
          {
            brdata_[length+1] = '\0';
            //std::cout << "(raw brdata_ "<< length <<")" << brdata_ << std::endl;
            do_write_serv_req(length);
          }
        });
  }
  void do_write_serv_req(std::size_t length){
    boost::asio::async_write(_service_socket, boost::asio::buffer(brdata_, length),
        [this](boost::system::error_code ec, std::size_t )
        {
          if (!ec)
          {
            //std::cout << "Done passing http request" << std::endl;
            memset(brdata_, '\0', max_length);
            do_read_serv_req();
          }
        });
  }
  void do_read_serv_rep(){
    _service_socket.async_read_some(
        boost::asio::buffer(srdata_, max_length-1),
        [this](boost::system::error_code ec, std::size_t length)
        {
          if (!ec)
          {
            srdata_[length+1] = '\0';
            //std::cout << "(raw srdata_ "<< length <<")" << srdata_ << std::endl;
            do_write_serv_rep(length);
          }
        });
  }
  void do_write_serv_rep(std::size_t length){
    boost::asio::async_write(_browser_socket, boost::asio::buffer(srdata_, length),
        [this](boost::system::error_code ec, std::size_t )
        {
          if (!ec)
          {
            //std::cout << "Done passing http reply" << std::endl;
            memset(srdata_, '\0', max_length);
            do_read_serv_rep();
          }
        });
  }


  tcp::acceptor _acceptor{io_context};
  tcp::acceptor _ftp_acceptor{io_context};
  tcp::resolver _resolver{io_context};
  tcp::resolver::results_type endpoints;
  tcp::socket _browser_socket{io_context};
  tcp::socket _service_socket{io_context};
  
  enum { max_length = 1024 };
  char brdata_[max_length];
  char srdata_[max_length];
  std::size_t http_req_length;
  std::size_t http_rep_length;
  
  unsigned char input_buffer_[max_length];
  std::vector<unsigned char> output_buffer_;
  struct socks4{
    unsigned char VN;
    unsigned char CD;
    unsigned short DSTPORT; // 2 byte
    unsigned char DSTIP[4]; // 4 byte
    unsigned char nullByte = '\0';
  };
  std::string unresolveDst;
  enum {
    CD_CONN = 1,
    CD_BIND = 2,
    CD_ACCP = 90,
    CD_RJCT = 91
  };
  struct socks4 req, rep;
  unsigned short SRCPORT;
  std::string SRCIP;
  unsigned short BINDPORT;
  boost::asio::ip::address SOCKSIP;
  std::string dstip;

  // util
  std::string IPtoStr(unsigned char* iparr){
    char buffer [16];
    sprintf(buffer, "%d.%d.%d.%d", iparr[0], iparr[1], iparr[2], iparr[3]);
    return std::string(buffer);
  }
  bool isSocks4a(){
    return (req.DSTIP[0] == 0 && req.DSTIP[1] == 0 && req.DSTIP[2] == 0 && req.DSTIP[3] != 0);
  }
};

void childHandler(int sig){
  while(waitpid(-1, NULL, WNOHANG) > 0){
  }
}

int main(int argc, char *argv[]) {
  using namespace std;
  signal (SIGCHLD, childHandler);
  
  try {
    if (argc!=2) {
      std::cerr << "Usage: ./socks_server <port>\n";
      return 1;
    }
    socks_server s(atoi(argv[1]));
    io_context.run();
  }
  catch (exception &e) {
    cerr << "Exception: " << e.what() << endl;
  }
}