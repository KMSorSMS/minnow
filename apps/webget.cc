#include "address.hh"
#include "socket.hh"

#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <span>
#include <string>
#include <sys/socket.h>

using namespace std;

void get_URL( const string& host, const string& path )
{
  // 得到address地址
  Address addre = Address( host, "http" );
  // 接下来就可以通过TCP协议，发送连接请求,注意到进程的socket是和一个port
  // bind一起的，这里初始化的socket还没有bind任何进程
  TCPSocket tsocket = TCPSocket();
  // 原本应该这里bind一个port,但是客户端是不需要主动做这个操作的，只需要发起连接的时候由操作系统创建一个即可
  tsocket.connect( addre );
  // 连接成功过后，发送get请求
  string request = "GET " + path + " HTTP/1.1\r\n" + "Host: " + host + "\r\n" + "Connection: close\r\n\r\n";
  tsocket.write( request );
  string buffer;
  while ( !tsocket.eof() ) {
    tsocket.read( buffer );
    cout << buffer;
  }
}

int main( int argc, char* argv[] )
{
  try {
    if ( argc <= 0 ) {
      abort(); // For sticklers: don't try to access argv[0] if argc <= 0.
    }

    auto args = span( argv, argc );

    // The program takes two command-line arguments: the hostname and "path" part of the URL.
    // Print the usage message unless there are these two arguments (plus the program name
    // itself, so arg count = 3 in total).
    if ( argc != 3 ) {
      cerr << "Usage: " << args.front() << " HOST PATH\n";
      cerr << "\tExample: " << args.front() << " stanford.edu /class/cs144\n";
      return EXIT_FAILURE;
    }

    // Get the command-line arguments.
    const string host { args[1] };
    const string path { args[2] };

    // Call the student-written function.
    get_URL( host, path );
  } catch ( const exception& e ) {
    cerr << e.what() << "\n";
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
