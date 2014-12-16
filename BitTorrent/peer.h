#ifndef PEER_H
#define PEER_H

#include <string.h>
#include <time.h>
#include "bitfield.h"

#define  INITIAL              -1  // 表明处于初始化状态
#define  HALFSHAKED            0  // 表明处于半握手状态
#define  HANDSHAKED            1  // 表明处于全握手状态
#define  SENDBITFIELD          2  // 表明处于已发送位图状态
#define  RECVBITFIELD          3  // 表明处于已接收位图状态
#define  DATA                  4  // 表明处于与peer交换数据的状态
#define  CLOSING               5  // 表明处于即将与peer断开的状态

// 发送和接收缓冲区的大小,16K可以存放一个slice,2K可以存放其他消息
#define  MSG_SIZE  2*1024+16*1024

typedef struct _Request_piece 
{
	int     index;                // 请求的piece的索引
	int     begin;                // 请求的piece的偏移
	int     length;               // 请求的长度,一般为16KB
	struct _Request_piece *next;
} Request_piece;

typedef struct  _Peer 
{
	int            socket;                // 通过该socket与peer进行通信
	char           ip[16];                // peer的ip地址
	unsigned short port;                  // peer的端口号
	char           id[21];                // peer的id

	int            state;                 // 当前所处的状态

	int            am_choking;            // 是否将peer阻塞
	int            am_interested;         // 是否对peer感兴趣
	int            peer_choking;          // 是否被peer阻塞
	int            peer_interested;       // 是否被peer感兴趣

	Bitmap         bitmap;                // 存放peer的位图
	
	char           *in_buff;              // 存放从peer处获取的消息
	int            buff_len;              // 缓存区in_buff的长度
	char           *out_msg;              // 存放将发送给peer的消息
	int            msg_len;               // 缓冲区out_msg的长度
	char           *out_msg_copy;         // out_msg的副本,发送时使用该缓冲区
	int            msg_copy_len;          // 缓冲区out_msg_copy的长度
	int            msg_copy_index;        // 下一次要发送的数据的偏移量

	Request_piece  *Request_piece_head;   // 向peer请求数据的队列
	Request_piece  *Requested_piece_head; // 被peer请求数据的队列

	unsigned int   down_total;            // 从该peer下载的数据的总和
	unsigned int   up_total;              // 向该peer上传的数据的总和

	time_t         start_timestamp;       // 最近一次接收到peer消息的时间
	time_t         recet_timestamp;       // 最近一次发送消息给peer的时间

	time_t         last_down_timestamp;   // 最近下载数据的开始时间
	time_t         last_up_timestamp;     // 最近上传数据的开始时间
	long long      down_count;            // 本计时周期从peer下载的数据的字节数
	long long      up_count;              // 本计时周期向peer上传的数据的字节数
	float          down_rate;             // 本计时周期从peer处下载数据的速度
	float          up_rate;               // 本计时周期向peer处上传数据的速度

	struct _Peer   *next;                 // 指向下一个Peer结构体
} Peer;

int   initialize_peer(Peer *peer);        // 对peer进行初始化
Peer* add_peer_node();                    // 添加一个peer结点
int   del_peer_node(Peer *peer);          // 删除一个peer结点
void  free_peer_node(Peer *node);         // 释放一个peer的内存

int   cancel_request_list(Peer *node);    // 撤消当前请求队列
int   cancel_requested_list(Peer *node);  // 撤消当前被请求队列

void  release_memory_in_peer();           // 释放peer.c中的动态分配的内存
void  print_peers_data();                 // 打印peer链表中某些成员的值,用于调试

#endif
