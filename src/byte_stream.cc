#include "byte_stream.hh"
#include <cstdint>
#include <string>
#include <type_traits>
#include <utility>

using namespace std;

ByteStream::ByteStream( uint64_t capacity ) : capacity_( capacity ) {}

bool Writer::is_closed() const
{
  return is_closed_var;
}

void Writer::push( string data )
{
  if ( available_capacity() == 0 || data.empty() ) {
    return;
  }
  uint64_t writeDatalen = min( data.length(), available_capacity() );
  data = data.substr( 0, writeDatalen );
  pushedBytes = pushedBytes + writeDatalen;
  bufferBytes = bufferBytes + writeDatalen;
  // load data into queue
  data_queue_.emplace_back( move( data ) );
  // add a string view which we will use in the reader part
  // view_queue_.emplace_back( data_queue_.back() );
  // 最开始viewData没有初始化(为空)，这个时候需要将它归位到首位字节
  if ( viewData.empty() ) {
    viewData = data_queue_.front();
  }
  return;
}

void Writer::close()
{
  is_closed_var = true;
}

uint64_t Writer::available_capacity() const
{
  // writer处计算available_capacity的方法是：
  // 由一个计数器得到当前收发的数量和（发送+，接收-），通过capacity- 目前存在buffer里面的 即可得到available
  return capacity_ - bufferBytes;
}

uint64_t Writer::bytes_pushed() const
{
  // Total number of bytes cumulatively pushed to the stream
  return pushedBytes;
}

bool Reader::is_finished() const
{
  // Is the stream finished (closed and fully popped)? 当write==read量的时候，就说明完全的pop出去了
  return is_closed_var && bufferBytes == 0;
}

uint64_t Reader::bytes_popped() const
{
  // Total number of bytes cumulatively popped from stream
  return popedBytes;
}

string_view Reader::peek() const
{
  // Peek at the next bytes in the buffer
  if ( viewData.empty() ) {
    return {};
  }
  return viewData;
}

void Reader::pop( uint64_t len )
{
  // Remove `len` bytes from the buffer
  auto n = min( len, bufferBytes );
  while ( n > 0 ) {
    auto sz = viewData.length();
    if ( n <= sz ) {
      viewData.remove_prefix( n );
      bufferBytes -= n;
      popedBytes += n;
      // 检测是否viewData空了
      if ( viewData.empty() ) {
        data_queue_.pop_front();
        if ( data_queue_.empty() ) {
          viewData = ""; // 将viewData置空
        } else {
          viewData = data_queue_.front();
        }
        // viewData = data_queue_.empty() ? "" : data_queue_.front();
      }
      return;
    }
    // view_queue_.pop_front();
    data_queue_.pop_front();
    // viewData = data_queue_.empty() ? "" : data_queue_.front();
    if ( data_queue_.empty() ) {
      viewData = ""; // 将viewData置空
    } else {
      viewData = data_queue_.front();
    }
    n -= sz;
    bufferBytes -= sz;
    popedBytes += sz;
  }
}

uint64_t Reader::bytes_buffered() const
{
  // Number of bytes currently buffered (pushed and not popped)
  return bufferBytes;
}
