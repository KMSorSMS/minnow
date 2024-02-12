#include "byte_stream.hh"
#include <cstdint>
#include <string>
#include <type_traits>

using namespace std;
uint64_t ByteStream::writerWrite = 0;
uint64_t ByteStream::readerRecei = 0;
bool ByteStream::is_closed_var = false;
bool ByteStream::is_finished_var = false;
string ByteStream::tunnel;
// string ByteStream::bufferWrite;
// string ByteStream::bufferRead;

ByteStream::ByteStream( uint64_t capacity ) : capacity_( capacity )
{
  is_closed_var = false, is_finished_var = false;
}

bool Writer::is_closed() const
{
  return is_closed_var;
}

void Writer::push( string data )
{
  uint64_t writeDatalen = data.length() > available_capacity() ? available_capacity() : data.length();
  writerWrite = writerWrite + writeDatalen;
  // buffer all the bytes to make sure reliable;
  bufferWrite = bufferWrite + data;
  // push all possible to the stream;
  tunnel = tunnel + bufferWrite.substr( 0, writeDatalen );
  // 删除掉已经传输的buffer内容
  bufferWrite = bufferWrite.substr( writeDatalen );
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
  // Total number of bytes cumulatively pushed to the stream
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
  // Total number of bytes cumulatively popped from stream
  return readerRecei;
}

string_view Reader::peek() const
{
  // Peek at the next bytes in the buffer
  string_view sv = tunnel;
  return sv;
}

void Reader::pop( uint64_t len )
{
  // Remove `len` bytes from the buffer
  bufferRead = bufferRead.substr( len );
  // 更新rederRecei
  readerRecei = readerRecei + len;
}

uint64_t Reader::bytes_buffered() const
{
  // Your code here.
  return bufferRead.length();
}
