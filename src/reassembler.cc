#include "reassembler.hh"
#include <cstdint>
#include <string>
#include <type_traits>

using namespace std;

void Reassembler::insert( uint64_t first_index, string data, bool is_last_substring )
{
  // judge if empty and need to check out of capacity
  // within capacity: first_index <= first_unacceptable_index
  if ( is_last_substring ) {
    has_last = true;
  }
  first_unacceptable_index = first_unassembled_index + output_.writer().available_capacity();
  if ( first_index < first_unassembled_index
       && first_index + data.size() <= first_unassembled_index ) { // 说明是之前assemble过的
    return;
  }
  if ( !data.empty() && first_index < first_unacceptable_index ) {
    // data和first需要一个预处理，主要是处理已经发送的data，也就是在first_unassembled_index之前的index内容去掉
    if ( first_index < first_unassembled_index && first_index + data.size() > first_unassembled_index ) {
      data = data.substr( first_unassembled_index - first_index ); // 截取到能够接受的index之后
      first_index = first_unassembled_index;
    }
    if ( data.size() + first_index > first_unacceptable_index ) { // 在这里把data截断
      data = data.substr( 0, first_unacceptable_index - first_index );
      has_last = false;
    }
    // using function to strip and insert the data
    bytes_pending_num += find_replace_addStr( data, first_index );
    // check if the nextBytes come then write bytes
    bool has_pushed = false;
    auto iter = data_set.begin();
    if ( first_index == first_unassembled_index ) {
      has_pushed = true;
    }
    while ( iter != data_set.end() && first_index == first_unassembled_index ) {
      output_.writer().push( iter->second );
      // 更新pendding数量
      bytes_pending_num -= iter->second.size();
      // 更新跟踪信息：first_unassembled_index、first_unacceptable_index
      first_unassembled_index += iter->second.size();
      first_unacceptable_index = first_unassembled_index + output_.writer().available_capacity();
      // 走到下一个iter
      iter++;
      first_index = iter->first;
    }
    if ( has_pushed ) {
      // 去除数据
      data_set.erase( data_set.begin(), iter );
    }
  }
  // if this is the last substring and it's not cut the tail
  // 判断是不是到了last_byte
  if ( has_last && data_set.empty() ) {
    output_.writer().close();
    bytes_pending_num = 0;
    return;
  }
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
  set<pair<uint64_t, string>>::iterator iter_up = data_set.upper_bound( { first_index + input_str.size(), "" } );
  //思路是对于上界的划分精细
  if(iter_down!=data_set.begin()){
    iter_down--;
  }else if(iter_down==iter_up){//这个说明data_set的所有index都比first_index大，那么就需要在iter_down == iter_up的时候去调整iter_down 向后
    iter_up++;
  }
  //前面保证了iter_down 不会取到end
  uint64_t count_insert = 0,start_index = first_index; // 记录插入的bytes数
  string opeStr = input_str;
  //截取操作进行规范，我们需要定义关键位置：理解这几个公式需要画图
  uint64_t leftDownIndex=0 ,leftUpIndex=0, rightDownIndex=0,rightUpindex=0 ;
  // 开始遍历分割字符串
  for ( ; iter_down != iter_up; iter_down++ ) {
    //更新关键位置变量：
    leftDownIndex = min(iter_down->first,start_index);
    leftUpIndex = min(iter_down->first,start_index+opeStr.size());
    rightDownIndex =  max(iter_down->first+iter_down->second.size(),start_index);
    rightUpindex = max(iter_down->first+iter_down->second.size(),start_index+opeStr.size());
    //截取
    if(leftDownIndex!=leftUpIndex){
      data_set.insert({leftDownIndex,opeStr.substr(0,leftUpIndex-leftDownIndex)});
    }
    //更新start、opeStr
    opeStr = rightDownIndex==rightUpindex ? "" : opeStr.substr(rightDownIndex-start_index,rightUpindex-rightDownIndex);
    start_index = rightDownIndex;
    count_insert += leftUpIndex-leftDownIndex;
  }
  if(!opeStr.empty()){
    data_set.insert({rightDownIndex,opeStr});
    count_insert += opeStr.size();
  }
  return count_insert;
}
