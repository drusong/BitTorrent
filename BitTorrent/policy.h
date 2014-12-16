#ifndef POLICY_H
#define POLICY_H
#include "peer.h"

// 本文件实现了bittorrent协议的一些关键算法,主要有：
// 流水作业(一次生成对某个peer的多个slice请求,一般为5个)
// 片断选择算法(针对不同的下载阶段,有不同的选择策略)
// 阻塞算法(根据速度选择非阻塞peer)以及选择优化非阻塞peer
// 判断是否下载完毕(根据位图作出判断,下载完毕即终止程序)

// 每隔10秒计算一次各个peer的上传下载速度
#define COMPUTE_RATE_TIME  10
// 以下结构体存储下载速度最快的4个peer的指针
#define UNCHOKE_COUNT  4
// 每次请求的slice数
#define REQ_SLICE_NUM  5

typedef struct _Unchoke_peers {
	Peer*  unchkpeer[UNCHOKE_COUNT];
	int    count;
	Peer*  optunchkpeer;
} Unchoke_peers;


void init_unchoke_peers();     // 初始化全局变量unchoke_peers


int select_unchoke_peer();     // 选择unchoke peer
int select_optunchoke_peer();  // 从peer队列中选择一个优化非阻塞peer


int compute_rate();            // 计算最近一段时间(10秒)每个peer的上传下载速度
int compute_total_rate();      // 计算总的上传下载速度


int is_seed(Peer *node);       // 判断某个peer是否为种子

// 构造数据请求,为了提高效率一次请求5个slice
int create_req_slice_msg(Peer *node);  

#endif
