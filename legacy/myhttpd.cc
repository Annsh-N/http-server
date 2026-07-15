#include <algorithm>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <netdb.h>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <cerrno>
#include <ctime>
#include <string>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <thread>
#include <mutex>
#include <vector>
#include <sys/wait.h>
#include <dirent.h>
#include <string>
#include <fstream>


time_t serverStartTime;
int totalRequests = 0;
double minServiceTime = -1.0;
double maxServiceTime = -1.0;
std::string minURL;
std::string maxURL;

const std::string logFilePath = "log.txt";

const int QueueLength = 5;
const size_t MaxRequestSize = 8192;
const char *RootDir = "http-root-dir";
const std::string AUTH_TOKEN = "Basic <redacted>";
const std::string REALM = "myhttpd-cs252";

struct DirEntry {
  std::string name;
  off_t size;
  time_t mtime;
  bool is_dir;
};


// Helper to send all bytes in buffer
ssize_t sendAll(int fd, const void *buf, size_t len) {
    size_t total = 0;
    const char *p = (const char*) buf;
    while (total < len) {
        ssize_t sent = write(fd, p + total, len - total);
        if (sent <= 0) return sent;
        total += sent;
    }
    return total;
}

void handleCGI(int clientFd, const std::string& method, const std::string& url, const std::string& requestBody) {
    // Parse script path and query string
    std::string scriptPath, queryString;
    size_t qm_pos = url.find('?');
    if (qm_pos != std::string::npos) {
        scriptPath = url.substr(0, qm_pos);
        queryString = url.substr(qm_pos + 1);
    } else {
        scriptPath = url;
        queryString = "";
    }

    // Remove /cgi-bin/ prefix
    std::string script = scriptPath.substr(strlen("/cgi-bin/"));
    std::string scriptFullPath = "http-root-dir/cgi-bin/" + script;

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        return;
    }
    else if (pid == 0) {
        // CHILD

        // Set environment variables
        setenv("REQUEST_METHOD", method.c_str(), 1);

        if (method == "GET") {
            setenv("QUERY_STRING", queryString.c_str(), 1);
        }
        else if (method == "POST") {
            setenv("CONTENT_LENGTH", std::to_string(requestBody.size()).c_str(), 1);
        }

        // Redirect STDOUT to clientFd
        dup2(clientFd, STDOUT_FILENO);

        if (method == "POST") {
            // Redirect STDIN to read POST data
            // Create a pipe
            int pipefd[2];
            if (pipe(pipefd) < 0) {
                perror("pipe");
                exit(1);
            }
            // Write POST data to pipe
            write(pipefd[1], requestBody.c_str(), requestBody.size());
            close(pipefd[1]); // finished writing
            dup2(pipefd[0], STDIN_FILENO); // replace STDIN with pipe read end
            close(pipefd[0]);
        }

        // Send HTTP response header first
        std::cout << "HTTP/1.0 200 Document follows\r\n";
        std::cout << "Server: MyHTTPD\r\n";

        // Execute script
        char* argv[] = { (char*)script.c_str(), nullptr };
        execv(scriptFullPath.c_str(), argv);

        // If execv fails
        perror("execv");
        exit(1);
    }
    else {
        // PARENT
        waitpid(pid, NULL, 0);
        return;
    }
}

/* Function to handle stats */
void handleStats(int clientFd) {
    time_t now = time(NULL);
    double uptime = difftime(now, serverStartTime);

    std::ostringstream html;
    html << "<html><body>\n";
    html << "<h1>Server Stats</h1>\n";
    html << "<ul>\n";
    html << "<li>Students: Annsh Navle</li>\n";
    html << "<li>Uptime: " << uptime << " seconds</li>\n";
    html << "<li>Total Requests: " << totalRequests << "</li>\n";
    html << "<li>Min Service Time: " << minServiceTime << " seconds (" << minURL << ")</li>\n";
    html << "<li>Max Service Time: " << maxServiceTime << " seconds (" << maxURL << ")</li>\n";
    html << "</ul>\n";
    html << "</body></html>\n";

    std::string body = html.str();

    std::ostringstream hdr;
    hdr << "HTTP/1.0 200 OK\r\n"
        << "Content-Length: " << body.size() << "\r\n"
        << "Content-Type: text/html\r\n"
        << "\r\n";

    std::string header = hdr.str();
    sendAll(clientFd, header.c_str(), header.size());
    sendAll(clientFd, body.c_str(), body.size());
}

/* Function to hangle logs */
void handleLogs(int clientFd) {
    std::ifstream logfile(logFilePath);
    if (!logfile.is_open()) {
        std::string notFound = "<html><body><h1>No logs available</h1></body></html>";
        std::ostringstream hdr;
        hdr << "HTTP/1.0 404 Not Found\r\n"
            << "Content-Length: " << notFound.size() << "\r\n"
            << "Content-Type: text/html\r\n"
            << "\r\n";
        std::string header = hdr.str();
        sendAll(clientFd, header.c_str(), header.size());
        sendAll(clientFd, notFound.c_str(), notFound.size());
        return;
    }

    std::ostringstream body;
    body << "<html><body>\n";
    body << "<h1>Request Logs</h1>\n";
    body << "<pre>\n"; // preserves formatting

    std::string line;
    while (getline(logfile, line)) {
        body << line << "\n";
    }
    body << "</pre>\n";
    body << "</body></html>\n";

    logfile.close();

    std::string html = body.str();
    std::ostringstream hdr;
    hdr << "HTTP/1.0 200 OK\r\n"
        << "Content-Length: " << html.size() << "\r\n"
        << "Content-Type: text/html\r\n"
        << "\r\n";

    std::string header = hdr.str();
    sendAll(clientFd, header.c_str(), header.size());
    sendAll(clientFd, html.c_str(), html.size());
}

void handleClient(int clientFd, struct sockaddr_in clientAddr) {
    char request[MaxRequestSize + 1];
    std::string leftover;
    //bool keepAlive = true;

    char clientIP[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(clientAddr.sin_addr), clientIP, INET_ADDRSTRLEN);

    //while (keepAlive) {
    clock_t start = clock();
    ssize_t n = read(clientFd, request, MaxRequestSize);
    if (n <= 0) {
        perror("read error");
        return;
    }
    request[n] = '\0';

    std::string fullRequest(request);

    // Parse request line
    std::istringstream reqStream(fullRequest);
    std::string method, url, version;
    reqStream >> method >> url >> version;

    std::ofstream logfile(logFilePath, std::ios::app); // open log file
    if (logfile.is_open()) {
      logfile << clientIP << " " << url << "\n";
      logfile.close();
    }

    if (fullRequest.find("Authorization: " + AUTH_TOKEN) == std::string::npos) {
        std::ostringstream response;
        response << "HTTP/1.0 401 Unauthorized\r\n"
                << "WWW-Authenticate: Basic realm=\"" << REALM << "\"\r\n"
                << "Content-Length: 0\r\n"
                << "\r\n";
        std::string respStr = response.str();
        sendAll(clientFd, respStr.c_str(), respStr.size());
        return;
    }

    // wrong method
    if (method != "GET" && method != "POST") {
        std::ostringstream response;
        response << "HTTP/1.0 501 Not Implemented\r\n"
                 << "Content-Length: 0\r\n"
                 << "\r\n";
                 std::string resp = response.str();
        sendAll(clientFd, resp.c_str(), resp.size());
        return;
    }

    //default
    if (url == "/") {
      url = "/index.html";
    }

    if (url == "/stats") {
      handleStats(clientFd);
      return;
    }

    if (url == "/logs") {
      handleLogs(clientFd);
      return;
    }

    size_t pos = fullRequest.find("\r\n\r\n");
    std::string headerPart = fullRequest.substr(0, pos);
    std::string requestBody = fullRequest.substr(pos + 4);

    if (url.find("/cgi-bin/") == 0) {
      handleCGI(clientFd, method, url, requestBody);
      return;
    }

    //dir
    std::string dir;
    if (std::count(url.begin(), url.end(), '/') == 1) {
        dir = "/htdocs";
    } else {
        dir = "/htdocs";
    }


    char sortField = 'N'; // Default: Name
    char sortOrder = 'A'; // Default: Ascending

    // parse sorting options
    std::string::size_type qm_pos = url.find('?');
    if (qm_pos != std::string::npos) {
        std::string query = url.substr(qm_pos + 1);
        url = url.substr(0, qm_pos); // remove ?query part from url for display

        size_t c_pos = query.find("C=");
        size_t o_pos = query.find("O=");
        if (c_pos != std::string::npos && c_pos + 2 < query.size())
          sortField = query[c_pos + 2];
        if (o_pos != std::string::npos && o_pos + 2 < query.size())
          sortOrder = query[o_pos + 2];
    }

    //file path
    std::string path = std::string(RootDir) + dir + url;

    // for debugging
    //std::cout << "Path is: " + path <<std::endl;

    // open file
    struct stat sb;
    if (stat(path.c_str(), &sb) == 0) {
      if (S_ISDIR(sb.st_mode)) {

        if (url.back() != '/') {
          // URL does not end with /, but it's a directory
          // → Redirect to URL with trailing slash
          std::ostringstream hdr;
          hdr << "HTTP/1.0 301 Moved Permanently\r\n"
              << "Location: " << url << "/" << "\r\n"
              << "Content-Length: 0\r\n"
              << "\r\n";

          std::string header = hdr.str();
          sendAll(clientFd, header.c_str(), header.size());
          return;
        }
        //directory

        //read dir entries
        DIR* dir = opendir(path.c_str());
        // 404 not found
        if (!dir) {
          const char* notFound = "<html><body><h1>404 Not Found</h1></body></html>";
          std::ostringstream hdr;
          hdr << "HTTP/1.0 404 Not Found\r\n"
              << "Content-Length: " << strlen(notFound) << "\r\n"
              << "Content-Type: text/html\r\n"
              << "\r\n";
          std::string header = hdr.str();
          sendAll(clientFd, header.c_str(), header.size());
          sendAll(clientFd, notFound, strlen(notFound));
          return;
        }

        //create vector of entries
        std::vector<DirEntry> entries;
        struct dirent* entry;
        while ((entry = readdir(dir)) != NULL) {
          std::string entryName = entry->d_name;
          if (entryName == "." || entryName == "..") continue; // skip .

          std::string fullEntryPath = path + "/" + entryName;

          struct stat entryStat;
          if (stat(fullEntryPath.c_str(), &entryStat) == 0) {
            entries.push_back({
              entryName,
              entryStat.st_size,
              entryStat.st_mtime,
              S_ISDIR(entryStat.st_mode)
            });
          }
        }
        closedir(dir);

        //compare functions for sorting
        auto cmpNameAsc = [](const DirEntry& a, const DirEntry& b) {
          return a.name < b.name;
        };
        auto cmpNameDesc = [](const DirEntry& a, const DirEntry& b) {
          return a.name > b.name;
        };
        auto cmpSizeAsc = [](const DirEntry& a, const DirEntry& b) {
          return a.size < b.size;
        };
        auto cmpSizeDesc = [](const DirEntry& a, const DirEntry& b) {
          return a.size > b.size;
        };
        auto cmpMtimeAsc = [](const DirEntry& a, const DirEntry& b) {
          return a.mtime < b.mtime;
        };
        auto cmpMtimeDesc = [](const DirEntry& a, const DirEntry& b) {
          return a.mtime > b.mtime;
        };

        //sort entries
        if (sortField == 'N') {
          if (sortOrder == 'D')
            std::sort(entries.begin(), entries.end(), cmpNameDesc);
          else
            std::sort(entries.begin(), entries.end(), cmpNameAsc);
        }
        else if (sortField == 'S') {
          if (sortOrder == 'D')
            std::sort(entries.begin(), entries.end(), cmpSizeDesc);
          else
            std::sort(entries.begin(), entries.end(), cmpSizeAsc);
        }
        else if (sortField == 'M') {
          if (sortOrder == 'D')
            std::sort(entries.begin(), entries.end(), cmpMtimeDesc);
          else
            std::sort(entries.begin(), entries.end(), cmpMtimeAsc);
        }

        //generate html page
        std::ostringstream html;
        html << "<html><body>\n";
        html << "<h1>Index of " << url << "</h1>\n";
        html << "<table>\n";
        html << "<tr><th valign=\"top\"><img src=\"/icons/blank.gif\" alt=\"[ICO]\"</th>";
        html << "<th><a href=\"?C=N;O=" << ((sortField == 'N' && sortOrder == 'A') ? "D" : "A") << "\">Name</a></th>"
             << "<th><a href=\"?C=M;O=" << ((sortField == 'M' && sortOrder == 'A') ? "D" : "A") << "\">Last modified</a></th>"
             << "<th><a href=\"?C=S;O=" << ((sortField == 'S' && sortOrder == 'A') ? "D" : "A") << "\">Size</a></th></tr>\n";
        html << "<tr><th colspan=\"4\"><hr></th></tr>\n";

        //parent dir link
        if (url != "/") {
          std::string parent = url.substr(0, url.find_last_of('/', url.length() - 2));
          if (parent.empty()) parent = "/";
          else parent += "/";

          html << "<tr>"
               << "<td valign=\"top\"><img src=\"/icons/back.gif\" alt=\"[PARENTDIR]\"></td>"
               << "<td><a href=\"" << parent << "\">Parent Directory</a></td>"
               << "<td align=\"right\">&nbsp;</td>"
               << "<td align=\"right\"> - </td>"
               << "<td>&nbsp;</td>"
               << "</tr>\n";
        }

        // Now list actual entries
        for (const auto& e : entries) {
          std::string icon = e.is_dir ? "/icons/folder.gif" : "/icons/text.gif";
          std::string altText = e.is_dir ? "[DIR]" : "[FILE]";

          html << "<tr>";

          // Icon
          html << "<td valign=\"top\"><img src=\"" << icon << "\" alt=\"" << altText << "\"></td>";

          // File/folder link
          html << "<td><a href=\"" << url;
          if (url.back() != '/') html << "/";
          html << e.name;
          if (e.is_dir) html << "/";
          html << "\">" << e.name << "</a></td>";

          // Last modified time
          char timebuf[64];
          struct tm* tm_info = localtime(&e.mtime);
          strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M", tm_info);
          html << "<td align=\"right\">" << timebuf << "</td>";

          // Size
          if (e.is_dir) {
              html << "<td align=\"right\"> - </td>";
          } else {
              html << "<td align=\"right\">" << e.size << "</td>";
          }

          // Description (empty)
          html << "<td>&nbsp;</td>";

          html << "</tr>\n";
        }

        html << "<tr><th colspan=\"4\"><hr></th></tr>\n";
        html << "</table>\n";
        html << "</body></html>\n";

        //send html and response header
        std::string body = html.str();
        std::ostringstream hdr;
        hdr << "HTTP/1.0 200 OK\r\n"
            << "Content-Length: " << body.size() << "\r\n"
            << "Content-Type: text/html\r\n"
            << "\r\n";

        std::string header = hdr.str();
        sendAll(clientFd, header.c_str(), header.size());
        sendAll(clientFd, body.c_str(), body.size());
      }
      else {
        //normal file
        int fileFd = open(path.c_str(), O_RDONLY);
        if (fileFd < 0) {
            perror("open");
            return;
        }
        // make header
        std::ostringstream hd;
        hd << "HTTP/1.0 200 OK\r\n"
           << "Content-Length: " << sb.st_size << "\r\n"
           << "Content-Type: text/html\r\n"
           << "\r\n";
        std::string header = hd.str();
        sendAll(clientFd, header.c_str(), header.size());
        // send file
        const size_t BufSize = 4096;
        char buf[BufSize];
        ssize_t r;
        while ((r = read(fileFd, buf, BufSize)) > 0) {
            if (sendAll(clientFd, buf, r) < 0) break;
        }
        close(fileFd);
      }
    } else {
        // 404 Not Found
        const char *body = "<html><body><h1>404 Not Found</h1></body></html>\n";
        std::ostringstream hd;
        hd << "HTTP/1.0 404 Not Found\r\n"
           << "Content-Length: " << strlen(body) << "\r\n"
           << "Content-Type: text/html\r\n"
           << "\r\n";
        std::string header = hd.str();
        sendAll(clientFd, header.c_str(), header.size());
        sendAll(clientFd, body, strlen(body));
    }

    clock_t end = clock();
    double serviceTime = (double)(end - start) / CLOCKS_PER_SEC;

    // Update statistics
    totalRequests++;

    if (minServiceTime < 0 || serviceTime < minServiceTime) {
      minServiceTime = serviceTime;
      minURL = url;
    }
    if (serviceTime > maxServiceTime) {
      maxServiceTime = serviceTime;
      maxURL = url;
    }
    //} //while end
}

int main(int argc, char **argv) {
    char mode = 'i';  //default mode is iterative
    int port = 42069l; //default port

    if (argc >= 2) {
      if (argv[1][0] == '-') {
        mode = argv[1][1];
        if (argc >= 3) {
          port = atoi(argv[2]);
        }
      } else {
        port = atoi(argv[1]);
      }
    }

    // socket
    int masterSocket = socket(PF_INET, SOCK_STREAM, 0);
    if (masterSocket < 0) {
        perror("socket");
        return EXIT_FAILURE;
    }

    // address reuse
    int opt = 1;
    if (setsockopt(masterSocket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt");
        close(masterSocket);
        return EXIT_FAILURE;
    }

    // bind
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons((unsigned short)port);
    if (bind(masterSocket, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(masterSocket);
        return EXIT_FAILURE;
    }

    // listen
    if (listen(masterSocket, QueueLength) < 0) {
        perror("listen");
        close(masterSocket);
        return EXIT_FAILURE;
    }
    std::cout << "Server listening on port " << port << "...\n";
    struct sockaddr_in clientAddr;
    socklen_t clientLen = sizeof(clientAddr);

    serverStartTime = time(NULL);

    if (mode == 'i') {
      // itertive mode
      while (true) {
          int clientFd = accept(masterSocket, (struct sockaddr*)&clientAddr, &clientLen);
          if (clientFd < 0) {
              perror("accept");
              continue;
          }
          handleClient(clientFd, clientAddr);
          close(clientFd);
      }
    }
    else if (mode == 'f') {
      while (true) {
        int clientFd = accept(masterSocket, (struct sockaddr*)&clientAddr, &clientLen);
        if (clientFd < 0) {
          perror("accept");
          continue; 
        }

        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            close(clientFd);
        } else if (pid == 0) {
            // child process
            close(masterSocket);
            handleClient(clientFd, clientAddr);
            close(clientFd);
            exit(0);  // exit child
        } else {
            // parent process
            close(clientFd);
            waitpid(-1, NULL, WNOHANG);  // reap zombie immediately
        }
      }
    }
    else if (mode == 't') {
      // single thread per client
      while (true) {
        int clientFd = accept(masterSocket, (struct sockaddr*)&clientAddr, &clientLen);
        if (clientFd < 0) {
          perror("accept");
          continue;
        }

        std::thread([clientFd, clientAddr]() {
            handleClient(clientFd, clientAddr);
            close(clientFd);
        }).detach();  // fire and forget
      }
    }
    else if (mode == 'p') {
      // thread pool
      std::mutex acceptMutex;
      const int poolSize = 5;
      std::vector<std::thread> workers;

      for (int i = 0; i < poolSize; ++i) {
        workers.emplace_back([&]() {
            while (true) {
                int clientFd;
                {
                    std::lock_guard<std::mutex> lock(acceptMutex);
                    clientFd = accept(masterSocket, (struct sockaddr*)&clientAddr, &clientLen);
                    if (clientFd < 0) {
                      perror("accept");
                      continue;
                    }
                }
                handleClient(clientFd, clientAddr);
                close(clientFd);
            }
        });
      }

      for (auto& t : workers) t.join();
    }

    close(masterSocket);
    return EXIT_SUCCESS;
}
