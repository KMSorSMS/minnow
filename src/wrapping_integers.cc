#include "wrapping_integers.hh"
using namespace std;

Wrap32 Wrap32::wrap( uint64_t n, Wrap32 zero_point )
{
  // 2^32-1 = 4294967295
  // 如果uint64_t的值大于uint32_t可以表示的最大值（即4294967295或0xFFFFFFFF），那么转换的结果将会是原始值对4294967296（即2^32）取模的结果。
  return Wrap32 { ( (uint32_t)n + zero_point.raw_value_ ) };
}

uint64_t Wrap32::unwrap( Wrap32 zero_point, uint64_t checkpoint ) const
{
  // 计算出当前偏移量，利用了c++的特性处理负号情况
  uint64_t offset = raw_value_ - zero_point.raw_value_;
  // 找最接近的seqno,对offset进行非负处理，负数的话就直接先加偏移了;并且先让offset同阶，在该阶的左右找
  offset = offset + 4294967296 * ( checkpoint / 4294967296 );
  // 比较大小，选取distance最近的
  uint32_t dis = offset > checkpoint ? offset - checkpoint : checkpoint - offset;
  //采用比较骚的压行方式，算是对于底层的理解吧。看右边部分的dis是否小
  offset =  dis > offset + 4294967296 - checkpoint ? dis = offset + 4294967296 - checkpoint,offset + 4294967296:offset;
  // 看左边部分的dis是否小
  return offset = offset > 4294967296 && dis > checkpoint - offset + 4294967296 ? offset - 4294967296 : offset;
}
