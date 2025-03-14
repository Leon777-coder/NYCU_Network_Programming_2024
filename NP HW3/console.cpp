#include <iostream>
#include <string>
#include <sstream>
#include <vector>
#include <fstream>
#include <boost/asio.hpp>

using boost::asio::ip::tcp;

/* struct */
struct shConn{
  std::string shHost;
  std::string shPort;
  std::string cmdFile;
};

/* function */
std::vector<struct shConn> parseQry();
void output_shell(int session, std::string content);
void output_command(int session, std::string content);
void html_escape(std::string &content);
void getTemplate(std::string& tmpl, std::vector<struct shConn> shConnVec);

/* class */
class ShellSession
  : public std::enable_shared_from_this<ShellSession> 
{ 
private:
  tcp::socket socket_;
  tcp::resolver resolver_;
  tcp::resolver::results_type endpoints;
  int session;
  
  enum { max_length = 4096 };
  char rdata_[max_length];
  std::string input_buffer_;

  struct shConn shInfo;
  std::vector<std::string> cmdVec;
  std::string cmd;
  bool stopped_ = false;
public:
  ShellSession(boost::asio::io_context& io_context)
    : socket_(io_context),
      resolver_(io_context)
  {
    memset(rdata_, '\0', max_length);
  }
  void start(struct shConn shConn, int sessionID) 
  {
    shInfo = shConn;
    session = sessionID;
    memset(rdata_, '\0', max_length);
    /*
    std::cout << "constructor session: " << session << std::endl;
    std::cout << "shInfo.shHost: " << shInfo.shHost << std::endl;
    std::cout << "shInfo.shPort: " << shInfo.shPort << std::endl;
    */
    std::fstream fs("./test_case/" + shInfo.cmdFile);
    std::string lineInCmd;
    while(std::getline(fs, lineInCmd)){
      lineInCmd.append("\n");
      cmdVec.push_back(lineInCmd);
    }
    fs.close();
    /*
    for (size_t i = 0; i < cmdVec.size(); i++){
      std::cout << i << "\t: " << cmdVec[i] << std::flush;
    }
    */
    do_resolve();
  }
  void stop() 
  {
    stopped_ = true;
    //std::cout << "destructor session: " << session << std::endl;
    socket_.close();
  }
private:  
  void do_resolve() {
    if (stopped_) return;
    auto self(shared_from_this());
    resolver_.async_resolve(shInfo.shHost, shInfo.shPort,
      [this, self](boost::system::error_code ec,
      tcp::resolver::results_type returned_endpoints)
      {
        if (stopped_) return;
        if (!ec)
        {
          endpoints = returned_endpoints;
          tcp::resolver::results_type::iterator endpoint_iter = endpoints.begin();
          while(endpoint_iter != endpoints.end())
          {
            //std::cout << "Trying " << endpoint_iter->endpoint() << "\n";
            endpoint_iter++;
          }
          do_connect();
        }
        else{
          std::cout << "resolve Error: " << ec.message() << "\n";
          stop();
        }
      });
  }
  void do_connect() { // get socket after connect
    if (stopped_) return;
    auto self(shared_from_this());
    boost::asio::async_connect(socket_, endpoints,
    [this, self](boost::system::error_code ec, tcp::endpoint){
      if (stopped_) return;
      if (!ec)
      {
        //do_send_cmd(); // for test in echo server
        do_read();
      }
      else{
        std::cout << "connect Error: " << ec.message() << "\n";
        stop();
      }
    });
  }
  void do_read() 
  {
    if (stopped_) return;
    auto self(shared_from_this());
    /*
    boost::asio::async_read_until(socket_,
        boost::asio::dynamic_buffer(input_buffer_), "% ",
        [this, self](boost::system::error_code ec, std::size_t length)
        {
          if (stopped_) return;
          if (!ec){
            std::string reply(input_buffer_.substr(0, length - 2));
            input_buffer_.erase(0, length);
            reply.append("% ");
            //std::cout << reply << std::flush;
            output_shell(session, reply);
            do_send_cmd();
          }else{
            if (ec == boost::asio::error::eof){
              //std::cout << "get eof" << std::endl;
              stop();
            }else{
              std::cout << "read Error: " << ec.message() << std::endl;

            }
          }
        });
    */
    socket_.async_read_some(
        boost::asio::buffer(rdata_, max_length-1), 
        [this, self](boost::system::error_code ec, std::size_t length)
        {
          if (stopped_) return;
          if (!ec){
            rdata_[length+1] = '\0';
            //std::cout << "(raw recv "<< length <<")" << rdata_ << std::endl;
            
            std::string reply = std::string(rdata_);
            memset(rdata_, '\0', max_length);
            output_shell(session, reply);
            //std::cout << "(recv)" << reply << std::flush;
            
            if (reply.find("% ") != std::string::npos){
              //std::cout << "get %" << std::endl;
              do_send_cmd();
            }else{
              // do_send_cmd(); // for test in echo server
               do_read();
            }
            
          }
          else{
            if (ec == boost::asio::error::eof){
              //std::cout << "get eof" << std::endl;
              stop();
            }else{
              std::cout << "read Error: " << ec.message() << std::endl;
            }
          }
        });
        
  }
  void do_send_cmd() {
    if (stopped_) return;
    // get cmd from .txt
    cmd = cmdVec.front();
    cmdVec.erase(cmdVec.begin());
    output_command(session, cmd);
    //std::cout << "(sent)" << cmd << std::flush;
    
    // send cmd to shell server 
    auto self(shared_from_this());
    boost::asio::async_write(socket_, boost::asio::buffer(cmd.c_str(), cmd.size()),
        [this, self](boost::system::error_code ec, std::size_t )
        {
          if (stopped_) return;
          if (!ec)
          {
            if (cmd == "exit\n"){
              stop();
            }
            else{
              do_read();
            }
          }
          else{
            std::cout << "write Error: " << ec.message() << std::endl;
            stop();
          }
        });

  }
};


int main(){
  /* test */
  //setenv("QUERY_STRING", "h0=nplinux1.cs.nctu.edu.tw&p0=19999&f0=t5.txt&h1=&p1=&f1=&h2=&p2=&f2=&h3=&p3=&f3=&h4=&p4=&f4=", 1);
  

  std::vector<struct shConn> shConnVec = parseQry();
  std::string tmpl;
  getTemplate(tmpl, shConnVec);
  /* render html */
  std::cout << "Content-type: text/html\r\n\r\n" << std::flush;
  std::cout << tmpl << std::flush;
  
  size_t s;
  try
  {
    boost::asio::io_context io_context;
    for (s = 0; s < 5; s++){
      if (shConnVec[s].shHost == "") continue;
      //std::cout << "start session: " << s << std::endl;
      std::make_shared<ShellSession>(io_context)->start(shConnVec[s], (int)s);
    }
    io_context.run();
  }
  catch (std::exception& e)
  {
    std::cout << e.what() << std::endl;
    //output_command(s, std::string(e.what())+"\n");
  }

  return 0;
}

std::vector<struct shConn> parseQry(){
  std::vector<struct shConn> shConnVec;

  std::istringstream issQry(getenv("QUERY_STRING"));
  std::string itmInQry;
  std::string itmNoHead;
  for(int iConn = 0; iConn < 5; iConn++){
    struct shConn tmp;
    shConnVec.push_back(tmp);
    /* nplinux1.cs.nctu.edu.tw */
    getline(issQry, itmInQry, '&');
    itmNoHead = itmInQry.substr(itmInQry.find_first_of("=")+1);
    shConnVec[iConn].shHost = itmNoHead;
    /**************************************************//*
    if (itmNoHead != ""){
      shConnVec[iConn].shHost = "127.0.0.1";
    }
    *//**************************************************/
    /* 1234 */
    getline(issQry, itmInQry, '&');
    itmNoHead = itmInQry.substr(itmInQry.find_first_of("=")+1);
    shConnVec[iConn].shPort = itmNoHead;
    /* t1.txt */
    getline(issQry, itmInQry, '&');
    itmNoHead = itmInQry.substr(itmInQry.find_first_of("=")+1);
    shConnVec[iConn].cmdFile = itmNoHead;
  }
  return shConnVec;
}

void output_shell(int session, std::string content){
  std::string htmlContent(content);
  html_escape(htmlContent);
  std::cout << "<script>document.getElementById('s" << session << "').innerHTML += '" << htmlContent << "';</script>" << std::flush;
}

void output_command(int session, std::string content){
  std::string htmlContent(content);
  html_escape(htmlContent);
  std::cout << "<script>document.getElementById('s" << session << "').innerHTML += '<b>" << htmlContent <<"</b>';</script>" << std::flush;
}

void html_escape(std::string &content){
  std::string newContent;
  for(size_t i = 0; i < content.size(); i++){
    std::string curChar = content.substr(i, 1);
    if(curChar == "&") newContent.append("&amp;");
    else if(curChar == ">") newContent.append("&gt;");
    else if(curChar == "<") newContent.append("&lt;");
    else if(curChar == "\"") newContent.append("&quot;");
    else if(curChar == "\'") newContent.append("&apos;");
    else if(curChar == "\n") newContent.append("&NewLine;");
    else if(curChar == "\r") newContent.append("");
    else newContent.append(curChar);
  }
  content.clear();
  content.append(newContent);
}

void getTemplate(std::string& tmpl, std::vector<struct shConn>shConnVec){
  tmpl = 
  "<!DOCTYPE html>"
  "<html lang=\"en\">"
    "<head>"
      "<meta charset=\"UTF-8\" />"
      "<title>NP Project 3 Sample Console</title>"
      "<link "
        "rel=\"stylesheet\""
        "href=\"https://cdn.jsdelivr.net/npm/bootstrap@4.5.3/dist/css/bootstrap.min.css\""
        "integrity=\"sha384-TX8t27EcRE3e/ihU7zmQxVncDAy5uIKz4rEkgIXeMed4M0jlfIDPvg6uqKI2xXr2\""
        "crossorigin=\"anonymous\""
      "/>"
      "<link "
        "href=\"https://fonts.googleapis.com/css?family=Source+Code+Pro\""
        "rel=\"stylesheet\""
      "/>"
      "<link "
        "rel=\"icon\""
        "type=\"image/png\""
        "href=\"https://cdn0.iconfinder.com/data/icons/small-n-flat/24/678068-terminal-512.png\""
      "/>"
      "<style>"
        "* {"
          "font-family: 'Source Code Pro', monospace;"
          "font-size: 1rem !important;"
        "}"
        "body {"
          "background-color: #212529;"
        "}"
        "pre {"
          "color: #cccccc;"
        "}"
        "b {"
          "color: #01b468;"
        "}"
      "</style>"
    "</head>"
    "<body>"
      "<table class=\"table table-dark table-bordered\">"
        "<thead>"
          "<tr>"
            "<th scope=\"col\">";
  if (shConnVec[0].shHost != "")
    tmpl.append(shConnVec[0].shHost+":"+shConnVec[0].shPort);
  tmpl.append(
            "</th>"
            "<th scope=\"col\">"
  );
  if (shConnVec[1].shHost != "")
   tmpl.append(shConnVec[1].shHost+":"+shConnVec[1].shPort);
  tmpl.append(
            "</th>"
          "</tr>"
        "</thead>"
        "<tbody>"
          "<tr>"
            "<td><pre id=\"s0\" class=\"mb-0\"></pre></td>"
            "<td><pre id=\"s1\" class=\"mb-0\"></pre></td>"
          "</tr>"
        "</tbody>"
      "</table>"
      "<table class=\"table table-dark table-bordered\">"
        "<thead>"
          "<tr>"
            "<th scope=\"col\">"
  );
  if (shConnVec[2].shHost != "")
    tmpl.append(shConnVec[2].shHost+":"+shConnVec[2].shPort);
  tmpl.append(
            "</th>"
            "<th scope=\"col\">"
  );
  if (shConnVec[3].shHost != "")
    tmpl.append(shConnVec[3].shHost+":"+shConnVec[3].shPort);
  tmpl.append(
            "</th>"
          "</tr>"
        "</thead>"
        "<tbody>"
          "<tr>"
            "<td><pre id=\"s2\" class=\"mb-0\"></pre></td>"
            "<td><pre id=\"s3\" class=\"mb-0\"></pre></td>"
          "</tr>"
        "</tbody>"
      "</table>"
      "<table class=\"table table-dark table-bordered\">"
        "<thead>"
          "<tr>"
            "<th scope=\"col\">"
  );
  if (shConnVec[4].shHost != "")
    tmpl.append(shConnVec[4].shHost+":"+shConnVec[4].shPort);
  tmpl.append(
            "</th>"
          "</tr>"
        "</thead>"
        "<tbody>"
          "<tr>"
            "<td><pre id=\"s4\" class=\"mb-0\"></pre></td>"
          "</tr>"
        "</tbody>"
      "</table>"
    "</body>"
  "</html>"
  );
}

//d3