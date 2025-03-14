#include <cstdlib>
#include <iostream>
#include <memory>
#include <utility>
#include <strings.h>
#include <sys/stat.h>
#include <fstream>
#include <boost/asio.hpp>
#include <boost/algorithm/string.hpp>


#define MAX_SERVER  5
#define ATTRIBUTES_SIZE 3
#define BUFFER_SIZE     4096

using boost::asio::ip::tcp;
using boost::asio::io_service;
using namespace std;

typedef struct client_info {
    int    ID;
    string host;
    string port;
    string fname;
    string full_message;
    tcp::socket *sock;
    ifstream *input_file;
    char     data_buffer[BUFFER_SIZE];
} ClientInfo;
vector<ClientInfo> clients;

boost::asio::io_context io_context;
string TEST_CASE_DIR("./test_case/");

vector<string> my_split(string str, char delimeter) {
    stringstream ss(str);
    string token;
    vector<string> result;

    while (getline(ss, token, delimeter)) {
        result.push_back(token);
    }

    return result;
}

inline void show_error(char *func_name, int code, string msg) {
    fprintf(stderr, "[%s]: (%d, %s)\n", func_name, code, msg.c_str());
}

class shellSession: public enable_shared_from_this<shellSession> {
public:
    shellSession(boost::asio::io_context& io_context, tcp::socket c_sock):
        resolver_(io_context),
        client_sock(move(c_sock)) {
            close_counter = 0;

            for (size_t x=0; x < clients.size(); ++x) {
                clients[x].sock = new tcp::socket(io_context);
                clients[x].input_file = new ifstream;

                // Clean buffer
                memset(clients[x].data_buffer, '\0', BUFFER_SIZE);

                // Open File
                clients[x].input_file->open(TEST_CASE_DIR + clients[x].fname, ios::in);
                if (clients[x].input_file->fail()) {
                    perror("Open file");
                }
            }
        }

    void start() {
        for (auto &c: clients) {
            do_resolve(c.ID);
        }
    }

    void do_resolve(int session_id) {
        #if 0
        cout << "Session: " << session_id << " do resolve" << endl;
        #endif
        tcp::resolver::query q(clients[session_id].host, clients[session_id].port);

        auto self(shared_from_this());
        resolver_.async_resolve(q,
            [this, self, session_id](boost::system::error_code ec, tcp::resolver::iterator iter) {
                if (!ec) {
                    do_connect(session_id, iter);
                } else {
                    show_error("shellSession:do_resolve", ec.value(), ec.message().c_str());
                }
            }
        );
    }

    void do_connect(int session_id, tcp::resolver::iterator iter) {
        #if 0
        cout << "Session: " << session_id << " do connect" << endl;
        #endif
        auto self(shared_from_this());
        clients[session_id].sock->async_connect(*iter,
            [this, self, iter, session_id](boost::system::error_code ec) {
                if (!ec) {
                    do_read(session_id);
                } else {
                    show_error("shellSession:do_connect", ec.value(), ec.message().c_str());
                    clients[session_id].sock->close();
                    do_connect(session_id, iter);
                }
            }
        );
    }

    void do_read(int session_id) {
        #if 0
        cout << "Session: " << session_id << " do read" << endl;
        #endif
        auto self(shared_from_this());
        clients[session_id].sock->async_read_some(boost::asio::buffer(clients[session_id].data_buffer, BUFFER_SIZE),
            [this, self, session_id](boost::system::error_code ec, size_t length) {
                if (!ec) {
                    clients[session_id].full_message += clients[session_id].data_buffer;
                    memset(clients[session_id].data_buffer, '\0', length);

                    print_result(session_id, clients[session_id].full_message);
                    if (clients[session_id].full_message.find("% ") != string::npos) {
                        do_write(session_id);
                    }
                    clients[session_id].full_message.clear();

                    do_read(session_id);
                } else {
                    if (ec.value() == boost::asio::error::eof) {
                        do_close(session_id);
                    } else {
                        show_error("shellSession:do_read", ec.value(), ec.message().c_str());
                    }
                }
            }
        );
    }

    void do_write(int session_id) {
        #if 0
        cout << "Session: " << session_id << " do write" << endl;
        #endif
        string command;

        auto self(shared_from_this());

        if (clients[session_id].input_file->eof()) {
            cerr << "Read EOF" << endl;
            // TODO: close here?
            return;
        }
        if (!getline(*clients[session_id].input_file, command)) {
            perror("Do write, read from file");
        }
        command += "\n";
        print_command(session_id, command);

        clients[session_id].sock->async_write_some(boost::asio::buffer(command.c_str(), command.length()), 
            [this,self](boost::system::error_code ec, size_t length) {
                if (!ec) {
                    // do nothing
                } else {
                    show_error("shellSession:do_write", ec.value(), ec.message().c_str());
                }
            }
        );
    }

    void do_close(int session_id) {
        #if 0
        cout << "Session: " << session_id << " do close" << endl;
        #endif
        clients[session_id].input_file->close();
        clients[session_id].sock->close();
        ++close_counter;

        delete clients[session_id].input_file;
        delete clients[session_id].sock;

        if(close_counter == clients.size()) {
            #if 0
            cout << "shell session close client socket" << endl;
            #endif
            client_sock.close();
            clients.clear();
        }
    }

    void string2html(string &input) {
        boost::algorithm::replace_all(input,"&","&amp;");
        boost::algorithm::replace_all(input,"<","&lt;");
        boost::algorithm::replace_all(input,">","&gt;");
        boost::algorithm::replace_all(input,"\"","&quot;");
        boost::algorithm::replace_all(input,"\'","&apos;");
        boost::algorithm::replace_all(input,"\r\n","\n");
        boost::algorithm::replace_all(input,"\n","<br>");
    }

    void print_result(int session_id, string content) {
        ostringstream oss;
        string result;

        string2html(content);
        oss << "<script>document.getElementById('s"
            << session_id
            << "').innerHTML += '"
            << content
            << "';</script>";
        result = oss.str();

        do_write_client(result);
    }

    void print_command(int session_id, string command) {
        ostringstream oss;
        string result;

        string2html(command);
        oss << "<script>document.getElementById('s"
            << session_id
            << "').innerHTML += '<b>"
            << command
            << "</b>';</script>";
        result = oss.str();

        do_write_client(result);
    }

    void do_write_client(string content) {
        auto self(shared_from_this());
        client_sock.async_write_some(boost::asio::buffer(content.c_str(), content.length()),
            [this,self](boost::system::error_code ec, size_t length) {
                if (!ec) {
                    // do nothing
                } else {
                    show_error("shellSession:do_write_client", ec.value(), ec.message().c_str());
                }
            }
        );
    }

private:
    tcp::resolver resolver_;
    tcp::socket client_sock; // Between client and http server
    size_t close_counter;
};


class htmlSession: public enable_shared_from_this<htmlSession> {
public:
    htmlSession(tcp::socket socket): sock(move(socket)) {
        memset(data, '\0', max_length);
        memset(empty, '\0', 1);
        memset(request_method, '\0', 8);
        memset(request_uri, '\0', 1024);
        memset(query_string, '\0', 1024);
        memset(server_protocol, '\0', 32);
    }

    void start() {
        do_read();
    }

private:

    tcp::socket sock;
    enum { max_length = 4096 };
    char data[max_length];
    char empty[1];
    char http_200[64] = "HTTP/1.1 200 OK\r\n";
    char request_method[8];
    char request_uri[1024];
    char query_string[1024];
    char server_protocol[32];
    string http_host;
    string prog_name;
    string server_addr;
    string server_port;
    string remote_addr;
    string remote_port;

    void parse_request() {
        #if 0
        cout << "Parse Request: " << data << endl;
        #endif

        // Parse first line
        sscanf(data, "%s %s %s\r\n", request_method, request_uri, server_protocol);
        #if 0
        cout << "Method: " << request_method << endl
                << "Protocol: " << server_protocol << endl;
        #endif

        // Parse query string
        char tmp[64];
        memset(tmp, '\0', 64);
        sscanf(request_uri, "%[^?]?%s", tmp, query_string);
        prog_name = "." + string(tmp);
        #if 0
        cout << "URI: " << request_uri << endl
                << "Program Name: " << prog_name << endl
                << "Query String: " << query_string << endl;
        #endif 

        // Get server and remote address information
        server_addr = sock.local_endpoint().address().to_string();
        server_port = to_string(static_cast<unsigned short>(sock.local_endpoint().port()));
        remote_addr = sock.remote_endpoint().address().to_string();
        remote_port = to_string(static_cast<unsigned short>(sock.remote_endpoint().port()));
        http_host = server_addr + ":" + server_port;
        #if 0
        cout << "Server: " << server_addr << ":" << server_port << endl;
        cout << "Remote: " << remote_addr << ":" << remote_port << endl;
        #endif
    }

    void do_read() {
        auto self(shared_from_this());
        sock.async_read_some(boost::asio::buffer(data, max_length),
                [this, self](boost::system::error_code ec, size_t length) {
                    /* Read Handler */
                    if (!ec) {
                        do_write();
                    } else {
                        show_error("htmlSession:do_read", ec.value(), ec.message().c_str());
                    }
                }
        );
    }

    void do_write() {
        auto self(shared_from_this());
        boost::asio::async_write(sock, boost::asio::buffer(http_200, strlen(http_200)),
            [this, self](boost::system::error_code ec, size_t length) {
                /* Write Handler */
                if (!ec) {
                    parse_request();

                    if (prog_name.compare("./panel.cgi") == 0) {
                        handle_panel_cgi();
                    } else if (prog_name.compare("./console.cgi") == 0) {
                        handle_console_cgi();
                    } else {
                        // Ignore other CGI program
                    }
                } else {
                    show_error("htmlSession:do_write", ec.value(), ec.message().c_str());
                }
            }
        );
    }

    void do_close() {
        sock.close();
    }

    void handle_panel_cgi() {
        ostringstream html_content;
        string test_case_menu;
        string host_menu;
        char tmp[128];

        // Create test case menu
        for (int x=0; x < 5; ++x) {
            memset(tmp, '\0', 128);
            sprintf(tmp, "<option value=\"t%d.txt\">t%d.txt</option>", x+1, x+1);
            test_case_menu += tmp;
        }

        // Create host menu
        for (int x=1; x < 13; ++x) {
            memset(tmp, '\0', 128);
            sprintf(tmp, "<option value=\"nplinux%d.cs.nctu.edu.tw\">nplinux%d</option>", x, x);
            host_menu += tmp;
        }

        // Build html content
        html_content << "Content-type: text/html\r\n\r\n";
        html_content << "<!DOCTYPE html>"
            << "<html lang=\"en\">"
            << "<head>"
            << "<title>NP Project 3 Panel</title>"
            << "<link"
            << "    rel=\"stylesheet\""
            << "    href=\"https://cdn.jsdelivr.net/npm/bootstrap@4.5.3/dist/css/bootstrap.min.css\""
            << "    integrity=\"sha384-TX8t27EcRE3e/ihU7zmQxVncDAy5uIKz4rEkgIXeMed4M0jlfIDPvg6uqKI2xXr2\""
            << "    crossorigin=\"anonymous\""
            << "/>"
            << "<link"
            << "    href=\"https://fonts.googleapis.com/css?family=Source+Code+Pro\""
            << "    rel=\"stylesheet\""
            << "/>"
            << "<link"
            << "    rel=\"icon\""
            << "    type=\"image/png\""
            << "    href=\"https://cdn4.iconfinder.com/data/icons/iconsimple-setting-time/512/dashboard-512.png\""
            << "/>"
            << "<style>"
            << "* {"
            << "font-family: 'Source Code Pro', monospace;"
            << "}"
            << "</style>"
            << "</head>"
            << "<body class=\"bg-secondary pt-5\">"
            << "<form action=\"console.cgi\" method=\"GET\">"
            << "<table class=\"table mx-auto bg-light\" style=\"width: inherit\">"
            << "<thead class=\"thead-dark\">"
            << "    <tr>"
            << "        <th scope=\"col\">#</th>"
            << "        <th scope=\"col\">Host</th>"
            << "        <th scope=\"col\">Port</th>"
            << "        <th scope=\"col\">Input File</th>"
            << "    </tr>"
            << "</thead>"
            << "<tbody>";
        
        for (int x=0; x < MAX_SERVER; ++x) {
            html_content << "<tr>"
                << "<th scope=\"row\" class=\align-middle\">Session " << x+1 << "</th>"
                << "<td>"
                << "<div class=\"input-group\">"
                << "<select name=\"h" << x << "\" class=\"custom-select\">"
                << "<option></option>"
                << host_menu
                << "</select>"
                << "<div class=\"input-group-append\">"
                << "<span class=\"input-group-text\">.cs.nctu.edu.tw</span>"
                << "</div>"
                << "</div>"
                << "</td>"
                << "<td>"
                << "<input name=\"p" << x << "\" type=\"text\" class=\"form-control\" size=\"5\" />"
                << "</td>"
                << "<td>"
                << "<select name=\"f" << x << "\" class=\"custom-select\">"
                << "<option></option>"
                << test_case_menu
                << "</select>"
                << "</td>"
                << "</tr>";
        }

        html_content << "<tr>"
            << "<td colspan=\"3\"></td>"
            << "<td>"
            << "<button type=\"submit\" class=\"btn btn-info btn-block\">Run</button>"
            << "</td>"
            << "</tr>"
            << "</tbody>"
            << "</table>"
            << "</form>"
            << "</body>"
            << "</html>";

        // Write the html content to the browser
        string html_content_string = html_content.str();
        auto self(shared_from_this());
        boost::asio::async_write(sock, boost::asio::buffer(html_content_string.c_str(), html_content_string.length()),
            [this, self](boost::system::error_code ec, size_t length) {
                if (!ec) {
                    do_close();
                } else {
                    perror("Handle panei CGI");
                }
            }
        );
    }

    void handle_console_cgi() {
        print_html();
        parse_query(string(query_string));
        print_table();
        make_shared<shellSession>(io_context, move(sock))->start();
    }

    void parse_query(string query) {
        vector<string> raw_queries;
        string host, port, fname;
        int counter = 0;
        int id = 0;
        #if 0
        cerr << query << endl;
        #endif

        raw_queries = my_split(query, '&');

        for (auto &s: raw_queries) {
            int x = s.find("=");
            string value;
            if (s.length() == 3) {
                continue;
            } else {
                value = s.substr(x+1, s.length() - x - 1);
            }

            switch (counter % ATTRIBUTES_SIZE) {
                case 0:
                    /* host */
                    host = value;
                    break;
                case 1:
                    /* port */
                    port = value;
                    break;
                case 2:
                    /* file */
                    fname = value;
                    break;
            }
            ++counter;

            if (host != "" && port != "" && fname != "") {
                ClientInfo client = { id, host, port, fname, "", NULL, NULL };
                clients.push_back(client);
                host.clear();
                port.clear();
                fname.clear();
                ++id;
            }
        }

        #if 0
        for (size_t x=0; x < clients.size(); ++x) {
            cerr << "ID: " << clients[x].ID
                <<  "Host: " << clients[x].host
                << " Port: " << clients[x].port
                << " File: " << clients[x].fname << endl;
        }
        cerr << "Active sessions: " << clients.size() << endl;
        #endif

        return;
    }

    void print_html() {
        ostringstream oss;
        string result;

        oss << "Content-type: text/html\r\n\r\n";
        oss << "\
    <!DOCTYPE html>\
    <html lang=\"en\">\
    <head>\
        <meta charset=\"UTF-8\" />\
        <title>NP Project 3 Console</title>\
        <link\
        rel=\"stylesheet\"\
        href=\"https://cdn.jsdelivr.net/npm/bootstrap@4.5.3/dist/css/bootstrap.min.css\"\
        integrity=\"sha384-TX8t27EcRE3e/ihU7zmQxVncDAy5uIKz4rEkgIXeMed4M0jlfIDPvg6uqKI2xXr2\"\
        crossorigin=\"anonymous\"\
        />\
        <link\
        href=\"https://fonts.googleapis.com/css?family=Source+Code+Pro\"\
        rel=\"stylesheet\"\
        />\
        <link\
        rel=\"icon\"\
        type=\"image/png\"\
        href=\"https://cdn0.iconfinder.com/data/icons/small-n-flat/24/678068-terminal-512.png\"\
        />\
        <style>\
        * {\
            font-family: 'Source Code Pro', monospace;\
            font-size: 1rem !important;\
        }\
        body {\
            background-color: #212529;\
        }\
        pre {\
            color: #cccccc;\
        }\
        b {\
            color: #01b468;\
        }\
        </style>\
    </head>\
    <body>\
        <table class=\"table table-dark table-bordered\">\
        <thead>\
            <tr id=\"table_head\"> </tr>\
        </thead>\
        <tbody>\
            <tr id=\"session\"> </tr>\
        </tbody>\
        </table>\
    </body>\
    </html>";

        result = oss.str();
        do_cgi_console_write(result);
    }

    void print_table() {
        ostringstream oss;
        string result;

        for (size_t x=0; x < clients.size(); ++x) {
            oss << "<script>var table = document.getElementById('table_head'); table.innerHTML += '<th scope=\"col\">"
                << clients[x].host << ":" << clients[x].port
                << "</th>';</script>";
            oss << "<script>var table = document.getElementById('session'); table.innerHTML += '<td><pre id=\\'s"
                << clients[x].ID
                << "\\' class=\\'mb-0\\'></pre></td>&NewLine;' </script>";
        }
        result = oss.str();

        do_cgi_console_write(result);
    }

    void do_cgi_console_write(string content) {
        auto self(shared_from_this());
        boost::asio::async_write(sock, boost::asio::buffer(content.c_str(), content.length()),
            [this, self](boost::system::error_code ec, size_t length) {
                /* Write Handler */
                if (!ec) {
                    // do nothing
                } else {
                    cerr << "Do CGI console write, error: " << ec.message() << endl;
                }
            }
        );
    }
};

class server {
public:
    server(boost::asio::io_context& io_context, short port): acceptor_(io_context, tcp::endpoint(tcp::v4(), port)) {
        boost::asio::socket_base::reuse_address option(true);
        acceptor_.set_option(option);

        do_accept();
    }

private:
    void do_accept() {
        acceptor_.async_accept(
            [this](boost::system::error_code ec, tcp::socket socket) {
                if (!ec) {
                    make_shared<htmlSession>(move(socket))->start();
                }

                do_accept();
            }
        );
    }

    tcp::acceptor acceptor_;
};

int main(int argc, char* argv[]) {
    try {
        if (argc != 2) {
            cerr << "Usage: http_server <port>\n";
            return 1;
        }

        server s(io_context, atoi(argv[1]));

        io_context.run();
    } catch (exception& e) {
        cerr << "Exception: " << e.what() << "\n";
    }

    return 0;
}

//D4