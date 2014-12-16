#ifndef  TRACKER_H
#define  TRACKER_H
#include <netinet/in.h>
#include "parse_metafile.h"

typedef struct _Peer_addr 
{
	char              ip[16];
	unsigned short    port;
	struct _Peer_addr *next;
} Peer_addr;

//  用于将info_hash和peer_id转换为http编码格式
//  http协议规定,传输数据中的非数字和非字母都要进行编码转换
int http_encode(unsigned char *in,int len1,char *out,int len2);
//  从种子文件中存储的tracker的URL获取tracker主机名
int get_tracker_name(Announce_list *node,char *name,int len);
//  从种子文件中存储的tracker的URL获取tracker端口号
int get_tracker_port(Announce_list *node,unsigned short *port);

//  创建发送到tracker服务器的请求信息
int create_request(char *request, int len,Announce_list *node,
				   unsigned short port,long long down,long long up,
				   long long left,int numwant);

//  准备连接tracker
int prepare_connect_tracker(int *max_sockfd);
//  准备连接peer
int prepare_connect_peer(int *max_sockfd);

//  获取tracker返回的消息的类型
//  一种类型为"5:peers"关键字之后是一个字符串,另一种是一个列表
int get_response_type(char *buffer,int len,int *total_length);
//  解析第一种tracker返回的消息
int parse_tracker_response1(char *buffer,int ret,char *redirection,int len);
//  解析第二种tracker返回的消息
int parse_tracker_response2(char *buffer,int ret);
//  将与之建立连接的peer加入到peer列表中
int add_peer_node_to_peerlist(int *sock,struct sockaddr_in saptr);

void free_peer_addr_head();
//  释放本文件函数中动态分配的内存以防止内存泄漏
void release_memory_in_tracker();

#endif
