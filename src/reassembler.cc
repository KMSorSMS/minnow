#include "reassembler.hh"
#include <cstdint>
#include <string>

using namespace std;

void Reassembler::insert( uint64_t first_index, string data, bool is_last_substring )
{
  // judge if empty and need to check out of capacity
  // within capacity: first_index <= first_unacceptable_index
  first_unacceptable_index = first_unassembled_index + output_.writer().available_capacity();
  if ( !data.empty() && first_index < first_unacceptable_index ) {
    // data和first需要一个预处理，主要是处理已经发送的data，也就是在first_unassembled_index之前的index内容去掉
    if ( first_index < first_unassembled_index && first_index + data.size() > first_unassembled_index ) {
      data = data.substr( first_unassembled_index - first_index ); // 截取到能够接受的index之后
      first_index = first_unassembled_index;
    }
    if ( data.size() + first_index > first_unacceptable_index ) { // 在这里把data截断
      data = data.substr( 0, first_unacceptable_index - first_index );
    }
    // using function to strip and insert the data
    bytes_pending_num += find_replace_addStr( data, first_index );
    // check if the nextBytes come then write bytes
    while ( first_index == first_unassembled_index ) {
      output_.writer().push( data_set.begin()->second );
      // 更新pendding数量
      bytes_pending_num -= data_set.begin()->second.size();
      // 更新跟踪信息：first_unassembled_index、first_unacceptable_index
      first_unassembled_index += data_set.begin()->second.size();
      first_unacceptable_index = first_unassembled_index + output_.writer().available_capacity();
      // 去除数据
      data_set.erase( data_set.begin() );
      first_index = data_set.begin()->first;
    }
  }
  // if this is the last substring
  if ( is_last_substring )
    output_.writer().close();
}
// How many bytes are stored in the Reassembler itself?
uint64_t Reassembler::bytes_pending() const
{
  return bytes_pending_num;
}
/*
大致思路是，使用set的方法，找到第一个小于等于输入字符串index的元素（需要使用upper_bound，得到迭代器并且往前推一个）
然后再找到第一个大于等于index+len-1的元素，然后这两个元素之间的所有元素，都会参与切割这个字符串
切割操作是：从第一个元素开始，字符串分割为(0,index_ele-index)和(index_ele-index+ele.size)
对于前一个分割，直接加入set，对于后一个，继续进行分割。
*/
uint64_t Reassembler::find_replace_addStr( string& input_str, const uint64_t& first_index )
{
  // 对于最特殊的 data_set是空的情况，直接插入：
  if ( data_set.begin() == data_set.end() ) {
    data_set.insert( { first_index, input_str } );
    return input_str.size();
  }
  set<pair<uint64_t, string>>::iterator iter_down = data_set.upper_bound( { first_index, "" } );
  set<pair<uint64_t, string>>::iterator iter_up = data_set.lower_bound( { first_index + input_str.size(), "" } );
  // 校验是否为空
  if ( iter_down != data_set.end() ) {
    iter_down--; // 往前走一位到小于等于
  } else { // 说明最大的index都比它小,把iter_down切换成最大的index
    iter_down = data_set.end();
    iter_down--;
  }
  uint64_t count_insert = 0; // 记录插入的bytes数
  uint64_t start_index = first_index;
  string leftStr = {};
  string rightStr = input_str;
  // 开始遍历分割字符串
  for ( ; iter_down != iter_up; iter_down++ ) {
    leftStr = rightStr.substr( 0, iter_down->first - start_index );
    rightStr = rightStr.substr( iter_down->first - start_index + rightStr.size() );
    count_insert += leftStr.size();
    if ( !leftStr.empty() ) {
      data_set.insert( { start_index, leftStr } );
    }
    if ( !rightStr.empty() ) {
      start_index = iter_down->first + iter_down->second.size();
    } else {
      break;
    }
  }
  // 对于最后的iter_down == iter_up != end的情况，要再来一次切割.ps:这个情况好像能把空的情况合并了
  if ( iter_down == iter_up && iter_down != data_set.end() && !rightStr.empty() ) {
    leftStr = rightStr.substr( 0, iter_down->first - start_index );
    rightStr = rightStr.substr( iter_down->first - start_index + rightStr.size() );
    count_insert += leftStr.size();
    if ( !leftStr.empty() ) {
      data_set.insert( { start_index, leftStr } );
    }
    if ( !rightStr.empty() ) {
      start_index = iter_down->first + iter_down->second.size();
      // 这里就需要把右边也插入了
      data_set.insert( { start_index, rightStr } );
      count_insert += rightStr.size();
    }
  }
  return count_insert;
}
