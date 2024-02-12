#include "byte_stream.hh"
#include <cstdint>
#include <string>
#include <type_traits>

using namespace std;
uint64_t ByteStream::writerWrite = 0;
uint64_t ByteStream::readerRecei = 0;
bool ByteStream::is_closed_var = false;
bool ByteStream::is_finished_var = false;
string ByteStream::buffer;

ByteStream::ByteStream( uint64_t capacity ) : capacity_( capacity ) { is_closed_var = false,is_finished_var = false;}

bool Writer::is_closed() const
{
  return is_closed_var;
}

void Writer::push( string data )
{
  uint64_t writeDatalen = data.length() > available_capacity() ? available_capacity() : data.length();
  writerWrite = writerWrite + writeDatalen;
  buffer = buffer + data.substr( 0, writeDatalen );
  return;
}

void Writer::close()
{
  is_closed_var = true;
}

uint64_t Writer::available_capacity() const
{
  // writer处计算available_capacity的方法是：
  // 由一个计数器得到当前收发的数量和（发送+，接收-），通过capacity-收发 得到available
  return capacity_ - ( writerWrite - readerRecei );
}

uint64_t Writer::bytes_pushed() const
{
  // Your code here.
  return writerWrite;
}

bool Reader::is_finished() const
{
  // Is the stream finished (closed and fully popped)? 当write==read量的时候，就说明完全的pop出去了
  if ( is_closed_var && writerWrite == readerRecei )
    is_finished_var = true;
  else
    is_finished_var = false;
  return is_finished_var;
}

uint64_t Reader::bytes_popped() const
{
  // Your code here.
  return {};
}

string_view Reader::peek() const
{
  // Your code here.
  return {};
}

void Reader::pop( uint64_t len )
{
  // Your code here.
  (void)len;
}

uint64_t Reader::bytes_buffered() const
{
  // Your code here.
  return buffer.length();
}
