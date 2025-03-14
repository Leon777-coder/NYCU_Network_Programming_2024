#include <iostream>
#include <boost/asio.hpp>
#include <string>
#include <sstream>
#include <vector>
#include <sys/wait.h>


using boost::asio::ip::tcp;

class session
  : public std::enable_shared_from_this<session>
{
public:
  session(tcp::socket socket)
    : socket_(std::move(socket))
  {
  }

  void start()
  {
    do_read();
  }

private:
  void do_read()
  {
    auto self(shared_from_this());
    socket_.async_read_some(boost::asio::buffer(rdata_, max_length),
        [this, self](boost::system::error_code ec, std::size_t length)
        {
          if (!ec)
          {
            /*std::cout << "hello world!" << std::endl;*/
            /* view request */
            std::string req(rdata_);
            std::cout << req << std::endl;
            /* parse request */
            parseReq(req);
            
            /* fork */
            int pid;
            if ((pid = fork()) == -1){
              std::cerr << "fork: " << strerror(errno) << std::endl;
              exit(0);
            }
            if (pid == 0){ //child
              /* set all env */
              setenv("REQUEST_METHOD", REQUEST_METHOD.c_str(), 1);
              setenv("REQUEST_URI", REQUEST_URI.c_str(), 1);
              setenv("QUERY_STRING", QUERY_STRING.c_str(), 1);
              setenv("SERVER_PROTOCOL", SERVER_PROTOCOL.c_str(), 1);
              setenv("HTTP_HOST", HTTP_HOST.c_str(), 1);
              setenv("SERVER_ADDR", SERVER_ADDR.c_str(), 1);
              setenv("SERVER_PORT", SERVER_PORT.c_str(), 1);
              setenv("REMOTE_ADDR", REMOTE_ADDR.c_str(), 1);
              setenv("REMOTE_PORT", REMOTE_PORT.c_str(), 1);
              setenv("DOCUMENT_ROOT", ".", 1);
              
              /* dup, close fd, exec */
              dup2(socket_.native_handle(), STDOUT_FILENO);
              for (int fd = 3; fd <= __FD_SETSIZE; fd++){
                close(fd);
              }
              std::cout << "HTTP/1.1 200 OK\r\n";
              std::vector<std::string> emptyARGV;
              std::string fpath = std::string(getenv("DOCUMENT_ROOT")).append(getenv("REQUEST_URI"));
              if(execvp(fpath.c_str(), vecStrToChar(emptyARGV)) == -1){
                std::cerr << "execvp: " << strerror(errno) << std::endl;
                std::cout << "HTTP/1.1 404 Not Found\r\n\r\n";
                exit(0);
              }
            }
            else{ //parent
              socket_.close();
            }
          }
        });
  }

  void do_write(std::size_t length)
  {
    auto self(shared_from_this());
    boost::asio::async_write(socket_, boost::asio::buffer(wdata_, length),
        [this, self](boost::system::error_code ec, std::size_t /*length*/)
        {
          if (!ec)
          {
            // do_read();
          }
        });
  }

  

  tcp::socket socket_;
  enum { max_length = 4096 };
  char rdata_[max_length];
  char wdata_[max_length];
  // bool isPanel = false;
  // bool isConsole = false;
  std::string REQUEST_METHOD;
  std::string REQUEST_URI;
  std::string QUERY_STRING;
  std::string SERVER_PROTOCOL;
  std::string HTTP_HOST;
  std::string SERVER_ADDR;
  std::string SERVER_PORT;
  std::string REMOTE_ADDR;
  std::string REMOTE_PORT;

  /* util function */
  void parseReq(std::string req){
    /* store all env as string */
    std::istringstream issReq(req);
    std::string lineInReq;
    for (int iLine = 0; iLine < 2; iLine++){
      std::getline(issReq, lineInReq);
      if (iLine == 0){
        /* GET /panel.cgi HTTP/1.1 */
        /* GET /console.cgi?h0=nplinux1.cs.nctu.edu.tw&p0= ... */
        std::istringstream issLine(lineInReq);
        std::string wordInLine;
        for(int iWord = 0; iWord < 3; iWord++){
          std::getline(issLine, wordInLine, ' ');
          if(iWord == 0){
            /* GET */
            REQUEST_METHOD = wordInLine;
          }
          else if (iWord == 1){
            /* /panel.cgi */
            /* /console.cgi?h0=nplinux1.cs.nctu.edu.tw&p0= ... */
            std::string file = wordInLine.substr(1, wordInLine.find_first_of(".")-1);
            file = "/" + file + ".cgi";
            REQUEST_URI = file;
            if (wordInLine.find_first_of("?") == std::string::npos){
              QUERY_STRING = "";
            }else {
              QUERY_STRING = wordInLine.substr(wordInLine.find_first_of("?")+1);
            }
          }
          else{
            /* HTTP/1.1 */
            SERVER_PROTOCOL = wordInLine;
          }
        }
      }
      else if (iLine == 1){
        /* Host: localhost:18787 */
        HTTP_HOST = lineInReq.substr(6);
      }             
    }
    /* set other env */
    SERVER_ADDR = socket_.local_endpoint().address().to_string();
    SERVER_PORT = std::to_string(socket_.local_endpoint().port());
    REMOTE_ADDR = socket_.remote_endpoint().address().to_string();
    REMOTE_PORT = std::to_string(socket_.remote_endpoint().port());
  }
  char** vecStrToChar(std::vector<std::string> cmd)
  {
    char** result = (char**)malloc(sizeof(char*)*(cmd.size()+1));
    for(size_t i = 0; i < cmd.size(); i++){
      result[i] = strdup(cmd[i].c_str());
    }
    result[cmd.size()] = NULL;
    return result;
  }
};

class server
{
public:
  server(boost::asio::io_context& io_context, short port)
    : acceptor_(io_context, tcp::endpoint(tcp::v4(), port))
  {
    do_accept();
  }

private:
  void do_accept()
  {
    acceptor_.async_accept(
        [this](boost::system::error_code ec, tcp::socket socket)
        {
          if (!ec)
          {
            std::make_shared<session>(std::move(socket))->start();
          }

          do_accept();
        });
  }

  tcp::acceptor acceptor_;
};

void childHandler(int sig){
  while(waitpid(-1, NULL, WNOHANG) > 0){

  }
}


int main(int argc, char* argv[])
{
  try
  {
    if (argc != 2)
    {
      std::cerr << "Usage: http_server <port>\n";
      return 1;
    }

    signal (SIGCHLD, childHandler);

    boost::asio::io_context io_context;

    server s(io_context, std::atoi(argv[1]));

    io_context.run();
  }
  catch (std::exception& e)
  {
    std::cerr << "Exception: " << e.what() << "\n";
  }

  return 0;
}

//d3